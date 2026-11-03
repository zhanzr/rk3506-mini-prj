/*
  face_detect - UVC camera face detection with boxes on the ST7365P
  3.5" 320x480 (MD350) SPI panel on the Orange Pi Zero.

  Same capture/panel stack as ../uvc_to_lcd in this group
  (RGB565 default, -6 for RGB666), plus face detection:

  - vendored libfacedetection CNN (BSD, weights embedded in source -
    no training, no runtime deps), ARM NEON enabled
  - 640x480 input is half-sampled to a 320x240 BGR frame and handed
    to a detector pthread, so video keeps streaming at full rate
  - boxes are mapped back through the 90-degree rotation/scale map
    and drawn into both panel buffers (RGB565 + RGB666), 2 px thick
  - the band OSD shows "uh-N <fps> fps <n>f <det>ms"

  - auto-detects the first UVC capture device under /dev/video*
  - shows the USB host controller number ("uh-N", derived from the
    controller base address in the device's sysfs path:
    rk3506-mini: ff780000=uh-0 (single USB host))
  - captures YUYV via V4L2 MMAP streaming, converts to RGB565,
    rotates 90 degrees and scales to the 320x480 panel (letterboxed)
  - fps + "uh-N" shown in the bottom band
  - no camera found: shows "UVC NA" and exits

  Panel transport identical to app/st7365_md350_320x480
  (hw SPI1, mode 0, DC PA1, RES PA6; SPI clock requested at 100 MHz -
  the controller clamps to its ~37.5 MHz ceiling, the practical raster
  rate is ~23 Mbit/s).
*/
#include <dirent.h>
#include <errno.h>
#include <pthread.h>

#include "det_shim.h"
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* stdio.h must precede jpeglib.h (FILE in the API) */
#include <jpeglib.h>
#include <math.h>
#include <linux/videodev2.h>

#include "lcd.h"
#include "ref/lcd_font_1608.h"
#include "spi_hw.h"

#include "log.h"

#define PANEL_W 320
#define PANEL_H 480
#define FPS_BAND 24

static uint16_t panel_fb[PANEL_W * PANEL_H];
static uint8_t panel_666[PANEL_W * PANEL_H * 3]; /* 8-bit comps, shown 6-bit */
static int pix18 = 0;                            /* RGB565 default; -6 for RGB666 */

/* camera capture requires exact 640x480 YUYV or MJPEG */

/* ---- face detection (libfacedetection CNN, weights vendored) ---- */

#define DET_W 320                    /* detector input: half of 640x480 */
#define DET_H 240
#define MAX_FACES 8

static unsigned char det_bgr[DET_W * DET_H * 3];
#define DET_BUF_SIZE 0x9000            /* FACEDETECTION_RESULT_BUFFER_SIZE */
static unsigned char det_buf[DET_BUF_SIZE];

static pthread_mutex_t det_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t det_cond = PTHREAD_COND_INITIALIZER;
static int det_pending = 0;          /* det_bgr holds an undetected frame */
static int det_ms = 0;
static int det_quit = 0;

struct det_box
{
    int x, y, w, h, conf;
};

static struct det_box faces[MAX_FACES];
static int n_faces = 0;

/* camera -> panel mapping, filled by build_src_map() */
static double map_s, map_offx, map_offy;
static int map_rw;

/* build the half-res BGR sample and hand it to the detector thread;
   no-op while the previous sample is still being detected */
