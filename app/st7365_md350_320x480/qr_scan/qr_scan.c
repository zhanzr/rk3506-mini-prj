/*
  qr_scan - UVC camera QR/barcode reader on the ST7365P 3.5" 320x480 (MD350) SPI panel on the Orange Pi Zero, using zbar (LGPL-2.1,
  board package libzbar-dev; built natively on the board).

  Same capture/panel stack as the other camera apps in this group (RGB565 by default;
  pass -6 for RGB666). The luma plane is handed straight to zbar as Y800 every
  frame (a scan takes single-digit ms); on a decode the text is drawn
  over the live view and printed to the log.

  Build: on the board - `make` (native gcc, needs libzbar-dev);
         cross - `make CROSS=arm-linux-gnueabihf-` (needs an armhf
         libzbar in a sysroot, not the usual flow for this app).
*/
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <zbar.h>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "lcd.h"
#include "spi_hw.h"
#include "lcd_fonts.h"
#include "lcd_font_1608.h"

#define PANEL_W 320
#define PANEL_H 480
#define FPS_BAND 24

static uint16_t panel_fb[PANEL_W * PANEL_H];
static uint8_t panel_666[PANEL_W * PANEL_H * 3]; /* 8-bit comps, shown 6-bit */
static int pix18 = 0;                            /* RGB565 default; -6 for RGB666 */

static volatile sig_atomic_t quit_req = 0;
static void on_signal(int sig) { (void)sig; quit_req = 1; }

/* ---- OSD text rendering straight into the panel framebuffer ---- */

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

/* panel framebuffer + precomputed rotation/scale map */
static int16_t *src_map;   /* per panel pixel: [sx_cam, sy_cam] */

