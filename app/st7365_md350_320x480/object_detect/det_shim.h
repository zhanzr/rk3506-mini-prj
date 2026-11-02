/* C side of the detector shim (see det_shim.cpp) */
#ifndef DET_SHIM_H
#define DET_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

/* run the CNN on a BGR image; writes up to 8 faces as
   {conf,x,y,w,h} quintuples into out5; returns the face count */
int det_run(unsigned char *buf, unsigned char *bgr,
            int w, int h, int step, int *out5);

#ifdef __cplusplus
}
#endif

#endif