static void det_publish(const uint8_t *src, int cw, int ch, int is_yuyv)
{
    if (cw < DET_W * 2 || ch < DET_H * 2)
        return;

    pthread_mutex_lock(&det_lock);
    if (!det_pending)
    {
        for (int dy = 0; dy < DET_H; dy++)
        {
            const uint8_t *row = src + (dy * 2) * cw * (is_yuyv ? 2 : 3);
            uint8_t *out = det_bgr + dy * DET_W * 3;

            for (int dx = 0; dx < DET_W; dx++)
            {
                int y, cb, cr;

                if (is_yuyv)
                {
                    const uint8_t *q = row + (dx * 2) * 2;
                    y = q[0] - 16; cb = q[1] - 128; cr = q[3] - 128;
                }
                else
                {
                    const uint8_t *q = row + (dx * 2) * 3;
                    y = q[0] - 16; cb = q[1] - 128; cr = q[2] - 128;
                }
                int r = (298 * y + 409 * cr + 128) >> 8;
                int g = (298 * y - 100 * cb - 208 * cr + 128) >> 8;
                int b = (298 * y + 517 * cb + 128) >> 8;

                out[dx * 3]     = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
                out[dx * 3 + 1] = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g);
                out[dx * 3 + 2] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
            }
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
        if (quit)
            break;

        int boxes[MAX_FACES * 5];
        struct timespec t0, t1;

        clock_gettime(CLOCK_MONOTONIC, &t0);
        int n = det_run(det_buf, det_bgr, DET_W, DET_H, DET_W * 3, boxes);
        clock_gettime(CLOCK_MONOTONIC, &t1);

        pthread_mutex_lock(&det_lock);
        det_ms = (int)((t1.tv_sec - t0.tv_sec) * 1000 +
                       (t1.tv_nsec - t0.tv_nsec) / 1000000);
        n_faces = n;
        for (int i = 0; i < n_faces; i++)
        {
            faces[i].conf = boxes[i * 5 + 0];
            faces[i].x = boxes[i * 5 + 1];
            faces[i].y = boxes[i * 5 + 2];
            faces[i].w = boxes[i * 5 + 3];
            faces[i].h = boxes[i * 5 + 4];
        }
        det_pending = 0;
        pthread_mutex_unlock(&det_lock);
    }
    return NULL;
}

/* rectangle in CAMERA coordinates -> 2 px thick frame on the panel
   (both buffers; map is a 90 deg rotation + scale + letterbox) */
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
    if (px2 < px1 || py2 < py1)
        return;

    for (int t = 0; t < 2; t++)
    {
        int xa = px1 + t, xb = px2 - t, ya = py1 + t, yb = py2 - t;

        if (xa > xb || ya > yb)
            break;
        for (int px = xa; px <= xb; px++)
        {
            panel_fb[ya * PANEL_W + px] = 0x07E0;
            panel_fb[yb * PANEL_W + px] = 0x07E0;
            if (pix18)
            {
                uint8_t *d = &panel_666[(ya * PANEL_W + px) * 3];

                d[0] = 0; d[1] = 0xFF; d[2] = 0;
                d = &panel_666[(yb * PANEL_W + px) * 3];
                d[0] = 0; d[1] = 0xFF; d[2] = 0;
            }
        }
        for (int py = ya; py <= yb; py++)
        {
            panel_fb[py * PANEL_W + xa] = 0x07E0;
            panel_fb[py * PANEL_W + xb] = 0x07E0;
            if (pix18)
            {
                uint8_t *d = &panel_666[(py * PANEL_W + xa) * 3];

                d[0] = 0; d[1] = 0xFF; d[2] = 0;
                d = &panel_666[(py * PANEL_W + xb) * 3];
                d[0] = 0; d[1] = 0xFF; d[2] = 0;
            }
        }
    }
}

/* ---- OSD text rendering straight into the panel framebuffer ---- */

