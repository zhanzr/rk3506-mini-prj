/*
  person_vis - "is a person visible?" on the ST7365P 3.5" 320x480 (MD350) SPI panel, using the MLPerf-Tiny person-detection model
  (person_detect.tflite, MobileNetV1 0.25 96x96x1 int8, Apache-2.0)
  run by TensorFlow Lite Micro (vendored lib, cross-built with gcc-12).

  Same capture/panel stack as the other camera apps in this group (RGB565 by default;
  pass -6 for RGB666).
  320x240 luma sample -> 96x96 box-downsample -> int8 quantize ->
  TFLM invoke on a detector pthread (video keeps streaming at ~15 fps;
  one inference takes a few hundred ms on the H3). A big PERSON/NO
  PERSON verdict + score is drawn over the live view.

  Model file: person_detect.tflite (copied next to the binary).
*/
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "model_settings.h"

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

extern "C" {
#include "lcd.h"
#include "spi_hw.h"
#include "lcd_fonts.h"
#include "lcd_font_1608.h"

/* ---- backlight PWM (JP4.15, pwm1_8ch_0 @ ff170000, rk3506-mini) ----
 * 200 kHz normal polarity, fixed 55% (bright enough to judge detection
 * results). The opz board had BLK hardwired to 3.3 V - no PWM there. */
#define BL_PERIOD_NS 5000

static int g_bl_pct = -1;

static void backlight_init(void)
{
    char path[128];
    FILE *fp;

    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip0/export");
    fp = fopen(path, "w");
    if (fp) { fprintf(fp, "0"); fclose(fp); }

    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip0/pwm0/enable");
    fp = fopen(path, "w");
    if (fp) { fprintf(fp, "0"); fclose(fp); }

    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip0/pwm0/polarity");
    fp = fopen(path, "w");
    if (fp) { fprintf(fp, "normal"); fclose(fp); }

    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip0/pwm0/duty_cycle");
    fp = fopen(path, "w");
    if (fp) { fprintf(fp, "0"); fclose(fp); }

    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip0/pwm0/period");
    fp = fopen(path, "w");
    if (fp) { fprintf(fp, "%d", BL_PERIOD_NS); fclose(fp); }
}

static void backlight_set(int percent)
{
    char path[128];
    FILE *fp;

    if (percent == g_bl_pct)
        return;
    g_bl_pct = percent;

    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip0/pwm0/duty_cycle");
    fp = fopen(path, "w");
    if (fp) { fprintf(fp, "%d", BL_PERIOD_NS * percent / 100); fclose(fp); }

    snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip0/pwm0/enable");
    fp = fopen(path, "w");
    if (fp) { fprintf(fp, "1"); fclose(fp); }
}

}

#define PANEL_W 320
#define PANEL_H 480
#define FPS_BAND 24

static uint16_t panel_fb[PANEL_W * PANEL_H];
static uint8_t panel_666[PANEL_W * PANEL_H * 3];
static int pix18 = 0;

static volatile sig_atomic_t quit_req = 0;
static void on_signal(int sig) { (void)sig; quit_req = 1; }

/* ---- panel primitives (dual-buffer, 565 + 666) ---- */

static void fb_string_t(const char *s, int x, int y, uint16_t fg)
{
    while (*s)
    {
        uint8_t c = *s;
        if (c < 0x20 || c > 0x7E) c = '?';
        int idx = c - 0x20;
        for (int row = 0; row < 16; row++)
        {
            uint8_t bits = asc2_1608[idx * 16 + row];
            int py = y + row;
            if (py < 0 || py >= PANEL_H) continue;
            for (int col = 0; col < 8; col++)
            {
                int px = x + col;
                if (px < 0 || px >= PANEL_W) continue;
                if ((bits >> col) & 1)
                {
                    panel_fb[py * PANEL_W + px] = fg;
                    if (pix18)
                    {
                        uint8_t *d6 = &panel_666[(py * PANEL_W + px) * 3];
                        unsigned r = (fg >> 11) & 0x1F, g = (fg >> 5) & 0x3F,
                                 bl = fg & 0x1F;
                        d6[0] = (uint8_t)((r << 3) | (r >> 2));
                        d6[1] = (uint8_t)((g << 2) | (g >> 4));
                        d6[2] = (uint8_t)((bl << 3) | (bl >> 2));
                    }
                }
            }
        }
        x += 8;
        s++;
    }
}

