/*
  wake_word - "yes/no" keyword spotting on the ST7365P 3.5" 320x480 (MD350) SPI panel, Orange Pi Zero.

  Models (Apache-2.0, shipped in tflite-micro):
   - audio_preprocessor_int8.tflite: 30 ms int16 audio -> 40 int8
     filterbank features (49 slices, 20 ms stride = 1 s window)
   - micro_speech_quantized.tflite: features -> silence/unknown/yes/no

  TensorFlow Lite Micro inference (vendored lib), ALSA capture from
  the H3 codec mic (CON5, "default" device, 16 kHz mono S16) linked
  against a vendored copy of the board's libasound.so.2 - same method
  as app/rec_wav.

  A capture thread fills a 1 s ring buffer; the main loop re-runs the
  49-slice feature extraction + speech model about twice a second and
  shows the verdict (YES/NO + score meter) on the panel.
*/
#include <alsa/asoundlib.h>
#include <cmath>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

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

/* micro_speech model settings */
constexpr int kAudioSampleFrequency = 16000;
constexpr int kFeatureSize = 40;
constexpr int kFeatureCount = 49;
constexpr int kFeatureElementCount = kFeatureSize * kFeatureCount;
constexpr int kFeatureDurationMs = 30;
constexpr int kFeatureStrideMs = 20;
constexpr int kAudioSampleDurationCount =
    kFeatureDurationMs * kAudioSampleFrequency / 1000;   /* 480 */
constexpr int kAudioSampleStrideCount =
    kFeatureStrideMs * kAudioSampleFrequency / 1000;     /* 320 */
constexpr int kWinSamples = 16000;                       /* 1 s */
constexpr int kCategoryCount = 4;
static const char *kCategoryLabels[kCategoryCount] = {
    "silence", "unknown", "yes", "no"
};

static uint16_t panel_fb[PANEL_W * PANEL_H];
static uint8_t panel_666[PANEL_W * PANEL_H * 3];
static int pix18 = 0;

static volatile sig_atomic_t quit_req = 0;
static void on_signal(int sig) { (void)sig; quit_req = 1; }

/* ---- panel primitives ---- */

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

static void fb_string_2x(const char *s, int x, int y, uint16_t fg)
{
    while (*s)
    {
        uint8_t c = *s;
        if (c < 0x20 || c > 0x7E) c = '?';
        int idx = c - 0x20;
        for (int row = 0; row < 16; row++)
        {
            uint8_t bits = asc2_1608[idx * 16 + row];
            for (int col = 0; col < 8; col++)
                if ((bits >> col) & 1)
                    for (int dy = 0; dy < 2; dy++)
                        for (int dx = 0; dx < 2; dx++)
                            put_px(x + col * 2 + dx, y + row * 2 + dy, fg);
        }
        x += 16;
        s++;
    }
}

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
    spi_res_open(49);   /* JP4.29 = GPIO1_C1 */
    spi_dc_open(47);    /* JP4.33 = GPIO1_B7 */
    backlight_init();
    backlight_set(55);
    if (spi_open(100 * 1000 * 1000) != 0) { panel_ok = 0; return; }
    LCD_Init();
    LCD_SetAsciiFont(&ASCII_Font16);
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

/* ---- ALSA capture: 16 kHz mono S16 into a 1 s ring ---- */

static int16_t ring[kWinSamples];
static size_t ring_pos = 0;              /* next write index */
static size_t ring_filled = 0;           /* samples written since reset */
static pthread_mutex_t ring_lock = PTHREAD_MUTEX_INITIALIZER;
enum cap_state { CAP_STARTING, CAP_READY, CAP_FAILED, CAP_STOPPED };
static cap_state capture_state = CAP_STARTING;
static pthread_mutex_t cap_state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cap_state_cond = PTHREAD_COND_INITIALIZER;
static const char *cap_dev = "default";

static void set_capture_state(cap_state state)
{
    pthread_mutex_lock(&cap_state_lock);
    capture_state = state;
    pthread_cond_broadcast(&cap_state_cond);
    pthread_mutex_unlock(&cap_state_lock);
}

static cap_state get_capture_state(void)
{
    pthread_mutex_lock(&cap_state_lock);
    cap_state state = capture_state;
    pthread_mutex_unlock(&cap_state_lock);
    return state;
}

