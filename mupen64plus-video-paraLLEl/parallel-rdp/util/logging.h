/* Copyright (c) 2017-2022 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef GRANITE_LOGGING_H
#define GRANITE_LOGGING_H

#include <stdio.h>
#include <retro_common_api.h>

#ifdef __LIBRETRO__
#include <libretro.h>

RETRO_BEGIN_DECLS

/* To the frontend's log, at its own level */
void granite_log(enum retro_log_level level, const char *fmt, ...);

RETRO_END_DECLS

#define LOGE(...) granite_log(RETRO_LOG_ERROR, __VA_ARGS__)
#define LOGW(...) granite_log(RETRO_LOG_WARN, __VA_ARGS__)
#define LOGI(...) granite_log(RETRO_LOG_INFO, __VA_ARGS__)
#elif defined(ANDROID)
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Granite", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "Granite", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Granite", __VA_ARGS__)
#else
#define GRANITE_LOG_STDERR(tag, ...) \
	do \
	{ \
		fprintf(stderr, tag __VA_ARGS__); \
		fflush(stderr); \
	} while (0)
#define LOGE(...) GRANITE_LOG_STDERR("[ERROR]: ", __VA_ARGS__)
#define LOGW(...) GRANITE_LOG_STDERR("[WARN]: ", __VA_ARGS__)
#define LOGI(...) GRANITE_LOG_STDERR("[INFO]: ", __VA_ARGS__)
#endif

#endif