static int panel_ok = 0;

static void panel_init(void)
{
    spi_res_open(49);
    spi_dc_open(47);
    backlight_init();
    backlight_set(55);
    if (spi_open(100 * 1000 * 1000) != 0) { panel_ok = 0; return; }
    LCD_Init();
    LCD_SetAsciiFont(&ASCII_Font16);
    LCD_Clear();
    LCD_SetPix18(pix18);
    panel_ok = 1;
    printf("panel: MD350 enabled (%s)\n",
           pix18 ? "18-bit RGB666" : "16-bit RGB565");
    fflush(stdout);
}

static void blit(void)
{
    LCD_SetAddress(0, 0, PANEL_W - 1, PANEL_H - 1);
    if (!pix18)
    {
        spi_write_pixels16(panel_fb, PANEL_W * PANEL_H);
        return;
    }
    static uint8_t row18[PANEL_W * 3];
    for (int py = 0; py < PANEL_H; py++)
    {
        const uint8_t *src = &panel_666[py * PANEL_W * 3];
        for (int px = 0; px < PANEL_W * 3; px++)
            row18[px] = src[px] & 0xFC;
        spi_write_buf(row18, sizeof(row18));
    }
}

/* camera -> panel map */
static int16_t *src_map;
static double map_s, map_offx, map_offy;
static int map_rw;

static void build_src_map(int cw, int ch)
{
    double rw = ch, rh = cw;
    double s = rw > 0 && rh > 0 ?
               (PANEL_W / rw < PANEL_H / rh ? PANEL_W / rw : PANEL_H / rh) : 1;
    double offx = (PANEL_W - rw * s) / 2;
    double offy = (PANEL_H - rh * s) / 2;

    free(src_map);
    src_map = (int16_t *)malloc(2 * sizeof(int16_t) * PANEL_W * PANEL_H);
    for (int py = 0; py < PANEL_H; py++)
        for (int px = 0; px < PANEL_W; px++)
        {
            int sx_rot = (int)((px - offx) / s);
            int sy_rot = (int)((py - offy) / s);
            int sx_cam = sy_rot;
            int sy_cam = (int)rw - 1 - sx_rot;
            if (sx_rot < 0 || sx_rot >= (int)rw ||
                sy_rot < 0 || sy_rot >= (int)rh)
            {
                src_map[(py * PANEL_W + px) * 2] = -1;
                src_map[(py * PANEL_W + px) * 2 + 1] = -1;
            }
            else
            {
                src_map[(py * PANEL_W + px) * 2] = (int16_t)sx_cam;
                src_map[(py * PANEL_W + px) * 2 + 1] = (int16_t)sy_cam;
            }
        }
    map_s = s; map_offx = offx; map_offy = offy; map_rw = (int)rw;
    printf("uvc: map built (%dx%d -> %dx%d, scale %.3f)\n",
           cw, ch, PANEL_W, PANEL_H, s);
}

static inline void put_px(int px, int py, uint16_t fg)
{
    if (px < 0 || px >= PANEL_W || py < 0 || py >= PANEL_H) return;
    panel_fb[py * PANEL_W + px] = fg;
    if (pix18)
    {
        uint8_t *d6 = &panel_666[(py * PANEL_W + px) * 3];
        unsigned r = (fg >> 11) & 0x1F, g = (fg >> 5) & 0x3F, bl = fg & 0x1F;
        d6[0] = (uint8_t)((r << 3) | (r >> 2));
        d6[1] = (uint8_t)((g << 2) | (g >> 4));
        d6[2] = (uint8_t)((bl << 3) | (bl >> 2));
    }
}