static void *cap_thread(void *arg)
{
    (void)arg;
    snd_pcm_t *pcm = NULL;
    int err = snd_pcm_open(&pcm, cap_dev, SND_PCM_STREAM_CAPTURE,
                           SND_PCM_NONBLOCK);
    if (err < 0)
    {
        fprintf(stderr, "alsa open failed: %s\n", snd_strerror(err));
        set_capture_state(CAP_FAILED);
        return NULL;
    }

    snd_pcm_hw_params_t *hw = NULL;
    err = snd_pcm_hw_params_malloc(&hw);
    if (err < 0)
    {
        fprintf(stderr, "alsa hw params allocation failed: %s\n",
                snd_strerror(err));
        snd_pcm_close(pcm);
        set_capture_state(CAP_FAILED);
        return NULL;
    }

    err = snd_pcm_hw_params_any(pcm, hw);
    if (err >= 0)
        err = snd_pcm_hw_params_set_access(pcm, hw,
                                           SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err >= 0)
        err = snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);
    if (err >= 0)
        err = snd_pcm_hw_params_set_channels(pcm, hw, 1);
    unsigned real_rate = kAudioSampleFrequency;
    int rate_dir = 0;
    if (err >= 0)
        err = snd_pcm_hw_params_set_rate_near(pcm, hw, &real_rate, &rate_dir);
    snd_pcm_uframes_t period = 320;      /* 20 ms */
    if (err >= 0)
        err = snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, 0);
    if (err >= 0)
        err = snd_pcm_hw_params(pcm, hw);
    if (err < 0)
    {
        fprintf(stderr, "alsa hw params failed: %s\n", snd_strerror(err));
        snd_pcm_hw_params_free(hw);
        snd_pcm_close(pcm);
        set_capture_state(CAP_FAILED);
        return NULL;
    }
    err = snd_pcm_hw_params_get_rate(hw, &real_rate, &rate_dir);
    snd_pcm_hw_params_free(hw);
    if (err < 0 || real_rate != kAudioSampleFrequency)
    {
        fprintf(stderr, "alsa requires %d Hz; device negotiated %u Hz\n",
                kAudioSampleFrequency, real_rate);
        snd_pcm_close(pcm);
        set_capture_state(CAP_FAILED);
        return NULL;
    }
    err = snd_pcm_prepare(pcm);
    if (err < 0)
    {
        fprintf(stderr, "alsa prepare failed: %s\n", snd_strerror(err));
        snd_pcm_close(pcm);
        set_capture_state(CAP_FAILED);
        return NULL;
    }
    printf("alsa: capturing %u Hz mono (wanted %d)\n",
           real_rate, kAudioSampleFrequency);
    set_capture_state(CAP_READY);

    static int16_t chunk[320];
    while (!quit_req)
    {
        long n = snd_pcm_readi(pcm, chunk, 320);
        if (n == -EAGAIN || n == 0)
        {
            err = snd_pcm_wait(pcm, 100);
            if (err < 0 && snd_pcm_recover(pcm, err, 1) < 0)
            {
                fprintf(stderr, "alsa wait recovery failed: %s\n",
                        snd_strerror(err));
                set_capture_state(CAP_FAILED);
                break;
            }
            continue;
        }
        if (n < 0)
        {
            err = snd_pcm_recover(pcm, (int)n, 1);
            if (err < 0)
            {
                fprintf(stderr, "alsa read recovery failed: %s\n",
                        snd_strerror(err));
                set_capture_state(CAP_FAILED);
                break;
            }
            continue;
        }
        pthread_mutex_lock(&ring_lock);
        for (long i = 0; i < n; i++)
        {
            ring[ring_pos] = chunk[i];
            ring_pos = (ring_pos + 1) % kWinSamples;
            if (ring_filled < kWinSamples) ring_filled++;
        }
        pthread_mutex_unlock(&ring_lock);
    }
    snd_pcm_close(pcm);
    if (get_capture_state() != CAP_FAILED)
        set_capture_state(CAP_STOPPED);
    return NULL;
}

static bool ring_snapshot(int16_t *out)   /* ordered oldest -> newest */
{
    pthread_mutex_lock(&ring_lock);
    bool full = ring_filled == kWinSamples;
    size_t start = full ? ring_pos : 0;
    if (full)
    {
        for (int i = 0; i < kWinSamples; i++)
            out[i] = ring[(start + i) % kWinSamples];
    }
    pthread_mutex_unlock(&ring_lock);
    return full;
}

/* ---- TFLM ---- */

alignas(16) static uint8_t pre_arena[64 * 1024];
alignas(16) static uint8_t speech_arena[64 * 1024];
static uint8_t pre_model_buf[32 * 1024];
static uint8_t speech_model_buf[32 * 1024];
static tflite::MicroInterpreter *pre_interp = nullptr;
static tflite::MicroInterpreter *speech_interp = nullptr;
static int8_t features[kFeatureCount][kFeatureSize];

