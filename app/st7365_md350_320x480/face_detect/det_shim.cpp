/* Plain-C shim for the C++-only libfacedetection API.
   facedetectcnn.h pulls in <vector>/<iostream>, so the app proper
   stays C; this TU is the only one compiled as C++. */
#include "facedetect/facedetectcnn.h"

#define DET_MAX_FACES 8

extern "C" int det_run(unsigned char *buf, unsigned char *bgr,
                       int w, int h, int step, int *out5)
{
    int *res = facedetect_cnn(buf, bgr, w, h, step);

    if (!res)
        return 0;
    int n = *res < DET_MAX_FACES ? *res : DET_MAX_FACES;

    for (int i = 0; i < n; i++)
    {
        short *p = ((short *)(res + 1)) + FACEDETECTION_RESULT_STRIDE_SHORTS * i;

        out5[i * 5 + 0] = p[0];   /* confidence [1-100] */
        out5[i * 5 + 1] = p[1];   /* x */
        out5[i * 5 + 2] = p[2];   /* y */
        out5[i * 5 + 3] = p[3];   /* w */
        out5[i * 5 + 4] = p[4];   /* h */
    }
    return n;
}