/* rectangle in CAMERA coords -> 2 px green frame on the panel */
static void fb_rect_cam(int cx, int cy, int cwid, int chgt)
{
    int px1 = (int)((map_rw - 1 - (cy + chgt)) * map_s + map_offx);
    int px2 = (int)((map_rw - 1 - cy) * map_s + map_offx);
    int py1 = (int)(cx * map_s + map_offy);
    int py2 = (int)((cx + cwid) * map_s + map_offy);

    if (px1 > px2) { int t = px1; px1 = px2; px2 = t; }
    if (py1 > py2) { int t = py1; py1 = py2; py2 = t; }
    px1 -= 1; px2 += 1; py1 -= 1; py2 += 1;
    if (px1 < 0) px1 = 0;
    if (py1 < 0) py1 = 0;
    if (px2 > PANEL_W - 1) px2 = PANEL_W - 1;
    if (py2 > PANEL_H - 1) py2 = PANEL_H - 1;
    if (px2 < px1 || py2 < py1) return;
    for (int t = 0; t < 2; t++)
    {
        int xa = px1 + t, xb = px2 - t, ya = py1 + t, yb = py2 - t;
        if (xa > xb || ya > yb) break;
        for (int px = xa; px <= xb; px++)
        { put_px(px, ya, 0x07E0); put_px(px, yb, 0x07E0); }
        for (int py = ya; py <= yb; py++)
        { put_px(xa, py, 0x07E0); put_px(xb, py, 0x07E0); }
    }
}

/* ---- detector thread ---- */

#define MAX_PEDS 8
struct det_box { int x, y, w, h, conf; };
static struct det_box peds[MAX_PEDS];
static int n_peds = 0;
static int det_ms = 0;
static int det_pending = 0;
static int det_quit = 0;
static pthread_mutex_t det_lock = PTHREAD_MUTEX_INITIALIZER;

/* TFLM state (single detector thread -> single interpreter) */
alignas(16) static uint8_t tflm_arena[136 * 1024];
alignas(16) static uint8_t model_buf[400 * 1024];
static const tflite::Model *tflm_model = nullptr;
static tflite::MicroInterpreter *tflm_interp = nullptr;
static tflite::MicroMutableOpResolver<5> *tflm_resolver = nullptr;
static int person_pct = -1;   /* last person score, -1 = not run yet */
static pthread_cond_t det_cond = PTHREAD_COND_INITIALIZER;
static unsigned char det_gray[320 * 240];  /* half-res luma sample */

static void det_publish(const uint8_t *yuyv, int cw, int ch)
{
    if (cw < 640 || ch < 480) return;
    pthread_mutex_lock(&det_lock);
    if (!det_pending)
    {
        for (int dy = 0; dy < 240; dy++)
        {
            const uint8_t *row = yuyv + (dy * 2) * cw * 2;
            uint8_t *out = det_gray + dy * 320;
            for (int dx = 0; dx < 320; dx++)
                out[dx] = row[(dx * 2) * 2];
        }
        det_pending = 1;
        pthread_cond_signal(&det_cond);
    }
    pthread_mutex_unlock(&det_lock);
}

