/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-Next - libretro_core_options.h                            *
 *   Copyright (C) 2020 M4xw <m4x@m4xw.net>                                *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef LIBRETRO_CORE_OPTIONS_H__
#define LIBRETRO_CORE_OPTIONS_H__

#include <stdlib.h>
#include <string.h>

#include <libretro.h>
#include <retro_inline.h>

#define CORE_NAME "parallel-n64"

#ifdef __cplusplus
extern "C"
{
#endif

#define HAVE_NO_LANGEXTRA
struct retro_core_option_v2_category option_cats_us[] = {
   {
      "input",
      "Pak/Controller Options",
      "Configure Core Pak/Controller Options."
   },
#ifdef HAVE_PARALLEL
   {
      "parallel",
      "ParaLLEl",
      "Configure ParaLLEl Options."
   },
#endif // HAVE_PARALLEL
#ifdef HAVE_THR_AL
   {
      "angrylion",
      "Angrylion",
      "Configure Angrylion Options."
   },
#endif // HAVE_THR_AL
   {
      "gliden64",
      "GLideN64",
      "Configure GLideN64 Options."
   },
   {
      "glide64",
      "Glide64",
      "Configure Glide64 Options."
   },
   { NULL, NULL, NULL },
};

struct retro_core_option_v2_definition option_defs_us[] = {
    {
        CORE_NAME "-cpucore",
        "CPU Core",
        NULL,
        "Select CPU Core",
        NULL,
        NULL,
        {
            {"cached_interpreter", "Angrylion"},
            {"pure_interpreter", "ParaLLEl-RDP"},
#ifdef DYNAREC
            {"dynamic_recompiler", "Dynamic Recompiler"},
#endif
            { NULL, NULL },
        },
#if defined(IOS)
        "cached_interpreter"
#else
#ifdef DYNAREC
        "dynamic_recompiler"
#else
        "cached_interpreter"
#endif
#endif
    },
    {
        CORE_NAME "-audio-buffer-size",
        "Audio Buffer Size",
        NULL,
        "Audio Buffer Size (restart)",
        NULL,
        NULL,
        {
            { "1024", NULL },
            { "2048", NULL },
            { NULL, NULL },
        },
        "2048"
    },
    {
        CORE_NAME "-astick-deadzone",
        "Analog Deadzone",
        NULL,
        "Analog Deadzone (percent)",
        NULL,
        NULL,
        {
            { "0", NULL },
            { "5", NULL },
            { "10", NULL },
            { "15", NULL },
            { "20", NULL },
            { "25", NULL },
            { "30", NULL },
            { NULL, NULL },
        },
        "15"
    },
    {
        CORE_NAME "-astick-sensitivity",
        "Analog Sensitivity",
        NULL,
        "Analog Sensitivity (percent)",
        NULL,
        NULL,
        {
            { "50", NULL },
            { "55", NULL },
            { "60", NULL },
            { "65", NULL },
            { "70", NULL },
            { "75", NULL },
            { "80", NULL },
            { "85", NULL },
            { "90", NULL },
            { "95", NULL },
            { "100", NULL },
            { "105", NULL },
            { "110", NULL },
            { "115", NULL },
            { "120", NULL },
            { "125", NULL },
            { "130", NULL },
            { "135", NULL },
            { "140", NULL },
            { "145", NULL },
            { "150", NULL },
            { "200", NULL },
            { NULL, NULL },
        },
        "100"
    },
    {
        CORE_NAME "-pak1",
        "Player 1 Pak",
        NULL,
        "Player 1 Pak",
        NULL,
        NULL,
        {
            { "none", NULL },
            { "memory", NULL },
            { "rumble", NULL },
            { NULL, NULL },
        },
        "none"
    },
    {
        CORE_NAME "-pak2",
        "Player 2 Pak",
        NULL,
        "Player 2 Pak",
        NULL,
        NULL,
        {
            { "none", NULL },
            { "memory", NULL },
            { "rumble", NULL },
            { NULL, NULL },
        },
        "none"
    },
    {
        CORE_NAME "-pak3",
        "Player 3 Pak",
        NULL,
        "Player 3 Pak",
        NULL,
        NULL,
        {
            { "none", NULL },
            { "memory", NULL },
            { "rumble", NULL },
            { NULL, NULL },
        },
        "none"
    },
    {
        CORE_NAME "-pak4",
        "Player 4 Pak",
        NULL,
        "Player 4 Pak",
        NULL,
        NULL,
        {
            { "none", NULL },
            { "memory", NULL },
            { "rumble", NULL },
            { NULL, NULL },
        },
        "none"
    },
    {
        CORE_NAME "-disable_expmem",
        "Enable Expansion Pak RAM",
        NULL,
        "Enable Expansion Pak RAM",
        NULL,
        NULL,
        {
            { "enabled", NULL },
            { "disabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-gfxplugin-accuracy",
        "GFX Accuracy",
        NULL,
        "GFX Accuracy, required restart",
        NULL,
        NULL,
        {
            { "low", NULL },
            { "medium", NULL },
            { "high", NULL },
            { "veryhigh", NULL },
            { NULL, NULL },
        },
#if defined(IOS) || defined(ANDROID)
        "medium"
#else
        "veryhigh"
#endif
    },
#ifdef HAVE_PARALLEL
    {
        CORE_NAME "-parallel-rdp-synchronous",
        "ParaLLEl Synchronous RDP",
        NULL,
        "ParaLLEl Synchronous RDP",
        NULL,
        NULL,
        {
            { "enabled", NULL },
            { "disabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-parallel-rdp-overscan",
        "(ParaLLEl-RDP) Crop pixel border pixels",
        NULL,
        "Crop pixel border pixels",
        NULL,
        "parallel",
        {
            { "0", NULL },
            { "2", NULL },
            { "4", NULL },
            { "6", NULL },
            { "8", NULL },
            { "10", NULL },
            { "12", NULL },
            { "14", NULL },
            { "16", NULL },
            { "18", NULL },
            { "20", NULL },
            { "22", NULL },
            { "24", NULL },
            { "26", NULL },
            { "28", NULL },
            { "30", NULL },
            { "32", NULL },
            { "34", NULL },
            { "36", NULL },
            { "38", NULL },
            { "40", NULL },
            { "42", NULL },
            { "44", NULL },
            { "46", NULL },
            { "48", NULL },
            { "50", NULL },
            { "52", NULL },
            { "54", NULL },
            { "56", NULL },
            { "58", NULL },
            { "60", NULL },
            { "62", NULL },
            { "64", NULL },
            { NULL, NULL },
        },
        "0"
    },
    {
        CORE_NAME "-parallel-rdp-divot-filter",
        "(ParaLLEl-RDP) VI divot filter",
        NULL,
        "VI divot filter",
        NULL,
        "parallel",
        {
            { "enabled", NULL },
            { "disabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-parallel-rdp-gamma-dither",
        "(ParaLLEl-RDP) VI gamma dither",
        NULL,
        "VI gamma dither",
        NULL,
        "parallel",
        {
            { "enabled", NULL },
            { "disabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-parallel-rdp-vi-aa",
        "(ParaLLEl-RDP) VI AA",
        NULL,
        "VI AntiAliasing",
        NULL,
        "parallel",
        {
            { "enabled", NULL },
            { "disabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-parallel-rdp-vi-bilinear",
        "(ParaLLEl-RDP) VI bilinear",
        NULL,
        "VI bilinear filtering",
        NULL,
        "parallel",
        {
            { "enabled", NULL },
            { "disabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-parallel-rdp-dither-filter",
        "(ParaLLEl-RDP) VI dither filter",
        NULL,
        "VI dither filter",
        NULL,
        "parallel",
        {
            { "enabled", NULL },
            { "disabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-parallel-rdp-upscaling",
        "(ParaLLEl-RDP) Upscaling factor (restart)",
        NULL,
        "Upscaling factor (restart)",
        NULL,
        "parallel",
        {
            { "1x", NULL },
            { "2x", NULL },
            { "4x", NULL },
            { "8x", NULL },
            { NULL, NULL },
        },
        "1x"
    },
    {
        CORE_NAME "-parallel-rdp-downscaling",
        "(ParaLLEl-RDP) Downsampling",
        NULL,
        "Downsampling",
        NULL,
        "parallel",
        {
            { "disable", NULL },
            { "1/2", NULL },
            { "1/4", NULL },
            { "1/8", NULL },
            { NULL, NULL },
        },
        "disable"
    },
    {
        CORE_NAME "-parallel-rdp-native-texture-lod",
        "(ParaLLEl-RDP) Use native texture LOD when upscaling",
        NULL,
        "Use native texture LOD when upscaling",
        NULL,
        "parallel",
        {
            { "enabled", NULL },
            { "disabled", NULL },
            { NULL, NULL },
        },
        "disabled"
    },
    {
        CORE_NAME "-parallel-rdp-native-tex-rect",
        "(ParaLLEl-RDP) Use native resolution for TEX_RECT",
        NULL,
        "Use native resolution for TEX_RECT",
        NULL,
        "parallel",
        {
            { "enabled", NULL },
            { "disabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
#endif

    {
        CORE_NAME "-send_allist_to_hle_rsp",
        "Send audio lists to HLE RSP",
        NULL,
        "Send audio lists to HLE RSP",
        NULL,
        NULL,
        {
            { "enabled", NULL },
            { "disabled", NULL },
            { NULL, NULL },
        },
        "disabled"
    },
    {
        CORE_NAME "-gfxplugin",
        "GFX Plugin",
        NULL,
        "Graphics plugin",
        NULL,
        NULL,
        {
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
            { "gliden64", NULL },
            { "glide64", NULL },
            { "gln64", NULL },
            { "rice", NULL },
#endif
            { "angrylion", NULL },
#ifdef HAVE_PARALLEL
            { "parallel", NULL },
#endif
            { NULL, NULL },
        },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
        "gliden64"
#else
        "angrylion"
#endif
    },
    {
        CORE_NAME "-rspplugin",
        "RSP Plugin",
        NULL,
        "RSP Plugin",
        NULL,
        NULL,
        {
            { "auto", NULL },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
            { "hle", NULL },
#endif
            { "cxd4", NULL },
#ifdef HAVE_PARALLEL_RSP
            { "parallel", NULL },
#endif
            { NULL, NULL },
        },
        "auto"
    },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_PARALLEL)
    {
        CORE_NAME "-screensize",
        "Resolution",
        NULL,
        "Resolution (restart)",
        NULL,
        NULL,
        {
            { "320x240", NULL },
            { "640x480", NULL },
            { "960x720", NULL },
            { "1280x960", NULL },
            { "1440x1080", NULL },
            { "1600x1200", NULL },
            { "1920x1440", NULL },
            { "2240x1680", NULL },
            { "2880x2160", NULL },
            { "5760x4320", NULL },
            { NULL, NULL },
        },
#ifdef CLASSIC
        "320x240"
#else
        "640x480"
#endif
    },
    {
        CORE_NAME "-aspectratiohint",
        "Aspect ratio hint",
        NULL,
        "Aspect ratio hint (reinit)",
        NULL,
        NULL,
        {
            { "normal", NULL },
            { "widescreen", NULL },
            { NULL, NULL },
        },
        "normal"
    },
    {
        CORE_NAME "-filtering",
        "(Glide64) Texture Filtering",
        NULL,
        "Texture Filtering",
        NULL,
        "glide64",
        {
            { "automatic", NULL },
            { "N64 3-point", NULL },
            { "bilinear", NULL },
            { "nearest", NULL },
            { NULL, NULL },
        },
        "automatic"
    },
    {
        CORE_NAME "-dithering",
        "(Angrylion) Dithering",
        NULL,
        "Dithering",
        NULL,
        "angrylion",
        {
            { "enabled", NULL },
            { "disabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-polyoffset-factor",
        "(Glide64) Polygon Offset Factor",
        NULL,
        "Polygon Offset Factor",
        NULL,
        "glide64",
        {
            { "-5.0", NULL },
            { "-4.5", NULL },
            { "-4.0", NULL },
            { "-3.5", NULL },
            { "-3.0", NULL },
            { "-2.5", NULL },
            { "-2.0", NULL },
            { "-1.5", NULL },
            { "-1.0", NULL },
            { "-0.5", NULL },
            {  "0.0", NULL },
            {  "0.5", NULL },
            {  "1.0", NULL },
            {  "1.5", NULL },
            {  "2.0", NULL },
            {  "2.5", NULL },
            {  "3.0", NULL },
            {  "3.5", NULL },
            {  "4.0", NULL },
            {  "4.5", NULL },
            {  "5.0", NULL },
            { NULL, NULL },
        },
        "-3.0"
    },
    {
        CORE_NAME "-polyoffset-units",
        "(Glide64) Polygon Offset Units",
        NULL,
        "Polygon Offset Units",
        NULL,
        "glide64",
        {
            { "-5.0", NULL },
            { "-4.5", NULL },
            { "-4.0", NULL },
            { "-3.5", NULL },
            { "-3.0", NULL },
            { "-2.5", NULL },
            { "-2.0", NULL },
            { "-1.5", NULL },
            { "-1.0", NULL },
            { "-0.5", NULL },
            {  "0.0", NULL },
            {  "0.5", NULL },
            {  "1.0", NULL },
            {  "1.5", NULL },
            {  "2.0", NULL },
            {  "2.5", NULL },
            {  "3.0", NULL },
            {  "3.5", NULL },
            {  "4.0", NULL },
            {  "4.5", NULL },
            {  "5.0", NULL },
            { NULL, NULL },
        },
        "-3.0"
    },
#endif
    {
        CORE_NAME "-angrylion-vioverlay",
        "(Angrylion) VI Overlay",
        NULL,
        "VI Overlay",
        NULL,
        "angrylion",
        {
            { "Filtered", NULL },
            { "AA+Blur", NULL },
            { "AA+Dedither", NULL },
            { "AA only", NULL },
            { "Unfiltered", NULL },
            { "Depth", NULL },
            { "Coverage", NULL },
            { NULL, NULL },
        },
        "Filtered"
    },
    {
        CORE_NAME "-angrylion-sync",
        "(Angrylion) Thread sync level",
        NULL,
        "Thread sync level",
        NULL,
        "angrylion",
        {
            { "Low", NULL },
            { "Medium", NULL },
            { "High", NULL },
            { NULL, NULL },
        },
        "Low"
    },
    {
        CORE_NAME "-angrylion-multithread",
        "(Angrylion) Multi-threading",
        NULL,
        "Multi-threading",
        NULL,
        "angrylion",
        {
            { "all threads", NULL },
            { "1", NULL },
            { "2", NULL },
            { "3", NULL },
            { "4", NULL },
            { "5", NULL },
            { "6", NULL },
            { "7", NULL },
            { "8", NULL },
            { "9", NULL },
            { "10", NULL },
            { "11", NULL },
            { "12", NULL },
            { "13", NULL },
            { "14", NULL },
            { "15", NULL },
            { "16", NULL },
            { "17", NULL },
            { "18", NULL },
            { "19", NULL },
            { "20", NULL },
            { "21", NULL },
            { "22", NULL },
            { "23", NULL },
            { "24", NULL },
            { "25", NULL },
            { "26", NULL },
            { "27", NULL },
            { "28", NULL },
            { "29", NULL },
            { "30", NULL },
            { "31", NULL },
            { "32", NULL },
            { "33", NULL },
            { "34", NULL },
            { "35", NULL },
            { "36", NULL },
            { "37", NULL },
            { "38", NULL },
            { "39", NULL },
            { "40", NULL },
            { "41", NULL },
            { "42", NULL },
            { "43", NULL },
            { "44", NULL },
            { "45", NULL },
            { "46", NULL },
            { "47", NULL },
            { "48", NULL },
            { "49", NULL },
            { "50", NULL },
            { "51", NULL },
            { "52", NULL },
            { "53", NULL },
            { "54", NULL },
            { "55", NULL },
            { "56", NULL },
            { "57", NULL },
            { "58", NULL },
            { "59", NULL },
            { "60", NULL },
            { "61", NULL },
            { "62", NULL },
            { "63", NULL },
            { NULL, NULL },
        },
        "all threads"
    },
    {
        CORE_NAME "-angrylion-overscan",
        "(Angrylion) Hide overscan",
        NULL,
        "Hide overscan",
        NULL,
        "angrylion",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "disabled"
    },
    {
        CORE_NAME "-virefresh",
        "VI Refresh (Overclock)",
        NULL,
        "VI Refresh",
        NULL,
        NULL,
        {
            { "auto", NULL },
            { "1500", NULL },
            { "2200", NULL },
            { NULL, NULL },
        },
        "auto"
    },
    {
        CORE_NAME "-bufferswap",
        "Buffer Swap",
        NULL,
        "Buffer Swap",
        NULL,
        NULL,
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "disabled"
    },
    {
        CORE_NAME "-framerate",
        "Framerate (restart)",
        NULL,
        "Framerate (restart)",
        NULL,
        NULL,
        {
            { "original", NULL },
            { "fullspeed", NULL },
            { NULL, NULL },
        },
        "original"
    },
    {
        CORE_NAME "-alt-map",
        "Independent C-button Controls",
        NULL,
        "Independent C-button Controls",
        NULL,
        NULL,
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "disabled"
    },
#if defined(HAVE_PARALLEL) || !defined(HAVE_OPENGL) || !defined(HAVE_OPENGLES)
    {
        CORE_NAME "-vcache-vbo",
        "(Glide64) Vertex cache VBO",
        NULL,
        "Vertex cache VBO (restart)",
        NULL,
        "glide64",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "disabled"
    },
#endif
    {
        CORE_NAME "-boot-device",
        "Boot Device",
        NULL,
        "Boot Device",
        NULL,
        NULL,
        {
            { "Default", NULL },
            { "64DD IPL", NULL },
            { NULL, NULL },
        },
        "Default"
    },
    {
        CORE_NAME "-64dd-hardware",
        "64DD Hardware",
        NULL,
        "64DD Hardware",
        NULL,
        NULL,
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "disabled"
    },
    {
        CORE_NAME "-gliden64-viewport-hack",
        "Widescreen Hack",
        NULL,
        "(GLideN64) Hack that adjusts the viewport to allow unstretched 16:9",
        "Hack that adjusts the viewport to allow unstretched 16:9",
        "gliden64",
        {
            {"enabled", NULL },
            {"disabled", NULL},
            { NULL, NULL },
        },
        "disabled"
    },
    {
        CORE_NAME "-gliden64-EnableNativeResFactor",
        "Native Resolution Factor",
        NULL,
        "(GLideN64) Render at N times the native resolution.",
        "Render at N times the native resolution.",
        "gliden64",
        {
            {"0", "Disabled"},
            {"1", "1x"},
            {"2", "2x"},
            {"3", "3x"},
            {"4", "4x"},
            {"5", "5x"},
            {"6", "6x"},
            {"7", "7x"},
            {"8", "8x"},
            { NULL, NULL },
        },
        "0"
    },
    {
        CORE_NAME "-gliden64-BilinearMode",
        "Bilinear filtering mode",
        NULL,
        "(GLideN64) Select a Bilinear filtering method, 3point is the original system specific way.",
        "Select a Bilinear filtering method, 3point is the original system specific way.",
        "gliden64",
        {
            {"3point", NULL},
            {"standard", NULL},
            { NULL, NULL },
        },
        "standard"
    },
#ifndef HAVE_OPENGLES2
    {
        CORE_NAME "-gliden64-MultiSampling",
        "MSAA level",
        NULL,
        "(GLideN64) Anti-Aliasing level (0 = disabled).",
        "Anti-Aliasing level (0 = disabled).",
        "gliden64",
        {
            {"0", NULL},
            {"2", NULL},
            {"4", NULL},
            {"8", NULL},
            {"16", NULL},
            { NULL, NULL },
        },
        "0"
    },
#endif
    {
        CORE_NAME "-gliden64-FXAA",
        "FXAA",
        NULL,
        "(GLideN64) Fast Approximate Anti-Aliasing shader, moderately blur textures (0 = disabled).",
        "Fast Approximate Anti-Aliasing shader, moderately blur textures (0 = disabled).",
        "gliden64",
        {
            {"0", NULL},
            {"1", NULL},
            { NULL, NULL },
        },
        "0"
    },
    {
        CORE_NAME "-gliden64-EnableLODEmulation",
        "LOD Emulation",
        NULL,
        "(GLideN64) Calculate per-pixel Level Of Details to select texture mip levels and blend them with each other using LOD fraction.",
        "Calculate per-pixel Level Of Details to select texture mip levels and blend them with each other using LOD fraction.",
        "gliden64",
        {
            {"False", NULL},
            {"True", NULL},
            { NULL, NULL },
        },
        "True"
    },
    {
        CORE_NAME "-gliden64-EnableFBEmulation",
        "Framebuffer Emulation",
        NULL,
        "(GLideN64) Frame/depth buffer emulation. Disabling it can shorten input lag for particular games, but also break some special effects.",
        "Frame/depth buffer emulation. Disabling it can shorten input lag for particular games, but also break some special effects.",
        "gliden64",
        {
            {"False", NULL},
            {"True", NULL},
            { NULL, NULL },
        },
#ifndef VC
        "True"
#else
        "False"
#endif // VC
    },
    {
        CORE_NAME "-gliden64-EnableCopyAuxToRDRAM",
        "Copy auxiliary buffers to RDRAM",
        NULL,
        "(GLideN64) Copy auxiliary buffers to RDRAM (fixes some Game artifacts like Paper Mario Intro).",
        "Copy auxiliary buffers to RDRAM (fixes some Game artifacts like Paper Mario Intro).",
        "gliden64",
        {
            {"False", NULL},
            {"True", NULL},
            { NULL, NULL },
        },
        "False",
    },
    {
        CORE_NAME "-gliden64-EnableCopyColorToRDRAM",
        "Color buffer to RDRAM",
        NULL,
        "(GLideN64) Color buffer copy to RDRAM (Off will trade compatibility for Performance).",
        "Color buffer copy to RDRAM (Off will trade compatibility for Performance).",
        "gliden64",
        {
            {"Off", NULL},
            {"Sync", NULL},
#ifndef HAVE_OPENGLES2
            {"Async", "DoubleBuffer"},
            {"TripleBuffer", "TripleBuffer"},
#endif // HAVE_OPENGLES2
            { NULL, NULL },
        },
#ifndef HAVE_OPENGLES2
        "Async"
#else
        "Sync"
#endif // HAVE_OPENGLES2
    },
    {
        CORE_NAME "-gliden64-EnableCopyDepthToRDRAM",
        "Depth buffer to RDRAM",
        NULL,
        "(GLideN64) Depth buffer copy to RDRAM (Off will trade compatibility for Performance).",
        "Depth buffer copy to RDRAM (Off will trade compatibility for Performance).",
        "gliden64",
        {
            {"Off", NULL},
            {"Software", NULL},
            {"FromMem", NULL},
            { NULL, NULL },
        },
        "Software"
    },
    {
        CORE_NAME "-gliden64-BackgroundMode",
        "Background Mode",
        NULL,
        "(GLideN64) Render backgrounds mode (HLE only). One piece (fast), Stripped (precise).",
        "Render backgrounds mode (HLE only). One piece (fast), Stripped (precise).",
        "gliden64",
        {
            {"Stripped", NULL},
            {"OnePiece", NULL},
            { NULL, NULL },
        },
        "OnePiece"
    },
    {
        CORE_NAME "-gliden64-EnableHWLighting",
        "Hardware per-pixel lighting",
        NULL,
        "(GLideN64) Standard per-vertex lighting when disabled. Slightly different rendering.",
        "Standard per-vertex lighting when disabled. Slightly different rendering.",
        "gliden64",
        {
            {"False", NULL},
            {"True", NULL},
            { NULL, NULL },
        },
        "False"
    },
    {
        CORE_NAME "-gliden64-CorrectTexrectCoords",
        "Continuous texrect coords",
        NULL,
        "(GLideN64) Make texrect coordinates continuous to avoid black lines between them.",
        "Make texrect coordinates continuous to avoid black lines between them.",
        "gliden64",
        {
            {"Off", NULL},
            {"Auto", NULL},
            {"Force", NULL},
            { NULL, NULL },
        },
        "Off"
    },
    {
        CORE_NAME "-gliden64-EnableInaccurateTextureCoordinates",
        "Enable inaccurate texture coordinates",
        NULL,
        "(GLideN64) Enables inaccurate texture coordinate calculations. This can improve performance and texture pack compatibity at the cost of accuracy.",
        "Enables inaccurate texture coordinate calculations. This can improve performance and texture pack compatibity at the cost of accuracy.",
        "gliden64",
        {
            {"False", NULL},
            {"True", NULL},
            { NULL, NULL },
        },
        "False"
    },
    {
        CORE_NAME "-gliden64-EnableNativeResTexrects",
        "Native res. 2D texrects",
        NULL,
        "(GLideN64) Render 2D texrects in native resolution to fix misalignment between parts of 2D image (example: Mario Kart driver selection portraits).",
        "Render 2D texrects in native resolution to fix misalignment between parts of 2D image (example: Mario Kart driver selection portraits).",
        "gliden64",
        {
            {"Disabled", NULL},
            {"Unoptimized", NULL},
            {"Optimized", NULL},
            { NULL, NULL },
        },
        "Disabled"
    },
    {
        CORE_NAME "-gliden64-EnableLegacyBlending",
        "Less accurate blending mode",
        NULL,
        "(GLideN64) Do not use shaders to emulate N64 blending modes. Works faster on slow GPU. Can cause glitches.",
        "Do not use shaders to emulate N64 blending modes. Works faster on slow GPU. Can cause glitches.",
        "gliden64",
        {
            {"False", NULL},
            {"True", NULL},
            { NULL, NULL },
        },
#ifdef HAVE_OPENGLES
        "True"
#else
        "False"
#endif
    },
    {
        CORE_NAME "-gliden64-EnableFragmentDepthWrite",
        "GPU shader depth write",
        NULL,
        "(GLideN64) Enable writing of fragment depth. Some mobile GPUs do not support it, thus it's optional. Leave enabled.",
        "Enable writing of fragment depth. Some mobile GPUs do not support it, thus it's optional. Leave enabled.",
        "gliden64",
        {
            {"False", NULL},
            {"True", NULL},
            { NULL, NULL },
        },
#ifdef HAVE_OPENGLES
        "False"
#else
        "True"
#endif
    },
#if !defined(VC) && !defined(HAVE_OPENGLES)
    {
        CORE_NAME "-gliden64-EnableN64DepthCompare",
        "N64 Depth Compare",
        NULL,
        "(GLideN64) Enable N64 depth compare instead of OpenGL standard one. Experimental, Fast mode will have more glitches.",
        "Enable N64 depth compare instead of OpenGL standard one. Experimental, Fast mode will have more glitches.",
        "gliden64",
        {
            {"False", "Off"},
            {"True", "Fast"},
            {"Compatible", NULL},
        },
        "False"
    },
    {
        CORE_NAME "-gliden64-EnableShadersStorage",
        "Cache GPU Shaders",
        NULL,
        "(GLideN64) Use persistent storage for compiled shaders.",
        "Use persistent storage for compiled shaders.",
        "gliden64",
        {
            {"False", NULL},
            {"True", NULL},
            { NULL, NULL },
        },
        "True"
    },
#endif
    {
        CORE_NAME "-gliden64-EnableTextureCache",
        "Cache Textures",
        NULL,
        "(GLideN64) Save texture cache to hard disk.",
        "Save texture cache to hard disk.",
        "gliden64",
        {
            {"False", NULL},
            {"True", NULL},
            { NULL, NULL },
        },
        "True"
    },
    {
        CORE_NAME "-gliden64-EnableOverscan",
        "Overscan",
        NULL,
        "(GLideN64) Crop black borders from the overscan region around the screen.",
        "Crop black borders from the overscan region around the screen.",
        "gliden64",
        {
            {"Disabled", NULL},
            {"Enabled", NULL},
            { NULL, NULL },
        },
        "Enabled"
    },
    {
        CORE_NAME "-gliden64-OverscanTop",
        "Overscan Offset (Top)",
        NULL,
        "(GLideN64) Overscan Top Offset.",
        "Overscan Top Offset.",
        "gliden64",
        {
            {"0", NULL},
            {"1", NULL},
            {"2", NULL},
            {"3", NULL},
            {"4", NULL},
            {"5", NULL},
            {"6", NULL},
            {"7", NULL},
            {"8", NULL},
            {"9", NULL},
            {"10", NULL},
            {"11", NULL},
            {"12", NULL},
            {"13", NULL},
            {"14", NULL},
            {"15", NULL},
            {"16", NULL},
            {"17", NULL},
            {"18", NULL},
            {"19", NULL},
            {"20", NULL},
            {"21", NULL},
            {"22", NULL},
            {"23", NULL},
            {"24", NULL},
            {"25", NULL},
            {"26", NULL},
            {"27", NULL},
            {"28", NULL},
            {"29", NULL},
            {"30", NULL},
            {"31", NULL},
            {"32", NULL},
            {"33", NULL},
            {"34", NULL},
            {"35", NULL},
            {"36", NULL},
            {"37", NULL},
            {"38", NULL},
            {"39", NULL},
            {"40", NULL},
            {"41", NULL},
            {"42", NULL},
            {"43", NULL},
            {"44", NULL},
            {"45", NULL},
            {"46", NULL},
            {"47", NULL},
            {"48", NULL},
            {"49", NULL},
            {"50", NULL},
            { NULL, NULL },
        },
        "0"
    },
    {
        CORE_NAME "-gliden64-OverscanLeft",
        "Overscan Offset (Left)",
        NULL,
        "(GLideN64) Overscan Left Offset.",
        "Overscan Left Offset.",
        "gliden64",
        {
            {"0", NULL},
            {"1", NULL},
            {"2", NULL},
            {"3", NULL},
            {"4", NULL},
            {"5", NULL},
            {"6", NULL},
            {"7", NULL},
            {"8", NULL},
            {"9", NULL},
            {"10", NULL},
            {"11", NULL},
            {"12", NULL},
            {"13", NULL},
            {"14", NULL},
            {"15", NULL},
            {"16", NULL},
            {"17", NULL},
            {"18", NULL},
            {"19", NULL},
            {"20", NULL},
            {"21", NULL},
            {"22", NULL},
            {"23", NULL},
            {"24", NULL},
            {"25", NULL},
            {"26", NULL},
            {"27", NULL},
            {"28", NULL},
            {"29", NULL},
            {"30", NULL},
            {"31", NULL},
            {"32", NULL},
            {"33", NULL},
            {"34", NULL},
            {"35", NULL},
            {"36", NULL},
            {"37", NULL},
            {"38", NULL},
            {"39", NULL},
            {"40", NULL},
            {"41", NULL},
            {"42", NULL},
            {"43", NULL},
            {"44", NULL},
            {"45", NULL},
            {"46", NULL},
            {"47", NULL},
            {"48", NULL},
            {"49", NULL},
            {"50", NULL},
            { NULL, NULL },
        },
        "0"
    },
    {
        CORE_NAME "-gliden64-OverscanRight",
        "Overscan Offset (Right)",
        NULL,
        "(GLideN64) Overscan Right Offset.",
        "Overscan Right Offset.",
        "gliden64",
        {
            {"0", NULL},
            {"1", NULL},
            {"2", NULL},
            {"3", NULL},
            {"4", NULL},
            {"5", NULL},
            {"6", NULL},
            {"7", NULL},
            {"8", NULL},
            {"9", NULL},
            {"10", NULL},
            {"11", NULL},
            {"12", NULL},
            {"13", NULL},
            {"14", NULL},
            {"15", NULL},
            {"16", NULL},
            {"17", NULL},
            {"18", NULL},
            {"19", NULL},
            {"20", NULL},
            {"21", NULL},
            {"22", NULL},
            {"23", NULL},
            {"24", NULL},
            {"25", NULL},
            {"26", NULL},
            {"27", NULL},
            {"28", NULL},
            {"29", NULL},
            {"30", NULL},
            {"31", NULL},
            {"32", NULL},
            {"33", NULL},
            {"34", NULL},
            {"35", NULL},
            {"36", NULL},
            {"37", NULL},
            {"38", NULL},
            {"39", NULL},
            {"40", NULL},
            {"41", NULL},
            {"42", NULL},
            {"43", NULL},
            {"44", NULL},
            {"45", NULL},
            {"46", NULL},
            {"47", NULL},
            {"48", NULL},
            {"49", NULL},
            {"50", NULL},
            { NULL, NULL },
        },
        "0"
    },
    {
        CORE_NAME "-gliden64-OverscanBottom",
        "Overscan Offset (Bottom)",
        NULL,
        "(GLideN64) Overscan Bottom Offset.",
        "Overscan Bottom Offset.",
        "gliden64",
        {
            {"0", NULL},
            {"1", NULL},
            {"2", NULL},
            {"3", NULL},
            {"4", NULL},
            {"5", NULL},
            {"6", NULL},
            {"7", NULL},
            {"8", NULL},
            {"9", NULL},
            {"10", NULL},
            {"11", NULL},
            {"12", NULL},
            {"13", NULL},
            {"14", NULL},
            {"15", NULL},
            {"16", NULL},
            {"17", NULL},
            {"18", NULL},
            {"19", NULL},
            {"20", NULL},
            {"21", NULL},
            {"22", NULL},
            {"23", NULL},
            {"24", NULL},
            {"25", NULL},
            {"26", NULL},
            {"27", NULL},
            {"28", NULL},
            {"29", NULL},
            {"30", NULL},
            {"31", NULL},
            {"32", NULL},
            {"33", NULL},
            {"34", NULL},
            {"35", NULL},
            {"36", NULL},
            {"37", NULL},
            {"38", NULL},
            {"39", NULL},
            {"40", NULL},
            {"41", NULL},
            {"42", NULL},
            {"43", NULL},
            {"44", NULL},
            {"45", NULL},
            {"46", NULL},
            {"47", NULL},
            {"48", NULL},
            {"49", NULL},
            {"50", NULL},
            { NULL, NULL },
        },
        "0"
    },
    {
        CORE_NAME "-gliden64-txFilterMode",
        "Texture filter",
        NULL,
        "(GLideN64) Select Texture Filtering mode.",
        "Select Texture Filtering mode.",
        "gliden64",
        {
            {"None", NULL},
            {"Smooth filtering 1", NULL},
            {"Smooth filtering 2", NULL},
            {"Smooth filtering 3", NULL},
            {"Smooth filtering 4", NULL},
            {"Sharp filtering 1", NULL},
            {"Sharp filtering 2", NULL},
            { NULL, NULL },
        },
        "None"
    },
    {
        CORE_NAME "-gliden64-txEnhancementMode",
        "Texture Enhancement",
        NULL,
        "(GLideN64) Various Texture Filters ('As-Is' will just cache).",
        "Various Texture Filters ('As-Is' will just cache).",
        "gliden64",
        {
            {"None", NULL},
            {"As Is", NULL},
            {"X2", NULL},
            {"X2SAI", NULL},
            {"HQ2X", NULL},
            {"HQ2XS", NULL},
            {"LQ2X", NULL},
            {"LQ2XS", NULL},
            {"HQ4X", NULL},
            {"2xBRZ", NULL},
            {"3xBRZ", NULL},
            {"4xBRZ", NULL},
            {"5xBRZ", NULL},
            {"6xBRZ", NULL},
            { NULL, NULL },
        },
        "None"
    },
    {
        CORE_NAME "-gliden64-txFilterIgnoreBG",
        "Don't filter background textures",
        NULL,
        "(GLideN64) Ignore filtering for Background Textures.",
        "Ignore filtering for Background Textures.",
        "gliden64",
        {
            {"False", NULL},
            {"True", NULL},
            { NULL, NULL },
        },
        "True"
    },
    {
        CORE_NAME "-gliden64-txHiresEnable",
        "Use High-Res textures",
        NULL,
        "(GLideN64) Enable High-Res Texture packs if available.",
        "Enable High-Res Texture packs if available.",
        "gliden64",
        {
            {"False", NULL},
            {"True", NULL},
            { NULL, NULL },
        },
        "False"
    },
    {
        CORE_NAME "-gliden64-txCacheCompression",
        "Use High-Res Texture Cache Compression",
        NULL,
        "(GLideN64) Compress created texture caches.",
        "Compress created texture caches.",
        "gliden64",
        {
            {"False", NULL},
            {"True", NULL},
            { NULL, NULL },
        },
        "True"
    },
    {
        CORE_NAME "-gliden64-txHiresFullAlphaChannel",
        "Use High-Res Full Alpha Channel",
        NULL,
        "(GLideN64) This should be enabled unless it's a old RICE Texture pack.",
        "This should be enabled unless it's a old RICE Texture pack.",
        "gliden64",
        {
            {"False", NULL},
            {"True", NULL},
            { NULL, NULL },
        },
        "False"
    },
    {
        CORE_NAME "-gliden64-EnableHiResAltCRC",
        "Use alternative method for High-Res Checksums",
        NULL,
        "(GLideN64) Use an alternative method for High-Res paletted textures CRC calculations.",
        "Use an alternative method for High-Res paletted textures CRC calculations.",
        "gliden64",
        {
            {"False", NULL},
            {"True", NULL},
            { NULL, NULL },
        },
        "False"
    },
    {
        CORE_NAME "-gliden64-IniBehaviour",
        "INI Behaviour",
        NULL,
        "(GLideN64) Specifies INI Settings behaviour. This should really only contain essential options. Changing this can and will break ROM's, if the correct options aren't set manually. Some options may only be set via INI (fbInfoDisabled).",
        "Specifies INI Settings behaviour. This should really only contain essential options. Changing this can and will break ROM's, if the correct options aren't set manually. Some options may only be set via INI (fbInfoDisabled).",
        "gliden64",
        {
            {"late", "Prioritize INI over Core Options"},
            {"early", "Prioritize Core Options over INI"},
            {"disabled", "Disable INI"},
            { NULL, NULL },
        },
        "late"
    },
    {
        CORE_NAME "-gliden64-LegacySm64ToolsHacks",
        "Patch SM64 Hacks made with SM64 Editor",
        NULL,
        "(GLideN64) Make plugin behave like Jabo in certain case to fix compatibility with legacy SM64 hacks. It adds undocumented behavior that may interfere with real N64 games (unlikely).",
        "Make plugin behave like Jabo in certain case to fix compatibility with legacy SM64 hacks. It adds undocumented behavior that may interfere with real N64 games (unlikely).",
        "gliden64",
        {
            {"enabled", NULL},
            {"disabled", NULL},
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-gliden64-RemoveFBBlackBars",
        "Patch SM64 Hacks made with SM64 Editor",
        NULL,
        "(GLideN64) Remove the black bars added by VI emulation of the FrameBuffer.",
        "Remove the black bars added by VI emulation of the FrameBuffer.",
        "gliden64",
        {
            {"enabled", NULL},
            {"disabled", NULL},
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-FallbackSaveType",
        "Save type for unknown ROMs",
        NULL,
        "Sets the save type used by unknown ROMs",
        NULL,
        NULL,
        {
            {"IGNORE", "Do not force savetype"},
            {"EEPROM_4KB", "EEPROM (4kB)"},
            {"EEPROM_16KB", "EEPROM (16kB)"},
            {"SRAM", "SRAM"},
            {"FLASH_RAM", "FlashRAM"},
            {"CONTROLLER_PACK", "MemPak"},
            {"NONE", "Guess"},
            { NULL, NULL }
        },
        "NONE"
    },
    { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};

struct retro_core_options_v2 options_us = {
   option_cats_us,
   option_defs_us
};

struct retro_core_options_v2 *option_defs_intl[RETRO_LANGUAGE_LAST] = {
   &options_us, /* RETRO_LANGUAGE_ENGLISH */
   NULL,            /* RETRO_LANGUAGE_JAPANESE */
   NULL,            /* RETRO_LANGUAGE_FRENCH */
   NULL,            /* RETRO_LANGUAGE_SPANISH */
   NULL,            /* RETRO_LANGUAGE_GERMAN */
   NULL,            /* RETRO_LANGUAGE_ITALIAN */
   NULL,            /* RETRO_LANGUAGE_DUTCH */
   NULL,            /* RETRO_LANGUAGE_PORTUGUESE_BRAZIL */
   NULL,            /* RETRO_LANGUAGE_PORTUGUESE_PORTUGAL */
   NULL,            /* RETRO_LANGUAGE_RUSSIAN */
   NULL,            /* RETRO_LANGUAGE_KOREAN */
   NULL,            /* RETRO_LANGUAGE_CHINESE_TRADITIONAL */
   NULL,            /* RETRO_LANGUAGE_CHINESE_SIMPLIFIED */
   NULL,            /* RETRO_LANGUAGE_ESPERANTO */
   NULL,            /* RETRO_LANGUAGE_POLISH */
   NULL,            /* RETRO_LANGUAGE_VIETNAMESE */
   NULL,            /* RETRO_LANGUAGE_ARABIC */
   NULL,            /* RETRO_LANGUAGE_GREEK */
   NULL,            /* RETRO_LANGUAGE_TURKISH */
   NULL,            /* RETRO_LANGUAGE_SLOVAK */
   NULL,            /* RETRO_LANGUAGE_PERSIAN */
   NULL,            /* RETRO_LANGUAGE_HEBREW */
   NULL,            /* RETRO_LANGUAGE_ASTURIAN */
   NULL,            /* RETRO_LANGUAGE_FINNISH */
};


static INLINE void libretro_set_core_options(retro_environment_t environ_cb,
      bool *categories_supported)
{
   unsigned version  = 0;
#ifndef HAVE_NO_LANGEXTRA
   unsigned language = 0;
#endif

   if (!environ_cb || !categories_supported)
      return;

   *categories_supported = false;

   if (!environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version))
      version = 0;

   if (version >= 2)
   {
#ifndef HAVE_NO_LANGEXTRA
      struct retro_core_options_v2_intl core_options_intl;

      core_options_intl.us    = &options_us;
      core_options_intl.local = NULL;

      if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
          (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH))
         core_options_intl.local = options_intl[language];

      *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL,
            &core_options_intl);
#else
      *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2,
            &options_us);
#endif
   }
   else
   {
      size_t i, j;
      size_t option_index              = 0;
      size_t num_options               = 0;
      struct retro_core_option_definition
            *option_v1_defs_us         = NULL;
#ifndef HAVE_NO_LANGEXTRA
      size_t num_options_intl          = 0;
      struct retro_core_option_v2_definition
            *option_defs_intl          = NULL;
      struct retro_core_option_definition
            *option_v1_defs_intl       = NULL;
      struct retro_core_options_intl
            core_options_v1_intl;
#endif
      struct retro_variable *variables = NULL;
      char **values_buf                = NULL;

      /* Determine total number of options */
      while (true)
      {
         if (option_defs_us[num_options].key)
            num_options++;
         else
            break;
      }

      if (version >= 1)
      {
         /* Allocate US array */
         option_v1_defs_us = (struct retro_core_option_definition *)
               calloc(num_options + 1, sizeof(struct retro_core_option_definition));

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            struct retro_core_option_v2_definition *option_def_us = &option_defs_us[i];
            struct retro_core_option_value *option_values         = option_def_us->values;
            struct retro_core_option_definition *option_v1_def_us = &option_v1_defs_us[i];
            struct retro_core_option_value *option_v1_values      = option_v1_def_us->values;

            option_v1_def_us->key           = option_def_us->key;
            option_v1_def_us->desc          = option_def_us->desc;
            option_v1_def_us->info          = option_def_us->info;
            option_v1_def_us->default_value = option_def_us->default_value;

            /* Values must be copied individually... */
            while (option_values->value)
            {
               option_v1_values->value = option_values->value;
               option_v1_values->label = option_values->label;

               option_values++;
               option_v1_values++;
            }
         }

#ifndef HAVE_NO_LANGEXTRA
         if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
             (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH) &&
             options_intl[language])
            option_defs_intl = options_intl[language]->definitions;

         if (option_defs_intl)
         {
            /* Determine number of intl options */
            while (true)
            {
               if (option_defs_intl[num_options_intl].key)
                  num_options_intl++;
               else
                  break;
            }

            /* Allocate intl array */
            option_v1_defs_intl = (struct retro_core_option_definition *)
                  calloc(num_options_intl + 1, sizeof(struct retro_core_option_definition));

            /* Copy parameters from option_defs_intl array */
            for (i = 0; i < num_options_intl; i++)
            {
               struct retro_core_option_v2_definition *option_def_intl = &option_defs_intl[i];
               struct retro_core_option_value *option_values           = option_def_intl->values;
               struct retro_core_option_definition *option_v1_def_intl = &option_v1_defs_intl[i];
               struct retro_core_option_value *option_v1_values        = option_v1_def_intl->values;

               option_v1_def_intl->key           = option_def_intl->key;
               option_v1_def_intl->desc          = option_def_intl->desc;
               option_v1_def_intl->info          = option_def_intl->info;
               option_v1_def_intl->default_value = option_def_intl->default_value;

               /* Values must be copied individually... */
               while (option_values->value)
               {
                  option_v1_values->value = option_values->value;
                  option_v1_values->label = option_values->label;

                  option_values++;
                  option_v1_values++;
               }
            }
         }

         core_options_v1_intl.us    = option_v1_defs_us;
         core_options_v1_intl.local = option_v1_defs_intl;

         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL, &core_options_v1_intl);
#else
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, option_v1_defs_us);
#endif
      }
      else
      {
         /* Allocate arrays */
         variables  = (struct retro_variable *)calloc(num_options + 1,
               sizeof(struct retro_variable));
         values_buf = (char **)calloc(num_options, sizeof(char *));

         if (!variables || !values_buf)
            goto error;

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            const char *key                        = option_defs_us[i].key;
            const char *desc                       = option_defs_us[i].desc;
            const char *default_value              = option_defs_us[i].default_value;
            struct retro_core_option_value *values = option_defs_us[i].values;
            size_t buf_len                         = 3;
            size_t default_index                   = 0;

            values_buf[i] = NULL;

            if (desc)
            {
               size_t num_values = 0;

               /* Determine number of values */
               while (true)
               {
                  if (values[num_values].value)
                  {
                     /* Check if this is the default value */
                     if (default_value)
                        if (strcmp(values[num_values].value, default_value) == 0)
                           default_index = num_values;

                     buf_len += strlen(values[num_values].value);
                     num_values++;
                  }
                  else
                     break;
               }

               /* Build values string */
               if (num_values > 0)
               {
                  buf_len += num_values - 1;
                  buf_len += strlen(desc);

                  values_buf[i] = (char *)calloc(buf_len, sizeof(char));
                  if (!values_buf[i])
                     goto error;

                  strcpy(values_buf[i], desc);
                  strcat(values_buf[i], "; ");

                  /* Default value goes first */
                  strcat(values_buf[i], values[default_index].value);

                  /* Add remaining values */
                  for (j = 0; j < num_values; j++)
                  {
                     if (j != default_index)
                     {
                        strcat(values_buf[i], "|");
                        strcat(values_buf[i], values[j].value);
                     }
                  }
               }
            }

            variables[option_index].key   = key;
            variables[option_index].value = values_buf[i];
            option_index++;
         }

         /* Set variables */
         environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
      }

error:
      /* Clean up */

      if (option_v1_defs_us)
      {
         free(option_v1_defs_us);
         option_v1_defs_us = NULL;
      }

#ifndef HAVE_NO_LANGEXTRA
      if (option_v1_defs_intl)
      {
         free(option_v1_defs_intl);
         option_v1_defs_intl = NULL;
      }
#endif

      if (values_buf)
      {
         for (i = 0; i < num_options; i++)
         {
            if (values_buf[i])
            {
               free(values_buf[i]);
               values_buf[i] = NULL;
            }
         }

         free(values_buf);
         values_buf = NULL;
      }

      if (variables)
      {
         free(variables);
         variables = NULL;
      }
   }
}

#ifdef __cplusplus
}
#endif

#endif