/* transparent variant: only fg pixels are written - video shows through */
static void fb_string_t(const char *s, int x, int y, uint16_t fg)
{
    while (*s)
    {
        uint8_t c = *s;

        if (c < 0x20 || c > 0x7E)
            c = '?';
        int idx = c - 0x20;

        for (int row = 0; row < 16; row++)
        {
            uint8_t bits = asc2_1608[idx * 16 + row];
            int py = y + row;

            if (py < 0 || py >= PANEL_H)
                continue;
            for (int col = 0; col < 8; col++)
            {
                int px = x + col;

                if (px < 0 || px >= PANEL_W)
                    continue;
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

static volatile sig_atomic_t quit_req;

static void on_signal(int sig)
{
    (void)sig;
    quit_req = 1;
}

/* ---------------- camera discovery ---------------- */

/* map the controller base (from the sysfs path) to a host index */
static int host_number(const char *sys_path)
{
    if (strstr(sys_path, "ff780000")) return 0; /* rk3506 USB host */
    if (strstr(sys_path, "1c1b400") || strstr(sys_path, "1c1b000")) return 1;
    if (strstr(sys_path, "1c1c400") || strstr(sys_path, "1c1c000")) return 2;
    if (strstr(sys_path, "1c1d400") || strstr(sys_path, "1c1d000")) return 3;
    return -1;
}

/* returns malloc'd /dev/videoN path + sets *uh to the host number,
   NULL if no UVC capture device exists */
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
            (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) &&
            (cap.capabilities & V4L2_CAP_DEVICE_CAPS ?
             (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE) : 1) &&
            strcmp((char *)cap.driver, "uvcvideo") == 0)
        {
        snprintf(link_target, sizeof(link_target),
                 "/sys/class/video4linux/%s/device", e->d_name);
        {
            /* the raw symlink target is relative to the videoN/ dir -
               canonicalize so the controller base address is visible */
            char raw[300], joined[600], resolved[4096];

            ssize_t n = readlink(link_target, raw, sizeof(raw) - 1);
            if (n > 0)
            {
                raw[n] = '\0';
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

/* ---------------- V4L2 helpers ---------------- */

static int xioctl(int fd, unsigned long req, void *arg)
{
    int r;

    do
        r = ioctl(fd, req, arg);
    while (r < 0 && errno == EINTR);
    return r;
}

struct cam_buf {
    void *start;
    size_t len;
};

/* Prefer exact 640x480 modes, with YUYV ahead of MJPEG. */
static int pick_format(int fd, uint32_t *pixelformat, int *w, int *h)
{
    struct v4l2_fmtdesc f;
    long best_score = -1;
    uint32_t best_fmt = 0;
    int best_w = 0, best_h = 0;

    memset(&f, 0, sizeof(f));
    f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    while (xioctl(fd, VIDIOC_ENUM_FMT, &f) == 0)
    {
        int is_yuyv = f.pixelformat == V4L2_PIX_FMT_YUYV;
        int is_mjpeg = f.pixelformat == V4L2_PIX_FMT_MJPEG;
        struct v4l2_frmsizeenum fs;

        if (is_yuyv || is_mjpeg)
        {
            struct v4l2_format try_fmt;
            memset(&try_fmt, 0, sizeof(try_fmt));
            try_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            try_fmt.fmt.pix.width = 640;
            try_fmt.fmt.pix.height = 480;
            try_fmt.fmt.pix.pixelformat = f.pixelformat;
            try_fmt.fmt.pix.field = V4L2_FIELD_ANY;
            if (xioctl(fd, VIDIOC_TRY_FMT, &try_fmt) == 0 &&
                try_fmt.fmt.pix.width == 640 &&
                try_fmt.fmt.pix.height == 480 &&
                try_fmt.fmt.pix.pixelformat == f.pixelformat)
            {
                long score = 0x50000000L +
                             (is_yuyv ? 0x10000000L : 0) + 640L * 480;
                if (score > best_score)
                {
                    best_score = score;
                    best_fmt = f.pixelformat;
                    best_w = 640;
                    best_h = 480;
                }
            }

            memset(&fs, 0, sizeof(fs));
            fs.pixel_format = f.pixelformat;
            while (xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fs) == 0)
            {
                if (fs.type == V4L2_FRMSIZE_TYPE_DISCRETE)
                {
                    long area = (long)fs.discrete.width *
                                fs.discrete.height;
                    int exact = fs.discrete.width == 640 &&
                                fs.discrete.height == 480;
                    long score = (exact ? 0x60000000L :
                                  area <= 640L * 480 ? 0x40000000L : 0) +
                                 (is_yuyv ? 0x10000000L : 0) + area;

                    if (score > best_score)
                    {
                        best_score = score;
                        best_fmt = f.pixelformat;
                        best_w = fs.discrete.width;
                        best_h = fs.discrete.height;
                    }
                }
                fs.index++;
            }
        }
        f.index++;
    }
    if (best_score < 0)
        return -1;
    *pixelformat = best_fmt;
    *w = best_w;
    *h = best_h;
    return 0;
}

static int cam_open(const char *dev, int *mjpeg, int *w, int *h,
                    struct cam_buf **bufs_out, int *nbufs_out, int *cam_fd)
{
    int fd = open(dev, O_RDWR);
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    uint32_t pixelformat;
    int cw, ch;
    struct cam_buf *bufs = NULL;
    int mapped = 0;

    if (fd < 0)
        return -1;
    *bufs_out = NULL;
    *nbufs_out = 0;
    *cam_fd = -1;
    if (w) *w = 0;
    if (h) *h = 0;
    if (mjpeg) *mjpeg = 0;

    if (pick_format(fd, &pixelformat, &cw, &ch) < 0)
    {
        log_printf(LOG_LVL_ERROR, "uvc: no YUYV/MJPEG format on the camera");
        quit_req = 1;
        close(fd);
        return -1;
    }
    if (cw != 640 || ch != 480)
    {
        log_printf(LOG_LVL_ERROR, "uvc: detector requires 640x480; camera offers %dx%d; exiting",
                cw, ch);
        quit_req = 1;
        close(fd);
        return -1;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = cw;
    fmt.fmt.pix.height = ch;
    fmt.fmt.pix.pixelformat = pixelformat;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0 ||
        fmt.fmt.pix.pixelformat != pixelformat ||
        fmt.fmt.pix.width != 640 || fmt.fmt.pix.height != 480 ||
        (pixelformat == V4L2_PIX_FMT_YUYV &&
         fmt.fmt.pix.bytesperline != 640 * 2))
    {
        log_printf(LOG_LVL_ERROR, "uvc: camera cannot provide the required 640x480 format/stride; exiting");
        quit_req = 1;
        close(fd);
        return -1;
    }
    if (mjpeg) *mjpeg = pixelformat == V4L2_PIX_FMT_MJPEG;
    if (w) *w = fmt.fmt.pix.width;
    if (h) *h = fmt.fmt.pix.height;
    log_printf(LOG_LVL_INFO, "uvc: %s at %dx%d %s", dev, fmt.fmt.pix.width,
           fmt.fmt.pix.height,
           pixelformat == V4L2_PIX_FMT_MJPEG ? "MJPEG" : "YUYV");

    memset(&req, 0, sizeof(req));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 2)
    {
        perror("uvc: REQBUFS");
        close(fd);
        return -1;
    }

    bufs = calloc(req.count, sizeof(*bufs));
    if (!bufs)
    {
        perror("uvc: buffer allocation");
        close(fd);
        return -1;
    }
    for (int i = 0; i < (int)req.count; i++)
    {
        struct v4l2_buffer b;

        memset(&b, 0, sizeof(b));
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        if (xioctl(fd, VIDIOC_QUERYBUF, &b) < 0)
        {
            perror("uvc: QUERYBUF");
            goto fail;
        }
        void *start = mmap(NULL, b.length, PROT_READ | PROT_WRITE,
                           MAP_SHARED, fd, b.m.offset);
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
        if (xioctl(fd, VIDIOC_QBUF, &b) < 0)
        {
            perror("uvc: QBUF");
            goto fail;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0)
    {
        perror("uvc: STREAMON");
        goto fail;
    }
    log_printf(LOG_LVL_INFO, "uvc: streaming on %s", dev);

    *bufs_out = bufs;
    *nbufs_out = (int)req.count;
    *cam_fd = fd;
    return 0;

fail:
    for (int i = 0; i < mapped; i++)
        munmap(bufs[i].start, bufs[i].len);
    free(bufs);
    close(fd);
    return -1;
}

/* ---------------- MJPEG decode + color conversion ---------------- */

/* MJPEG decode with a non-fatal error handler: corrupt/truncated frames
   (common on cheap cams) are skipped instead of killing the process,
   and warnings stay silent. */

struct my_jerr {
    struct jpeg_error_mgr pub;
    jmp_buf jb;
};

static void my_error_exit(j_common_ptr ci)
{
    struct my_jerr *e = (struct my_jerr *)ci->err;

    longjmp(e->jb, 1);
}

static void my_output_message(j_common_ptr ci)
{
    (void)ci;   /* silence libjpeg warnings (truncated frames etc.) */
}

/* BT.601 limited-range (Y 16-235, Cb/Cr 128±112 - what UVC cameras
   actually encode) -> RGB565. Using libjpeg's full-range conversion on
   this data washes the image out (lifted blacks, compressed whites).
   A gamma LUT + chroma saturation gain compensate the panel response
   (tunable via -G/-S; -R selects the source range). */
static int cam_range_limited = 1;  /* 1 = Y 16-235, 0 = full range */
static int sat_pct = 100;          /* chroma gain, % (125+ counters wash; greenish at 125 - revisit) */
static int gamma_pct = 100;        /* luma exponent, %: 100 = off (125+ darkens but went greenish) */
static uint8_t gamma_lut[256];

static void build_gamma_lut(void)
{
    for (int v = 0; v < 256; v++)
    {
        double o = pow(v / 255.0, (double)gamma_pct / 100.0) * 255.0 + 0.5;
        int oi = (int)o;

        gamma_lut[v] = oi < 0 ? 0 : oi > 255 ? 255 : (uint8_t)oi;
    }
}

static inline void ycc_to_rgb888(int Yv, int Cbv, int Crv,
                                 int *ro, int *go, int *bo)
{
    int y = gamma_lut[Yv];
    int cb = (Cbv - 128) * sat_pct / 100;   /* centered! */
    int cr = (Crv - 128) * sat_pct / 100;

    if (cam_range_limited)
        y -= 16;
    int r = (298 * y + 409 * cr + 128) >> 8;
    int g = (298 * y - 100 * cb - 208 * cr + 128) >> 8;
    int b = (298 * y + 517 * cb + 128) >> 8;

    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    *ro = r; *go = g; *bo = b;
}

static inline uint16_t ycc_to_rgb565(int Yv, int Cbv, int Crv)
{
    int r, g, b;

    ycc_to_rgb888(Yv, Cbv, Crv, &r, &g, &b);
    return (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
}
/* decode one MJPEG buffer to malloc'd YCbCr 3BPP (caller frees);
   returns NULL on decode failure */
static uint8_t *decode_mjpeg(const uint8_t *in, size_t len, int *w, int *h)
{
    struct jpeg_decompress_struct cinfo;
    struct my_jerr jerr;
    uint8_t *ycbcr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    jerr.pub.output_message = my_output_message;
    if (setjmp(jerr.jb))
    {
        jpeg_destroy_decompress(&cinfo);
        return NULL;
    }
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (unsigned char *)in, len);
    jpeg_read_header(&cinfo, TRUE);
    /* keep the raw YCbCr: the camera encodes BT.601 limited-range, the
       in-library RGB conversion would wash the image out */
    cinfo.out_color_space = JCS_YCbCr;
    jpeg_start_decompress(&cinfo);

    *w = cinfo.output_width;
    *h = cinfo.output_height;
    ycbcr = malloc((size_t)*w * *h * 3);
    if (!ycbcr)
    {
        jpeg_destroy_decompress(&cinfo);
        return NULL;
    }
    while (cinfo.output_scanline < (unsigned)*h)
    {
        JSAMPLE *rowp = (JSAMPLE *)(ycbcr + cinfo.output_scanline * *w * 3);

        jpeg_read_scanlines(&cinfo, &rowp, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return ycbcr;
}

/* panel framebuffer + precomputed rotation/scale map */
static int16_t *src_map;   /* per panel pixel: [sx_cam, sy_cam] */

static void build_src_map(int cw, int ch)
{
    /* camera (cw x ch) rotated 90 deg CW -> (ch x cw), fitted into the
       panel with letterbox; 1:1 when the camera is 320x240 */
    double rw = ch, rh = cw;
    double s = rw > 0 && rh > 0 ?
               (PANEL_W / rw < PANEL_H / rh ? PANEL_W / rw : PANEL_H / rh) : 1;
    double offx = (PANEL_W - rw * s) / 2;
    double offy = (PANEL_H - rh * s) / 2;

    src_map = malloc(2 * sizeof(int16_t) * PANEL_W * PANEL_H);
    for (int py = 0; py < PANEL_H; py++)
        for (int px = 0; px < PANEL_W; px++)
        {
            int sx_rot = (int)((px - offx) / s);
            int sy_rot = (int)((py - offy) / s);
            int sx_cam = sy_rot;          /* 90 deg CW */
            int sy_cam = (int)rw - 1 - sx_rot;

            if (sx_rot < 0 || sx_rot >= (int)rw ||
                sy_rot < 0 || sy_rot >= (int)rh ||
                sx_cam < 0 || sx_cam >= cw ||
                sy_cam < 0 || sy_cam >= ch)
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
    map_s = s;
    map_offx = offx;
    map_offy = offy;
    map_rw = (int)rw;
    log_printf(LOG_LVL_DEBUG, "uvc: map built (%dx%d -> %dx%d, scale %.3f)",
           cw, ch, PANEL_W, PANEL_H, s);
}

static void convert_frame(const uint8_t *ycbcr, int cw)
{
    for (int py = 0; py < PANEL_H; py++)
    {
        uint16_t *dst = &panel_fb[py * PANEL_W];
        const int16_t *mrow = &src_map[py * PANEL_W * 2];

        uint8_t *d6 = &panel_666[py * PANEL_W * 3];

        for (int px = 0; px < PANEL_W; px++)
        {
            int sx = mrow[px * 2], sy = mrow[px * 2 + 1];

            if (sx < 0)
            {
                dst[px] = 0;
                if (pix18) d6[px * 3] = d6[px * 3 + 1] = d6[px * 3 + 2] = 0;
                continue;
            }
            const uint8_t *p = ycbcr + (sy * cw + sx) * 3;
            if (pix18)
            {
                int r, g, b;

                ycc_to_rgb888(p[0], p[1], p[2], &r, &g, &b);
                dst[px] = (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
                d6[px * 3] = (uint8_t)r;
                d6[px * 3 + 1] = (uint8_t)g;
                d6[px * 3 + 2] = (uint8_t)b;
            }
            else
                dst[px] = ycc_to_rgb565(p[0], p[1], p[2]);
        }
    }
}

/* raw YUYV cameras: same BT.601 limited-range math, no decode */
static void convert_yuyv_frame(const uint8_t *yuyv, int cw)
{
    for (int py = 0; py < PANEL_H; py++)
    {
        uint16_t *dst = &panel_fb[py * PANEL_W];
        const int16_t *mrow = &src_map[py * PANEL_W * 2];

        uint8_t *d6 = &panel_666[py * PANEL_W * 3];

        for (int px = 0; px < PANEL_W; px++)
        {
            int sx = mrow[px * 2], sy = mrow[px * 2 + 1];

            if (sx < 0)
            {
                dst[px] = 0;
                if (pix18) d6[px * 3] = d6[px * 3 + 1] = d6[px * 3 + 2] = 0;
                continue;
            }
            const uint8_t *pair = yuyv + sy * cw * 2 + (sx & ~1) * 2;
            if (pix18)
            {
                int r, g, b;

                ycc_to_rgb888(sx & 1 ? pair[2] : pair[0],
                              pair[1], pair[3], &r, &g, &b);
                dst[px] = (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
                d6[px * 3] = (uint8_t)r;
                d6[px * 3 + 1] = (uint8_t)g;
                d6[px * 3 + 2] = (uint8_t)b;
            }
            else
                dst[px] = ycc_to_rgb565(sx & 1 ? pair[2] : pair[0],
                                        pair[1], pair[3]);
        }
    }
}

static void blit(void)
{
    /* spi_write_buf holds DC high and chunks at spidev's 4096-byte limit */
    LCD_SetAddress(0, 0, PANEL_W - 1, PANEL_H - 1);
    if (!pix18)
    {
        spi_write_pixels16(panel_fb, PANEL_W * PANEL_H);
        return;
    }
    /* COLMOD 0x06: 3 bytes/pixel, each
       component in D7..D2, D1..D0 don't care */
    static uint8_t row18[PANEL_W * 3];

    for (int py = 0; py < PANEL_H; py++)
    {
        const uint8_t *src = &panel_666[py * PANEL_W * 3];

        for (int px = 0; px < PANEL_W * 3; px++)
            row18[px] = src[px] & 0xFC;
        spi_write_buf(row18, sizeof(row18));
    }
}

/* text-only screen (scan state / bye) - COLMOD-agnostic via blit() */
static void draw_text_screen(const char *s)
{
    memset(panel_fb, 0, sizeof(panel_fb));
    if (pix18)
        memset(panel_666, 0, sizeof(panel_666));
    fb_string_t(s, 60, 120, LCD_WHITE);
    blit();
}

/* ---------------- main ---------------- */

/* release an open camera (STREAMOFF + munmap + close) */
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

static void msleep_slice(void)   /* 100 ms, Ctrl-C stays snappy */
{
    struct timespec sl = { 0, 100 * 1000 * 1000 };

    nanosleep(&sl, NULL);
}

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

int main(int argc, char **argv)
{
    log_init("face_detect");
    log_config_from_argv(argc, argv);
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    for (int i = 1; i < argc; i++)
    {
        int arg_err = 0;

        if (argv[i][0] == '-' && argv[i][1] && !argv[i][2])
        {
            switch (argv[i][1])
            {
            case 'G':
                if (i + 1 < argc)
                    gamma_pct = atoi(argv[++i]);
                else
                    arg_err = 1;
                break;
            case 'S':
                if (i + 1 < argc)
                    sat_pct = atoi(argv[++i]);
                else
                    arg_err = 1;
                break;
            case 'R':
                if (i + 1 < argc && strcmp(argv[++i], "full") == 0)
                {
                    cam_range_limited = 0;
                }
                break;
            case '5':
                pix18 = 0;
                break;
            case '6':
                pix18 = 1;
                break;
            default: arg_err = 1;
            }
        }
        else
            arg_err = 1;

        if (arg_err)
        {
            log_printf(LOG_LVL_INFO, "usage: face_detect [-G gamma_pct] [-S sat_pct] "
                   "[-R full|lim] [-5 RGB565] [-6 RGB666]\n");
            return 1;
        }
    }
    build_gamma_lut();

    log_printf(LOG_LVL_INFO, "stage: transport");
    spi_res_open(49);   /* JP4.29 = GPIO1_C1 */
    spi_dc_open(47);    /* JP4.33 = GPIO1_B7 */
    backlight_init();
    backlight_set(55);
    if (spi_open(100 * 1000 * 1000) != 0)
        return 1;
    log_printf(LOG_LVL_INFO, "stage: LCD_Init");
    LCD_Init();
    LCD_SetAsciiFont(&ASCII_Font16);
    LCD_SetPix18(pix18);
    log_printf(LOG_LVL_INFO, "stage: COLMOD %s", pix18 ? "18-bit (RGB666)" :
                                          "16-bit (RGB565)");
    pthread_t det_tid;

    int thread_err = pthread_create(&det_tid, NULL, det_thread, NULL);
    if (thread_err != 0)
    {
        log_printf(LOG_LVL_ERROR, "detector thread creation failed: %s",
                strerror(thread_err));
        return 1;
    }
    log_printf(LOG_LVL_INFO, "stage: face detector (libfacedetection CNN, NEON)");

    /* camera lifecycle: absent -> rescan every 2 s ("UVC NA" on screen);
       present -> stream; gone mid-stream -> release + rescan again */
    char *dev = NULL;
    int uh = -1, cam_fd = -1, cam_mjpeg = 0, cam_w = 0, cam_h = 0, nbufs = 0;
    struct cam_buf *bufs = NULL;
    uint8_t *ycbcr = NULL;
    int rgb_w = 0, rgb_h = 0;
    char band[40] = "uh-?";
    unsigned frames = 0, fps = 0, timeouts = 0, na_shown = 0;
    struct timespec ts;
    double t_end = 0, t_scan = 0;

    while (!quit_req)
    {
        if (!dev)
        {
            clock_gettime(CLOCK_MONOTONIC, &ts);
            double now = ts.tv_sec + ts.tv_nsec / 1e9;

            if (now - t_scan >= 2.0)
            {
                t_scan = now;
                dev = find_uvc(&uh);
                if (dev)
                {
                    log_printf(LOG_LVL_INFO, "uvc: camera at %s (uh-%d)", dev, uh);
                    snprintf(band, sizeof(band), "uh-%d", uh < 0 ? 0 : uh);
                    na_shown = 0;
                }
                else if (!na_shown)
                {
                    log_printf(LOG_LVL_INFO, "uvc: no UVC camera found");
                    draw_text_screen("UVC NA");
                    na_shown = 1;
                }
            }
            msleep_slice();
            continue;
        }

        if (cam_fd < 0)
        {
            if (cam_open(dev, &cam_mjpeg, &cam_w, &cam_h, &bufs, &nbufs,
                         &cam_fd) < 0)
            {
                if (quit_req)
                    break;
                log_printf(LOG_LVL_ERROR, "uvc: cam_open failed, rescanning");
                free(dev);
                dev = NULL;
                rgb_w = rgb_h = 0;
                t_scan = 0;
                msleep_slice();
                continue;
            }
        }

        struct v4l2_buffer b;
        fd_set fds;
        struct timeval tv = { 2, 0 };

        FD_ZERO(&fds);
        FD_SET(cam_fd, &fds);
        if (select(cam_fd + 1, &fds, NULL, NULL, &tv) <= 0)
        {
            if (++timeouts >= 2)
            {
                log_printf(LOG_LVL_ERROR, "uvc: no frames, releasing camera");
                cam_release(&cam_fd, &bufs, &nbufs);
                free(dev);
                dev = NULL;
                timeouts = 0;
                t_scan = 0;
                draw_text_screen("UVC NA");
            }
            continue;
        }
        timeouts = 0;

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

        if (cam_mjpeg)
        {
            int w, h;
            uint8_t *new_ycbcr = decode_mjpeg(bufs[b.index].start,
                                            b.bytesused, &w, &h);
            xioctl(cam_fd, VIDIOC_QBUF, &b);

            if (new_ycbcr)
            {
                free(ycbcr);
                ycbcr = new_ycbcr;
                if (w != rgb_w || h != rgb_h)
                {
                    rgb_w = w;
                    rgb_h = h;
                    build_src_map(rgb_w, rgb_h);
                }
                convert_frame(ycbcr, rgb_w);
                det_publish(ycbcr, rgb_w, rgb_h, 0);
            }
            else
            {
                msleep_slice();
                continue;
            }
        }
        else
        {
            if (rgb_w != cam_w || rgb_h != cam_h)
            {
                rgb_w = cam_w;
                rgb_h = cam_h;
                build_src_map(rgb_w, rgb_h);
            }
            convert_yuyv_frame(bufs[b.index].start, cam_w);
            det_publish(bufs[b.index].start, cam_w, cam_h, 1);
            xioctl(cam_fd, VIDIOC_QBUF, &b);
        }

        /* face boxes: copy under lock, draw after the frame convert
           (detector works on a half-scale image -> coords x2 for camera) */
        struct det_box drawn[MAX_FACES];

        pthread_mutex_lock(&det_lock);
        int nd = n_faces;
        int det_snapshot_ms = det_ms;
        for (int i = 0; i < nd; i++)
            drawn[i] = faces[i];
        pthread_mutex_unlock(&det_lock);
        for (int i = 0; i < nd; i++)
            fb_rect_cam(drawn[i].x * 2, drawn[i].y * 2,
                        drawn[i].w * 2, drawn[i].h * 2);

        /* OSD: transparent strings over the live frame, every frame */
        fb_string_t(band, 4, PANEL_H - FPS_BAND + 4, LCD_WHITE);
        blit();
        frames++;

        clock_gettime(CLOCK_MONOTONIC, &ts);
        double now = ts.tv_sec + ts.tv_nsec / 1e9;
        if (now - t_end >= 1.0)
        {
            /* displayed-data average R/G/B - color-balance diagnostic */
            unsigned long rs = 0, gs = 0, bs = 0;

            if (pix18)
            {
                for (int i = 0; i < PANEL_W * PANEL_H; i++)
                {
                    rs += panel_666[i * 3] >> 2;        /* as displayed: 6-bit */
                    gs += panel_666[i * 3 + 1] >> 2;
                    bs += panel_666[i * 3 + 2] >> 2;
                }
            }
            else
                for (int i = 0; i < PANEL_W * PANEL_H; i++)
                {
                    uint16_t v = panel_fb[i];

                    rs += (v >> 11) & 0x1F;
                    gs += (v >> 5) & 0x3F;
                    bs += v & 0x1F;
                }
            unsigned npix = PANEL_W * PANEL_H;

            fps = frames;
            frames = 0;
            t_end = now;
            snprintf(band, sizeof(band), "uh-%d %u fps %df %ums",
                     uh < 0 ? 0 : uh, fps, nd, det_snapshot_ms);
            log_printf(LOG_LVL_DEBUG, "fps: %u  det %d ms  faces %d  fb avg R=%lu G=%lu B=%lu (%s)",
                   fps, det_snapshot_ms, nd, rs / npix, gs / npix, bs / npix,
                   pix18 ? "6-bit/6-bit/6-bit" : "5-bit/6-bit/5-bit");
        }
    }

    /* cleanup */
    if (cam_fd >= 0)
        cam_release(&cam_fd, &bufs, &nbufs);
    free(ycbcr);
    free(src_map);
    free(dev);

    pthread_mutex_lock(&det_lock);
    det_quit = 1;
    pthread_cond_signal(&det_cond);
    pthread_mutex_unlock(&det_lock);
    pthread_join(det_tid, NULL);

    draw_text_screen("bye");
    log_printf(LOG_LVL_INFO, "bye");
    return 0;
}