static void *det_thread(void *arg)
{
    (void)arg;
    for (;;)
    {
        pthread_mutex_lock(&det_lock);
        while (!det_pending && !det_quit)
            pthread_cond_wait(&det_cond, &det_lock);
        int quit = det_quit;
        pthread_mutex_unlock(&det_lock);
        if (quit) break;

        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);

        /* 320x240 gray -> 96x96 box-average -> int8 quantize */
        TfLiteTensor *input = tflm_interp->input(0);
        const float scale = input->params.scale;
        const int zp = input->params.zero_point;
        for (int iy = 0; iy < 96; iy++)
        {
            int sy0 = iy * 240 / 96, sy1 = (iy + 1) * 240 / 96;
            for (int ix = 0; ix < 96; ix++)
            {
                int sx0 = ix * 320 / 96, sx1 = (ix + 1) * 320 / 96;
                unsigned sum = 0, cnt = 0;
                for (int sy = sy0; sy < sy1; sy++)
                    for (int sx = sx0; sx < sx1; sx++)
                    {
                        sum += det_gray[sy * 320 + sx];
                        cnt++;
                    }
                float v = (sum / cnt) / 255.0f;
                int q = (int)(v / scale + zp);
                if (q < -128) q = -128;
                if (q > 127) q = 127;
                input->data.int8[iy * 96 + ix] = (int8_t)q;
            }
        }

        TfLiteStatus st = tflm_interp->Invoke();
        int pct = -1;
        if (st == kTfLiteOk)
        {
            TfLiteTensor *output = tflm_interp->output(0);
            int8_t q = output->data.int8[kPersonIndex];
            pct = (int)((q - output->params.zero_point) *
                        output->params.scale * 100.0f + 0.5f);
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);

        pthread_mutex_lock(&det_lock);
        det_ms = (int)((t1.tv_sec - t0.tv_sec) * 1000 +
                       (t1.tv_nsec - t0.tv_nsec) / 1000000);
        person_pct = pct;
        n_peds = 1;   /* reuse the band counter as "verdict ready" */
        det_pending = 0;
        pthread_mutex_unlock(&det_lock);
    }
    return NULL;
}

/* ---- camera ---- */

struct cam_buf { void *start; size_t len; };
static int cam_fd = -1;
static struct cam_buf *bufs;
static int nbufs;
static int cam_w, cam_h;

static int xioctl(int fd, int req, void *arg)
{
    int r;
    do r = ioctl(fd, req, arg);
    while (r < 0 && errno == EINTR);
    return r;
}

/* map the controller base (from the canonicalized sysfs path) to an
   index - identical to app/uvc_face_detect_... */
static int host_number(const char *sys_path)
{
    if (strstr(sys_path, "ff780000")) return 0; /* rk3506 USB host */
    if (strstr(sys_path, "1c1b400") || strstr(sys_path, "1c1b000")) return 1;
    if (strstr(sys_path, "1c1c400") || strstr(sys_path, "1c1c000")) return 2;
    if (strstr(sys_path, "1c1d400") || strstr(sys_path, "1c1d000")) return 3;
    return -1;
}

static char *find_uvc(int *uh)
{
    DIR *d = opendir("/sys/class/video4linux");
    struct dirent *e;
    char *found = NULL;

    if (!d)
        return NULL;

    while ((e = readdir(d)) != NULL)
    {
        char dev_path[300], link_target[400], sys_path[400];
        int fd;
        struct v4l2_capability cap;

        if (strncmp(e->d_name, "video", 5) != 0)
            continue;

        snprintf(dev_path, sizeof(dev_path), "/dev/%s", e->d_name);
        fd = open(dev_path, O_RDWR);
        if (fd < 0)
            continue;

        memset(&cap, 0, sizeof(cap));
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0 &&
            (cap.capabilities & V4L2_CAP_DEVICE_CAPS
                 ? (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE)
                 : (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) &&
            strcmp((char *)cap.driver, "uvcvideo") == 0)
        {
            /* the device symlink is RELATIVE - canonicalize it so the
               controller base address is visible */
            snprintf(link_target, sizeof(link_target),
                     "/sys/class/video4linux/%s/device", e->d_name);
            {
                char raw[300], joined[600], resolved[4096];

                ssize_t n = readlink(link_target, raw, sizeof(raw) - 1);
                if (n > 0)
                {
                    raw[n] = 0;
                    snprintf(joined, sizeof(joined),
                             "/sys/class/video4linux/%s/%s", e->d_name, raw);
                    if (realpath(joined, resolved))
                        strcpy(sys_path, resolved);
                    else
                        strcpy(sys_path, joined);
                }
                else
                    strcpy(sys_path, "");
            }

            *uh = host_number(sys_path);
            found = strdup(dev_path);
            close(fd);
            break;
        }
        close(fd);
    }
    closedir(d);
    return found;
}