static bool load_file(const char *path, uint8_t *buf, size_t cap, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    *len = fread(buf, 1, cap, f);
    fclose(f);
    return *len > 0;
}

static bool tflm_init(void)
{
    size_t pre_len = 0, speech_len = 0;

    if (!load_file("audio_preprocessor_int8.tflite", pre_model_buf,
                   sizeof(pre_model_buf), &pre_len))
    {
        fprintf(stderr, "pre model load failed\n");
        return false;
    }
    printf("tflm: pre model %u bytes\n", (unsigned)pre_len);
    if (!load_file("micro_speech_quantized.tflite", speech_model_buf,
                   sizeof(speech_model_buf), &speech_len))
    {
        fprintf(stderr, "speech model load failed\n");
        return false;
    }
    printf("tflm: speech model %u bytes\n", (unsigned)speech_len);

    /* speech ops + the signal custom ops the audio preprocessor uses
       (identical to tflm micro_speech_test RegisterOps) */
    static tflite::MicroMutableOpResolver<24> all_ops;
    all_ops.AddReshape();
    all_ops.AddCast();
    all_ops.AddStridedSlice();
    all_ops.AddConcatenation();
    all_ops.AddMul();
    all_ops.AddAdd();
    all_ops.AddDiv();
    all_ops.AddMinimum();
    all_ops.AddMaximum();
    all_ops.AddWindow();
    all_ops.AddFftAutoScale();
    all_ops.AddRfft();
    all_ops.AddEnergy();
    all_ops.AddFilterBank();
    all_ops.AddFilterBankSquareRoot();
    all_ops.AddFilterBankSpectralSubtraction();
    all_ops.AddPCAN();
    all_ops.AddFilterBankLog();
    all_ops.AddFullyConnected();
    all_ops.AddDepthwiseConv2D();
    all_ops.AddSoftmax();

    static tflite::MicroInterpreter pi(
        tflite::GetModel(pre_model_buf), all_ops, pre_arena,
        sizeof(pre_arena));
    if (pi.AllocateTensors() != kTfLiteOk)
    {
        fprintf(stderr, "pre AllocateTensors failed\n");
        return false;
    }
    printf("tflm: preprocessor arena %u bytes\n",
           (unsigned)pi.arena_used_bytes());
    pre_interp = &pi;

    static tflite::MicroInterpreter si(
        tflite::GetModel(speech_model_buf), all_ops, speech_arena,
        sizeof(speech_arena));
    if (si.AllocateTensors() != kTfLiteOk)
    {
        fprintf(stderr, "speech AllocateTensors failed\n");
        return false;
    }
    printf("tflm: speech arena %u bytes\n",
           (unsigned)si.arena_used_bytes());
    speech_interp = &si;
    return true;
}

/* 1 s audio -> 49x40 features via 49 preprocessor invocations */
static bool generate_features(const int16_t *audio)
{
    TfLiteTensor *in = pre_interp->input(0);
    TfLiteTensor *out = pre_interp->output(0);
    if (in->dims->data[in->dims->size - 1] != kAudioSampleDurationCount)
        return false;
    if (out->dims->data[out->dims->size - 1] != kFeatureSize)
        return false;

    for (int f = 0; f < kFeatureCount; f++)
    {
        std::copy_n(audio + f * kAudioSampleStrideCount,
                    kAudioSampleDurationCount,
                    tflite::micro::GetTensorData<int16_t>(in));
        if (pre_interp->Invoke() != kTfLiteOk)
            return false;
        std::copy_n(tflite::micro::GetTensorData<int8_t>(out), kFeatureSize,
                    features[f]);
    }
    return true;
}

/* ---- panel verdict ---- */

