/* C side of the Yolo-Fastest ncnn shim (see yolo_shim.cpp) */
#ifndef YOLO_SHIM_H
#define YOLO_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* detection score threshold in percent (default 55) */
void yolo_set_thresh(int pct);

/* load .param + .bin; 0 on success */
int yolo_init(const char *param, const char *bin);

/* run detection on a BGR image (w x h); writes up to maxn boxes as
   {x1,y1,x2,y2,cate,score%} sextuples into out6; returns the count */
int yolo_detect(const unsigned char *bgr, int w, int h,
                int *out6, int maxn);

#ifdef __cplusplus
}
#endif

#endif