static void build_src_map(int cw, int ch)
{
    double rw = ch, rh = cw;
    double s = rw > 0 && rh > 0 ?
               (PANEL_W / rw < PANEL_H / rh ? PANEL_W / rw : PANEL_H / rh) : 1;
    double offx = (PANEL_W - rw * s) / 2;
    double offy = (PANEL_H - rh * s) / 2;

    free(src_map);
    src_map = malloc(2 * sizeof(int16_t) * PANEL_W * PANEL_H);
    for (int py = 0; py < PANEL_H; py++)
        for (int px = 0; px < PANEL_W; px++)
        {
            int sx_rot = (int)((px - offx) / s);
            int sy_rot = (int)((py - offy) / s);
            int sx_cam = sy_rot;          /* 90 deg CW */
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
    printf("uvc: map built (%dx%d -> %dx%d, scale %.3f)\n",
           cw, ch, PANEL_W, PANEL_H, s);
}

static inline uint16_t ycc_to_rgb565(int Yv, int Cbv, int Crv)
{
    int y = Yv, cb = Cbv - 128, cr = Crv - 128;
    int r = (298 * y + 409 * cr + 128) >> 8;
    int g = (298 * y - 100 * cb - 208 * cr + 128) >> 8;
    int b = (298 * y + 517 * cb + 128) >> 8;

    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    return (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
}

static inline void ycc_to_rgb888(int Yv, int Cbv, int Crv,
                                 int *ro, int *go, int *bo)
{
    int y = Yv, cb = Cbv - 128, cr = Crv - 128;
    int r = (298 * y + 409 * cr + 128) >> 8;
    int g = (298 * y - 100 * cb - 208 * cr + 128) >> 8;
    int b = (298 * y + 517 * cb + 128) >> 8;

    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    *ro = r; *go = g; *bo = b;
}

static void convert_yuyv_frame(const uint8_t *yuyv, int cw)
{
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
            const uint8_t *pair = yuyv + sy * cw * 2 + (sx & ~1) * 2;
            int yv = sx & 1 ? pair[2] : pair[0];

            if (pix18)
            {
                int r, g, b;

                ycc_to_rgb888(yv, pair[1], pair[3], &r, &g, &b);
                dst[px] = (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
                d6[px * 3] = (uint8_t)r;
                d6[px * 3 + 1] = (uint8_t)g;
                d6[px * 3 + 2] = (uint8_t)b;
            }
            else
                dst[px] = ycc_to_rgb565(yv, pair[1], pair[3]);
        }
    }
}

static void blit(void)
{
    LCD_SetAddress(0, 0, PANEL_W - 1, PANEL_H - 1);
    if (!pix18)
    {
        spi_write_pixels16(panel_fb, PANEL_W * PANEL_H);
        return;
    }
    /* COLMOD 0x06: 3 bytes/pixel, component in D7..D2 (datasheet 8.8.43).
       Our display path carries full 8-bit comps; top 6 bits go on the wire */
    static uint8_t row18[PANEL_W * 3];

    for (int py = 0; py < PANEL_H; py++)
    {
        const uint8_t *src = &panel_666[py * PANEL_W * 3];

        for (int px = 0; px < PANEL_W * 3; px++)
            row18[px] = src[px] & 0xFC;
        spi_write_buf(row18, sizeof(row18));
    }
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
               controller base address is visible, like face_detect */
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
    *fd = open(dev, O_RDWR);
    if (*fd < 0) return -1;
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(*fd, VIDIOC_G_FMT, &fmt);
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    if (xioctl(*fd, VIDIOC_S_FMT, &fmt) != 0) { close(*fd); return -1; }
    *w = fmt.fmt.pix.width;
    *h = fmt.fmt.pix.height;
    printf("uvc: %s at %dx%d YUYV\n", dev, *w, *h);

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(*fd, VIDIOC_REQBUFS, &req) < 0) { close(*fd); return -1; }
    *obufs = calloc(req.count, sizeof(struct cam_buf));
    for (int i = 0; i < (int)req.count; i++)
    {
        struct v4l2_buffer b;
        memset(&b, 0, sizeof(b));
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        xioctl(*fd, VIDIOC_QUERYBUF, &b);
        (*obufs)[i].len = b.length;
        (*obufs)[i].start = mmap(NULL, b.length, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, *fd, b.m.offset);
    }
    *onbufs = req.count;
    for (int i = 0; i < *onbufs; i++)
    {
        struct v4l2_buffer b;
        memset(&b, 0, sizeof(b));
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        xioctl(*fd, VIDIOC_QBUF, &b);
    }
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(*fd, VIDIOC_STREAMON, &type);
    printf("uvc: streaming on %s\n", dev);
    return 0;
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

static int panel_ok = 0;

static void panel_init(void)
{
    spi_res_open(49);   /* JP4.29 = GPIO1_C1 */
    spi_dc_open(47);    /* JP4.33 = GPIO1_B7 */
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

static void scan_msleep(void)
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
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-6") == 0)
            pix18 = 1;
        else if (strcmp(argv[i], "-5") != 0)
        {
            fprintf(stderr, "usage: qr_scan [-6 RGB666]\n");
            return 1;
        }
    }

    panel_init();
    if (!panel_ok)
    {
        fprintf(stderr, "panel init failed\n");
        return 1;
    }

    zbar_image_scanner_t *scanner = zbar_image_scanner_create();

    zbar_image_scanner_set_config(scanner, ZBAR_NONE, ZBAR_CFG_ENABLE, 1);

    char *dev = NULL;
    int uh = -1;
    uint8_t *gray = NULL;      /* decoded luma copy for zbar */
    int gray_w = 0, gray_h = 0;
    char last_data[512] = "";
    unsigned frames = 0, fps = 0, ndecodes = 0;
    struct timespec ts;
    double t_end = 0;

    printf("zbar: scanner ready\n");

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
                    if (pix18)
                        memset(panel_666, 0, sizeof(panel_666));
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
            fprintf(stderr, "uvc: cam_open failed, rescanning\n");
            free(dev);
            dev = NULL;
            scan_msleep();
            continue;
        }
        if (gray_w != cam_w || gray_h != cam_h)
        {
            gray_w = cam_w;
            gray_h = cam_h;
            free(gray);
            gray = malloc((size_t)gray_w * gray_h);
            build_src_map(cam_w, cam_h);
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

        const uint8_t *yuyv = bufs[b.index].start;

        /* camera view -> panel (rotate 90 CW, fit) */
        convert_yuyv_frame(yuyv, cam_w);

        /* luma plane -> zbar (Y800) */
        for (int y = 0; y < gray_h; y++)
        {
            const uint8_t *row = yuyv + y * gray_w * 2;
            uint8_t *out = gray + y * gray_w;

            for (int x = 0; x < gray_w; x++)
                out[x] = row[x * 2];
        }
        zbar_image_t *zimg = zbar_image_create();

        zbar_image_set_format(zimg, zbar_fourcc('Y', '8', '0', '0'));
        zbar_image_set_size(zimg, gray_w, gray_h);
        zbar_image_set_data(zimg, gray, (unsigned long)gray_w * gray_h, NULL);
        int n = zbar_scan_image(scanner, zimg);

        if (n > 0)
        {
            /* log a code when it is new, or when the same code is shown
               again after a 3 s cooldown (deliberate re-scan) */
            static double t_last_log = 0;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            double now_log = ts.tv_sec + ts.tv_nsec / 1e9;

            for (const zbar_symbol_t *sym = zbar_image_first_symbol(zimg);
                 sym; sym = zbar_symbol_next(sym))
            {
                const char *data = zbar_symbol_get_data(sym);
                zbar_symbol_type_t typ = zbar_symbol_get_type(sym);

                if (strcmp(data, last_data) != 0 ||
                    now_log - t_last_log >= 3.0)
                {
                    snprintf(last_data, sizeof(last_data), "%s", data);
                    ndecodes++;
                    t_last_log = now_log;
                    printf("qr[%d]: %s \"%s\"\n", ndecodes,
                           zbar_get_symbol_name(typ), data);
                }
            }
        }
        xioctl(cam_fd, VIDIOC_QBUF, &b);   /* re-queue */
        zbar_image_set_data(zimg, NULL, 0, NULL);
        zbar_image_destroy(zimg);

        /* band: fps + decode count only; the decoded TEXT goes to the
           log, not the panel (user request) */
        char band[40];

        snprintf(band, sizeof(band), "uh-%d %u fps %u codes",
                 uh < 0 ? 0 : uh, fps, ndecodes);
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
            printf("fps: %u  codes %u\n", fps, ndecodes);
        }
    }

    if (cam_fd >= 0)
        cam_release(&cam_fd, &bufs, &nbufs);
    free(gray);
    free(src_map);
    free(dev);
    zbar_image_scanner_destroy(scanner);

    memset(panel_fb, 0, sizeof(panel_fb));
    if (pix18)
        memset(panel_666, 0, sizeof(panel_666));
    fb_string_t("bye", 60, 120, LCD_WHITE);
    blit();
    printf("bye\n");
    return 0;
}
