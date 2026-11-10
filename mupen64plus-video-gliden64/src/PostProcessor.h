#ifndef POST_PROCESSOR_H
#define POST_PROCESSOR_H

#ifdef __cplusplus
extern "C" {
#endif

struct FrameBuffer;

void gli_pp_init(void);
void gli_pp_destroy(void);

/* Runs the buffer through FXAA and gamma correction; returns the result */
struct FrameBuffer * gli_pp_run(struct FrameBuffer * _pBuffer);

#ifdef __cplusplus
}
#endif

#endif /* POST_PROCESSOR_H */
