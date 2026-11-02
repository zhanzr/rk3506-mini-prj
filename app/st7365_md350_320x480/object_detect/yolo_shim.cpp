/* Plain-C shim for ncnn + Yolo-Fastest (C++ API).
   Object boxes come out already NMS'd by the model graph; each output
   row of the net is [cate, score, x1, y1, x2, y2] normalized to the
   input frame. We keep boxes with score >= threshold. */
#include "net.h"

#define YOLO_IN_W 320
#define YOLO_IN_H 320
#define YOLO_SCORE_TH 0.55f   /* v1.1 runs noisy; 0.55 kills most FPs */

static float yolo_th = YOLO_SCORE_TH;

static ncnn::Net ynet;

extern "C" void yolo_set_thresh(int pct)
{
    yolo_th = pct / 100.0f;
}

extern "C" int yolo_init(const char *param, const char *bin)
{
    ynet.opt.num_threads = 4;
    if (ynet.load_param(param) != 0)
        return -1;
    if (ynet.load_model(bin) != 0)
        return -2;
    return 0;
}

extern "C" int yolo_detect(const unsigned char *bgr, int w, int h,
                           int *out6, int maxn)
{
    ncnn::Mat in = ncnn::Mat::from_pixels_resize(bgr, ncnn::Mat::PIXEL_BGR2RGB,
                                                 w, h, YOLO_IN_W, YOLO_IN_H);
    const float norm_vals[3] = {1 / 255.f, 1 / 255.f, 1 / 255.f};

    in.substract_mean_normalize(0, norm_vals);

    ncnn::Extractor ex = ynet.create_extractor();

    ex.input("data", in);
    ncnn::Mat out;

    if (ex.extract("output", out) != 0)
        return -1;

    int n = 0;
    for (int i = 0; i < out.h && n < maxn; i++)
    {
        const float *v = out.row(i);

        if (v[1] < yolo_th)
            continue;
        out6[n * 6 + 0] = (int)(v[2] * w);          /* x1 */
        out6[n * 6 + 1] = (int)(v[3] * h);          /* y1 */
        out6[n * 6 + 2] = (int)(v[4] * w);          /* x2 */
        out6[n * 6 + 3] = (int)(v[5] * h);          /* y2 */
        out6[n * 6 + 4] = (int)v[0];                /* category 0..79 */
        out6[n * 6 + 5] = (int)(v[1] * 100);        /* score percent */
        n++;
    }
    return n;
}