static int cam_open(char *dev, int *w, int *h,
                    struct cam_buf **obufs, int *onbufs, int *fd)
{
    *obufs = NULL;
    *onbufs = 0;
    *fd = -1;
    int cam_fd = open(dev, O_RDWR);
    if (cam_fd < 0) return -1;
    struct cam_buf *bufs = NULL;
    int mapped = 0;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(cam_fd, VIDIOC_G_FMT, &fmt);
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    if (xioctl(cam_fd, VIDIOC_S_FMT, &fmt) != 0)
    {
        perror("uvc: S_FMT");
        quit_req = 1;
        close(cam_fd);
        return -1;
    }
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV ||
        fmt.fmt.pix.width != 640 || fmt.fmt.pix.height != 480 ||
        fmt.fmt.pix.bytesperline != 640 * 2)
    {
        fprintf(stderr, "uvc: detector requires 640x480 YUYV; camera negotiated %ux%u format 0x%08x; exiting\n",
                fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.pixelformat);
        quit_req = 1;
        close(cam_fd);
        return -1;
    }
    *w = fmt.fmt.pix.width;
    *h = fmt.fmt.pix.height;
    printf("uvc: %s at %dx%d YUYV\n", dev, *w, *h);

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(cam_fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 2)
    {
        perror("uvc: REQBUFS");
        close(cam_fd);
        return -1;
    }
    bufs = (struct cam_buf *)calloc(req.count, sizeof(struct cam_buf));
    if (!bufs)
    {
        perror("uvc: buffer allocation");
        close(cam_fd);
        return -1;
    }
    for (int i = 0; i < (int)req.count; i++)
    {
        struct v4l2_buffer b;
        memset(&b, 0, sizeof(b));
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        if (xioctl(cam_fd, VIDIOC_QUERYBUF, &b) < 0)
        {
            perror("uvc: QUERYBUF");
            goto fail;
        }
        void *start = mmap(NULL, b.length, PROT_READ | PROT_WRITE,
                           MAP_SHARED, cam_fd, b.m.offset);
        if (start == MAP_FAILED)
        {
            perror("uvc: mmap");
            goto fail;
        }
        bufs[i].len = b.length;
        bufs[i].start = start;
        mapped++;
    }
    for (int i = 0; i < (int)req.count; i++)
    {
        struct v4l2_buffer b;
        memset(&b, 0, sizeof(b));
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        if (xioctl(cam_fd, VIDIOC_QBUF, &b) < 0)
        {
            perror("uvc: QBUF");
            goto fail;
        }
    }
    if (xioctl(cam_fd, VIDIOC_STREAMON, &type) < 0)
    {
        perror("uvc: STREAMON");
        goto fail;
    }
    *obufs = bufs;
    *onbufs = (int)req.count;
    *fd = cam_fd;
    printf("uvc: streaming on %s\n", dev);
    return 0;

fail:
    for (int i = 0; i < mapped; i++)
        munmap(bufs[i].start, bufs[i].len);
    free(bufs);
    close(cam_fd);
    return -1;
}

static void cam_release(int *fd, struct cam_buf **bufs, int *nbufs)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (*fd >= 0) xioctl(*fd, VIDIOC_STREAMOFF, &type);
    for (int i = 0; i < *nbufs; i++)
        if ((*bufs)[i].start) munmap((*bufs)[i].start, (*bufs)[i].len);
    free(*bufs);
    close(*fd);
    *fd = -1;
    *bufs = NULL;
    *nbufs = 0;
}