static void draw_verdict(const char *label, int yes_pct, int no_pct)
{
    memset(panel_fb, 0, sizeof(panel_fb));
    if (pix18) memset(panel_666, 0, sizeof(panel_666));

    bool said_yes = strcmp(label, "yes") == 0;
    bool said_no = strcmp(label, "no") == 0;
    uint16_t col = said_yes ? 0x07E0 : (said_no ? 0xF800 : LCD_WHITE);

    char big[24];
    snprintf(big, sizeof(big), "%s", label);
    /* center the 2x text (16 px per char) */
    int len = (int)strlen(big);
    fb_string_2x(big, (PANEL_W - len * 16) / 2, 48, col);

    char line[40];
    snprintf(line, sizeof(line), "yes %d%%   no %d%%", yes_pct, no_pct);
    fb_string_t(line, 40, 140, LCD_WHITE);

    /* yes-score meter */
    int fill = yes_pct * 2;   /* 200 px max */
    fb_string_t("yes:", 16, 180, LCD_WHITE);
    for (int px = 0; px < 200; px++)
        for (int py = 200; py < 216; py++)
            put_px(20 + px, py, px < fill ? 0x07E0 : 0x2104);

    blit();
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* -d <dev> selects audio input; -6 selects RGB666 panel output. */
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc)
            cap_dev = argv[++i];
        else if (strcmp(argv[i], "-6") == 0)
            pix18 = 1;
        else if (strcmp(argv[i], "-5") == 0)
            pix18 = 0;
        else
        {
            fprintf(stderr, "usage: wake_word [-6 RGB666] [-d alsa_dev]\n");
            return 1;
        }
    }

    panel_init();
    if (!panel_ok)
    {
        fprintf(stderr, "panel init failed\n");
        return 1;
    }
    fb_string_t("wake_word: loading", 16, 120, LCD_WHITE);
    blit();

    if (!tflm_init())
    {
        fprintf(stderr, "tflm init failed (model files?)\n");
        return 1;
    }

    /* the H3 codec ships with the capture route disabled -> all-zero
       audio forever; enable Mic1/Mic2 capture + gain first */
    int route_status =
        system("amixer -c 0 sset 'Mic1',0 100%,100% cap > /dev/null 2>&1; "
               "amixer -c 0 sset 'Mic2',0 100%,100% cap > /dev/null 2>&1");
    if (route_status == 0)
        printf("alsa: Mic1/Mic2 capture routes enabled\n");
    else
        fprintf(stderr, "alsa: could not enable codec capture routes (status %d)\n",
                route_status);

    pthread_t cap_tid;
    int thread_err = pthread_create(&cap_tid, NULL, cap_thread, NULL);
    if (thread_err != 0)
    {
        fprintf(stderr, "alsa capture thread creation failed: %s\n",
                strerror(thread_err));
        return 1;
    }
    pthread_mutex_lock(&cap_state_lock);
    while (capture_state == CAP_STARTING && !quit_req)
        pthread_cond_wait(&cap_state_cond, &cap_state_lock);
    cap_state state = capture_state;
    pthread_mutex_unlock(&cap_state_lock);
    if (state != CAP_READY)
    {
        fprintf(stderr, "alsa capture failed during startup\n");
        pthread_join(cap_tid, NULL);
        return 1;
    }
    printf("wake_word: listening (say YES or NO)\n");

    static int16_t window[kWinSamples];
    unsigned rounds = 0;

    while (!quit_req)
    {
        usleep(500 * 1000);   /* ~2 verdicts/s */
        state = get_capture_state();
        if (state == CAP_FAILED)
        {
            fprintf(stderr, "alsa capture stopped after an unrecoverable error\n");
            break;
        }
        if (!ring_snapshot(window))
            continue;

        if (!generate_features(window))
        {
            fprintf(stderr, "feature generation failed\n");
            break;
        }

        TfLiteTensor *in = speech_interp->input(0);
        std::copy_n(&features[0][0], kFeatureElementCount,
                    tflite::micro::GetTensorData<int8_t>(in));
        if (speech_interp->Invoke() != kTfLiteOk)
        {
            fprintf(stderr, "speech invoke failed\n");
            break;
        }
        TfLiteTensor *out = speech_interp->output(0);
        float preds[kCategoryCount];
        int best = 0;
        float bestv = -1e9f;
        for (int i = 0; i < kCategoryCount; i++)
        {
            preds[i] = (out->data.int8[i] - out->params.zero_point) *
                       out->params.scale;
            if (preds[i] > bestv) { bestv = preds[i]; best = i; }
        }
        int yes_pct = (int)(preds[2] * 100 + 0.5f);
        int no_pct = (int)(preds[3] * 100 + 0.5f);

        /* capture liveness: RMS of the window (0 = mic delivers nothing) */
        double sq = 0;
        for (int i = 0; i < kWinSamples; i++)
            sq += (double)window[i] * window[i];
        int rms = (int)(sqrt(sq / kWinSamples));

        draw_verdict(kCategoryLabels[best], yes_pct, no_pct);
        rounds++;
        printf("r%u: rms %d  silence %.2f unknown %.2f yes %.2f no %.2f -> %s\n",
               rounds, rms, (double)preds[0], (double)preds[1],
               (double)preds[2], (double)preds[3],
               kCategoryLabels[best]);
    }

    quit_req = 1;
    pthread_join(cap_tid, NULL);
    memset(panel_fb, 0, sizeof(panel_fb));
    if (pix18) memset(panel_666, 0, sizeof(panel_666));
    fb_string_t("bye", 60, 120, LCD_WHITE);
    blit();
    printf("bye\n");
    return 0;
}
