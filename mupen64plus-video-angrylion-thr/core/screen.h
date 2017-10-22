#pragma once

#include <stdint.h>
#include <stdbool.h>

void screen_init(void);
void screen_swap(void);
void screen_upload(int32_t* buffer, int32_t width, int32_t height, int32_t pitch, int32_t output_height);
void screen_set_fullscreen(bool fullscreen);
bool screen_get_fullscreen(void);
void screen_toggle_fullscreen(void);
void screen_close(void);