static void scan_msleep(void)
{
    struct timespec sl = { 0, 100 * 1000 * 1000 };
    nanosleep(&sl, NULL);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-6") == 0)
            pix18 = 1;
        else if (strcmp(argv[i], "-5") != 0)
        {
            fprintf(stderr, "usage: person_vis [-6 RGB666]\n");
            return 1;
        }
    }

    panel_init();
    if (!panel_ok)
    {
        fprintf(stderr, "panel init failed\n");
        return 1;
    }

    {
        FILE *f = fopen("person_detect.tflite", "rb");
        if (!f) { fprintf(stderr, "person_detect.tflite not found\n"); return 1; }
        size_t n = fread(model_buf, 1, sizeof(model_buf), f);
        fclose(f);
        tflm_model = tflite::GetModel(model_buf);
        if (tflm_model->version() != TFLITE_SCHEMA_VERSION)
        {
            fprintf(stderr, "model schema mismatch\n");
            return 1;
        }
        tflm_resolver = new tflite::MicroMutableOpResolver<5>();
        tflm_resolver->AddAveragePool2D(tflite::Register_AVERAGE_POOL_2D_INT8());
        tflm_resolver->AddConv2D(tflite::Register_CONV_2D_INT8());
        tflm_resolver->AddDepthwiseConv2D(tflite::Register_DEPTHWISE_CONV_2D_INT8());
        tflm_resolver->AddReshape();
        tflm_resolver->AddSoftmax(tflite::Register_SOFTMAX_INT8());
        static tflite::MicroInterpreter interp(tflm_model, *tflm_resolver,
                                               tflm_arena, sizeof(tflm_arena));
        tflm_interp = &interp;
        if (tflm_interp->AllocateTensors() != kTfLiteOk)
        {
            fprintf(stderr, "AllocateTensors failed\n");
            return 1;
        }
        printf("stage: TFLM person-detect loaded (arena %u/%u bytes)\n",
               (unsigned)tflm_interp->arena_used_bytes(),
               (unsigned)sizeof(tflm_arena));
    }

    pthread_t det_tid;
    int thread_err = pthread_create(&det_tid, NULL, det_thread, NULL);
    if (thread_err != 0)
    {
        fprintf(stderr, "detector thread creation failed: %s\n",
                strerror(thread_err));
        return 1;
    }

    char *dev = NULL;
    int uh = -1;
    unsigned frames = 0, fps = 0;
    struct timespec ts;
    double t_end = 0;

    while (!quit_req)
    {
        if (!dev)
        {
            clock_gettime(CLOCK_MONOTONIC, &ts);
            double now = ts.tv_sec + ts.tv_nsec / 1e9;
            static double t_scan = 0;
            if (now - t_scan >= 2.0)
            {
                t_scan = now;
                dev = find_uvc(&uh);
                if (dev)
                    printf("uvc: camera at %s (uh-%d)\n", dev, uh);
                else
                {
                    memset(panel_fb, 0, sizeof(panel_fb));
                    if (pix18) memset(panel_666, 0, sizeof(panel_666));
                    fb_string_t("NO CAM", 60, 120, LCD_WHITE);
                    blit();
                }
            }
            scan_msleep();
            continue;
        }

        if (cam_fd < 0 &&
            cam_open(dev, &cam_w, &cam_h, &bufs, &nbufs, &cam_fd) < 0)
        {
            if (quit_req)
                break;
            fprintf(stderr, "uvc: cam_open failed, rescanning\n");
            free(dev);
            dev = NULL;
            scan_msleep();
            continue;
        }
        static int map_done_w = 0, map_done_h = 0;
        if (map_done_w != cam_w || map_done_h != cam_h)
        {
            build_src_map(cam_w, cam_h);
            map_done_w = cam_w;
            map_done_h = cam_h;
        }

        struct v4l2_buffer b;
        fd_set fds;
        struct timeval tv = { 2, 0 };
        FD_ZERO(&fds);
        FD_SET(cam_fd, &fds);
        if (select(cam_fd + 1, &fds, NULL, NULL, &tv) <= 0)
        {
            fprintf(stderr, "uvc: timeout, rescanning\n");
            cam_release(&cam_fd, &bufs, &nbufs);
            free(dev);
            dev = NULL;
            continue;
        }
        memset(&b, 0, sizeof(b));
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        if (xioctl(cam_fd, VIDIOC_DQBUF, &b) < 0)
        {
            perror("uvc: DQBUF");
            cam_release(&cam_fd, &bufs, &nbufs);
            free(dev);
            dev = NULL;
            continue;
        }

        const uint8_t *yuyv = (const uint8_t *)bufs[b.index].start;

        /* camera -> panel: rotate 90 CW + fit, dual-buffer */
        for (int py = 0; py < PANEL_H; py++)
        {
            uint16_t *dst = &panel_fb[py * PANEL_W];
            uint8_t *d6 = &panel_666[py * PANEL_W * 3];
            const int16_t *mrow = &src_map[py * PANEL_W * 2];
            for (int px = 0; px < PANEL_W; px++)
            {
                int sx = mrow[px * 2], sy = mrow[px * 2 + 1];
                if (sx < 0)
                {
                    dst[px] = 0;
                    if (pix18) d6[px * 3] = d6[px * 3 + 1] = d6[px * 3 + 2] = 0;
                    continue;
                }
                const uint8_t *pair = yuyv + sy * cam_w * 2 + (sx & ~1) * 2;
                int y = sx & 1 ? pair[2] : pair[0];
                int cb = pair[1] - 128, cr = pair[3] - 128;
                int r = (298 * y + 409 * cr + 128) >> 8;
                int g = (298 * y - 100 * cb - 208 * cr + 128) >> 8;
                int bl = (298 * y + 517 * cb + 128) >> 8;
                if (r < 0) r = 0; else if (r > 255) r = 255;
                if (g < 0) g = 0; else if (g > 255) g = 255;
                if (bl < 0) bl = 0; else if (bl > 255) bl = 255;
                dst[px] = (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | bl >> 3);
                d6[px * 3] = (uint8_t)r;
                d6[px * 3 + 1] = (uint8_t)g;
                d6[px * 3 + 2] = (uint8_t)bl;
            }
        }

        det_publish(yuyv, cam_w, cam_h);

        /* boxes + band */
        struct det_box drawn[MAX_PEDS];
        pthread_mutex_lock(&det_lock);
        int nd = n_peds;
        int result_pct = person_pct;
        int result_ms = det_ms;
        for (int i = 0; i < nd; i++)
            drawn[i] = peds[i];
        pthread_mutex_unlock(&det_lock);
        (void)drawn;

        /* verdict text over the live view */
        if (result_pct >= 0)
        {
            char verdict[24];

            if (result_pct >= 60)
                snprintf(verdict, sizeof(verdict), "PERSON %d%%", result_pct);
            else if (result_pct >= 0)
                snprintf(verdict, sizeof(verdict), "no person %d%%",
                         result_pct);
            fb_string_t(verdict, 4, 4, LCD_WHITE);
        }

        char band[40];
        snprintf(band, sizeof(band), "uh-%d %u fps %d%% %ums",
                 uh < 0 ? 0 : uh, fps, result_pct, result_ms);
        fb_string_t(band, 4, PANEL_H - FPS_BAND + 4, LCD_WHITE);
        blit();
        frames++;

        clock_gettime(CLOCK_MONOTONIC, &ts);
        double now = ts.tv_sec + ts.tv_nsec / 1e9;
        if (now - t_end >= 1.0)
        {
            fps = frames;
            frames = 0;
            t_end = now;
            printf("fps: %u  det %d ms  person %d%%\n", fps, result_ms,
                   result_pct);
        }

        xioctl(cam_fd, VIDIOC_QBUF, &b);
    }

    if (cam_fd >= 0)
        cam_release(&cam_fd, &bufs, &nbufs);
    free(src_map);
    pthread_mutex_lock(&det_lock);
    det_quit = 1;
    pthread_cond_signal(&det_cond);
    pthread_mutex_unlock(&det_lock);
    pthread_join(det_tid, NULL);

    memset(panel_fb, 0, sizeof(panel_fb));
    if (pix18) memset(panel_666, 0, sizeof(panel_666));
    fb_string_t("bye", 60, 120, LCD_WHITE);
    blit();
    printf("bye\n");
    return 0;
}
