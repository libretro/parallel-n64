#pragma once

#include "n64video.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


struct frame_buffer
{
    uint32_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
};

void screen_init(struct n64video_config* config);
void screen_swap(bool blank);
void screen_write(struct frame_buffer* fb, int32_t output_height);
void screen_read(struct frame_buffer* fb, bool rgb);
void screen_set_fullscreen(bool fullscreen);
bool screen_get_fullscreen(void);
void screen_toggle_fullscreen(void);
void screen_close(void);

#ifdef __cplusplus
}
#endif

