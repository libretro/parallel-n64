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

#define VR_AIM_BUTTON_VALUES \
   {"None", "disabled"}, \
   {"A", "A Button"}, {"B", "B Button"}, {"Z", "Z Trigger"}, \
   {"L", "L Trigger"}, {"R", "R Trigger"}, {"Start", "Start"}, \
   {"C-Up", NULL}, {"C-Down", NULL}, {"C-Left", NULL}, {"C-Right", NULL}, \
   {"D-Pad Up", NULL}, {"D-Pad Down", NULL}, {"D-Pad Left", NULL}, {"D-Pad Right", NULL}, \
   { NULL, NULL }

#define MOUSE_MAP_VALUES \
   {"None", "disabled"}, \
   {"A", "A Button"}, {"B", "B Button"}, {"Z", "Z Trigger"}, \
   {"L", "L Trigger"}, {"R", "R Trigger"}, {"Start", "Start"}, \
   {"C-Up", NULL}, {"C-Down", NULL}, {"C-Left", NULL}, {"C-Right", NULL}, \
   { NULL, NULL },

#define CONTROLLER_PAK_VALUES \
   { "none", "disabled" }, \
   { "memory", "Memory" }, \
   { "rumble", "Rumble" }, \
   { "biosensor", "Biosensor" }, \
   { "transfer", "Transfer" }, \
   { NULL, NULL },

#define OVERSCAN_VALUES \
   {"0", NULL}, {"1", NULL}, {"2", NULL}, {"3", NULL}, {"4", NULL}, \
   {"5", NULL}, {"6", NULL}, {"7", NULL}, {"8", NULL}, {"9", NULL}, \
   {"10", NULL},{"11", NULL},{"12", NULL},{"13", NULL},{"14", NULL},\
   {"15", NULL},{"16", NULL},{"17", NULL},{"18", NULL},{"19", NULL},\
   {"20", NULL},{"21", NULL},{"22", NULL},{"23", NULL},{"24", NULL},\
   {"25", NULL},{"26", NULL},{"27", NULL},{"28", NULL},{"29", NULL},\
   {"30", NULL},{"31", NULL},{"32", NULL},{"33", NULL},{"34", NULL},\
   {"35", NULL},{"36", NULL},{"37", NULL},{"38", NULL},{"39", NULL},\
   {"40", NULL},{"41", NULL},{"42", NULL},{"43", NULL},{"44", NULL},\
   {"45", NULL},{"46", NULL},{"47", NULL},{"48", NULL},{"49", NULL},\
   {"50", NULL}, \
   { NULL, NULL },


#define HAVE_NO_LANGEXTRA
struct retro_core_option_v2_category option_cats_us[] = {
   {
      "system",
      "System",
      "Configure system options."
   },
   {
      "video",
      "Video",
      "Configure video options."
   },
   {
      "audio",
      "Audio",
      "Configure audio options."
   },
   {
      "input",
      "Input",
      "Configure input & Controller Pak options."
   },
#if defined(HAVE_GLIDEN64) && !defined(HAVE_OPENGLES2)
   {
      "vr",
      "VR",
      "Configure stereoscopic 3D and VR headset options (GLideN64)."
   },
#endif
   {
      "aleck64",
      "Aleck64",
      "Aleck64 arcade dipswitches (only used by Aleck64 MAME romsets)."
   },
   {
      "hacks",
      "Emulation Hacks",
      "Configure emulation hack options."
   },
   { NULL, NULL, NULL },
};

struct retro_core_option_v2_definition option_defs_us[] = {
    {
        CORE_NAME "-gfxplugin",
        "RDP Plugin",
        NULL,
        "Reality Display Processor plugin.",
        NULL,
        "system",
        {
#ifdef HAVE_PARALLEL
            { "parallel", "ParaLLEl" },
#endif
            { "angrylion", "Angrylion" },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
            { "gliden64", "GLideN64" },
            { "glide64", "Glide64" },
            { "gln64", "glN64" },
            { "rice", "Rice" },
#endif
            { NULL, NULL },
        },
#ifdef HAVE_PARALLEL
        "parallel"
#elif defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
        "gliden64"
#else
        "angrylion"
#endif
    },
    {
        CORE_NAME "-rspplugin",
        "RSP Plugin",
        NULL,
        "Reality Signal Processor plugin.",
        NULL,
        "system",
        {
            { "auto", "Auto" },
#ifdef HAVE_PARALLEL_RSP
            { "parallel", "ParaLLEl" },
#endif
            { "cxd4", "Cxd4" },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
            { "hle", "HLE" },
#endif
            { NULL, NULL },
        },
        "auto"
    },
    {
        CORE_NAME "-cpucore",
        "CPU Core",
        NULL,
        "CPU emulation mode.",
        NULL,
        "system",
        {
            {"cached_interpreter", "Cached Interpreter"},
            {"pure_interpreter", "Pure Interpreter"},
#ifdef DYNAREC
#if defined(HAVE_DYNAREC_HACKTARUX) && defined(NEW_DYNAREC)
            {"dynamic_recompiler", "Dynamic Recompiler (Hacktarux)"},
            {"dynamic_recompiler_ari64", "Dynamic Recompiler (Ari64)"},
#elif defined(NEW_DYNAREC)
            {"dynamic_recompiler", "Dynamic Recompiler (Ari64)"},
#else
            {"dynamic_recompiler", "Dynamic Recompiler"},
#endif
#endif
            { NULL, NULL },
        },
#if defined(IOS)
        "cached_interpreter"
#else
#ifdef DYNAREC
#if defined(HAVE_DYNAREC_HACKTARUX) && defined(NEW_DYNAREC)
        "dynamic_recompiler_ari64"
#else
        "dynamic_recompiler"
#endif
#else
        "cached_interpreter"
#endif
#endif
    },
    {
        CORE_NAME "-disable_expmem",
        "Enable Expansion Pak RAM",
        NULL,
        "Give the console 8MB of RAM instead of 4MB, as the Expansion Pak does. Games that require the Pak (Donkey Kong 64, Majora's Mask) will not boot with this disabled. Applied when content starts.",
        NULL,
        "system",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-64dd-hardware",
        "64DD Hardware",
        NULL,
        "Treat content without a recognised cartridge or disk header as a 64DD disk. Development disks and MAME/SDK dumps carry no header marker; regular cartridges and retail D64 images are identified by their header and unaffected by this setting.",
        NULL,
        "system",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "disabled"
    },
    {
        CORE_NAME "-boot-device",
        "Boot Device",
        NULL,
        "Boot device used for 64DD mode.",
        NULL,
        "system",
        {
            { "Default", NULL },
            { "64DD IPL", NULL },
            { NULL, NULL },
        },
        "Default"
    },
    {
        CORE_NAME "-sdcard",
        "Flashcart SD Drive",
        NULL,
        "Emulate a flashcart.",
        NULL,
        "system",
        {
            {"disabled", "disabled"},
            {"SummerCart64", "SummerCart64"},
            { NULL, NULL }
        },
        "disabled"
    },
    {
        CORE_NAME "-virefresh",
        "VI Refresh",
        NULL,
        "Video Interface clock speed.",
        NULL,
        "hacks",
        {
            { "auto", "Auto" },
            { "1500", NULL },
            { "2200", NULL },
            { NULL, NULL },
        },
        "auto"
    },
    {
        CORE_NAME "-allow-large-roms",
        "Allow Large ROMs",
        NULL,
        "Enable support for ROMs that are larger than 64 MiB.",
        NULL,
        "hacks",
        {
            {"False", "disabled"},
            {"True", "enabled"},
            { NULL, NULL }
        },
        "True"
    },
    {
        CORE_NAME "-OverrideSaveType",
        "Save Type For Unknown ROMs",
        NULL,
        "Force save type when not sure.",
        NULL,
        "hacks",
        {
            {"NONE", "Auto"},
            {"IGNORE", "Do not force"},
            {"EEPROM_4KB", "EEPROM (4kB)"},
            {"EEPROM_16KB", "EEPROM (16kB)"},
            {"SRAM", "SRAM"},
            {"FLASH_RAM", "FlashRAM"},
            {"CONTROLLER_PACK", "MemPak"},
            { NULL, NULL }
        },
        "NONE"
    },
    {
        CORE_NAME "-rtc-savestate",
        "Rollback N64 System Clock on Savestate Load",
        NULL,
        "Use state saved timestamp for system clock after loading a state.",
        NULL,
        "hacks",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL }
        },
        "disabled"
    },
    {
        CORE_NAME "-allow-unaligned-dma",
        "Unaligned DMA Behaviour",
        NULL,
        "'Allow Unaligned' causes minor issues on Taz Express; 'Force Alignment' is accurate to N64, but breaks some romhacks.",
        NULL,
        "hacks",
        {
            {"False", "Force Alignment"},
            {"True", "Allow Unaligned"},
            { NULL, NULL }
        },
        "True"
    },
    {
        CORE_NAME "-dma-timing",
        "PI/SI DMA Timing",
        NULL,
        "'Bus Registers' times cartridge and controller DMAs from the PI domain timing registers and the joybus command list, as the hardware does. 'Fixed' keeps the per-device durations.",
        NULL,
        "hacks",
        {
            {"fixed", "Fixed"},
            {"registers", "Bus Registers"},
            { NULL, NULL }
        },
        "fixed"
    },
    {
        CORE_NAME "-goldeneye-tlb-hack",
        "GoldenEye TLB Mapping Hack (Restart)",
        NULL,
        "Map GoldenEye's demand-paged 0x7F000000 code segment directly onto the ROM, bypassing the game's own TLB pager (ari64 dynarec only). Historically required for GoldenEye to run at all; on desktop-class hosts the accurate path now measures just as fast. Disable for hardware-faithful TLB behaviour or for modified ROMs that relocate the game segment, which the mapping would corrupt.",
        NULL,
        "hacks",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-upscaling",
        "Upscaling Factor",
        NULL,
        "Render at a multiple of the console's resolution. ParaLLEl-RDP honours every factor; Angrylion renders at up to 4x and treats 8x as 4x.",
        NULL,
        "video",
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
        /* The key keeps its ParaLLEl-RDP name so existing configurations
         * carry over; both upscaling renderers honour it. */
        CORE_NAME "-parallel-rdp-native-texture-lod",
        "Use Native Texture LOD When Upscaling",
        NULL,
        "Pick texture mip levels as the console would, from a console pixel's footprint rather than an upscaled pixel's. Without it an upscaled render selects sharper levels, and games that use the level as a switch show the wrong image (the painting at the end of the castle corridor in Super Mario 64).",
        NULL,
        "video",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "disabled"
    },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
    {
        CORE_NAME "-screensize",
        "Resolution",
        NULL,
        "Render resolution for gl renderers. Restart required.",
        NULL,
        "video",
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
        "Aspect Ratio Hint",
        NULL,
        "Force display aspect ratio.",
        NULL,
        "video",
        {
            { "normal", "Normal" },
            { "widescreen", "Widescreen" },
            { NULL, NULL },
        },
        "normal"
    },
#ifdef HAVE_PARALLEL
    {
        CORE_NAME "-parallel-rdp-downscaling",
        "ParaLLEl: Downsampling",
        NULL,
        "Scale upscaled images back to smaller size.",
        NULL,
        "video",
        {
            { "disable", "disabled" },
            { "1/2", NULL },
            { "1/4", NULL },
            { "1/8", NULL },
            { NULL, NULL },
        },
        "disable"
    },
    {
        CORE_NAME "-parallel-rdp-vi-aa",
        "ParaLLEl: VI Anti-aliasing",
        NULL,
        "Anti-aliasing is performed to minimize jagged edges in low-resolution polygons.",
        NULL,
        "video",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-parallel-rdp-vi-bilinear",
        "ParaLLEl: VI Bilinear Filtering",
        NULL,
        "Fullscreen post-process blur filter.",
        NULL,
        "video",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-parallel-rdp-gamma-dither",
        "ParaLLEl: VI Gamma Dither",
        NULL,
        "Perform gamma correction using a dither pattern.",
        NULL,
        "video",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-parallel-rdp-dither-filter",
        "ParaLLEl: VI Dither Filter",
        NULL,
        "Make Mach bands less apparent with 16-bit pixels.",
        NULL,
        "video",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-parallel-rdp-divot-filter",
        "ParaLLEl: VI Divot Filter",
        NULL,
        "Filter for eliminating 'divots' (single-pixel holes created when multiple boundary edges overlap in one pixel) in anti-aliased pixels.",
        NULL,
        "video",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-parallel-rdp-super-sampled-read-back",
        "ParaLLEl: Supersampled Readback",
        NULL,
        "When upscaling, average the upscaled samples when a frame is read back into game memory instead of taking one sample per pixel. Can smooth framebuffer effects; changes what the game reads back. Takes effect on restart.",
        NULL,
        "video",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "disabled"
    },
    {
        CORE_NAME "-parallel-rdp-super-sampled-read-back-dither",
        "ParaLLEl: Supersampled Readback Dither",
        NULL,
        "Dither the averaged read-back down to the game's colour depth. Only applies with super-sampled read-back. Takes effect on restart.",
        NULL,
        "video",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-parallel-rdp-native-tex-rect",
        "ParaLLEl: Native Resolution Texture Rectangles",
        NULL,
        "Use native resolution for texture rectangles.",
        NULL,
        "video",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-parallel-rdp-synchronous",
        "ParaLLEl: Synchronous RDP",
        NULL,
        "Make the CPU wait for the GPU to finish rendering at RDP full sync. Disabling can improve performance but breaks games that read rendered frames back from memory (e.g. Resident Evil 2 never starts its intro FMV); such games force this on automatically.",
        NULL,
        "video",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-parallel-rdp-overscan",
        "ParaLLEl: Crop Overscan",
        NULL,
        "Crop overscan border pixels.",
        NULL,
        "video",
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
#endif
    {
        CORE_NAME "-angrylion-vioverlay",
        "Angrylion: VI Overlay",
        NULL,
        "Accuracy and performance related post-processings.",
        NULL,
        "video",
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
        CORE_NAME "-angrylion-deinterlace",
        "Angrylion: Deinterlacing",
        NULL,
        "How interlaced (480i) content is displayed. 'Weave' merges both fields (sharp but combs on motion), 'Bob' line-doubles the current field (no combing, slight shimmer). Affects 480i games like Kuru Kuru Fever or Hanabi de Doon and hi-res title screens.",
        NULL,
        "video",
        {
            { "weave", "Weave" },
            { "bob", "Bob" },
            { NULL, NULL },
        },
        "weave"
    },
    {
        CORE_NAME "-dithering",
        "Angrylion: Dithering",
        NULL,
        "Make Mach bands less apparent with 16-bit pixels.",
        NULL,
        "video",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-angrylion-sync",
        "Angrylion: Thread Sync Level",
        NULL,
        "Sacrifice accuracy for performance.",
        NULL,
        "video",
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
        "Angrylion: Multithreading",
        NULL,
        "'OFF' renders synchronously on the emulator thread: no worker pool, no command buffering, no thread synchronization. Numeric values use the threaded renderer with that many worker threads ('1' keeps the threaded pipeline with a single worker). 'All threads' uses the host's physical cores, at most 8. Every worker replays the whole command stream, so past 8 the duplicated work outweighs what more workers take off the spans, and a second thread on the same core only adds contention.",
        NULL,
        "video",
        {
            { "off", "disabled" },
            { "all threads", "All threads" },
            { "1", NULL },
            { "2", NULL },
            { "3", NULL },
            { "4", NULL },
            { "5", NULL },
            { "6", NULL },
            { "7", NULL },
            { "8", NULL },
            { NULL, NULL },
        },
        "all threads"
    },
    {
        CORE_NAME "-angrylion-overscan",
        "Angrylion: Crop Overscan",
        NULL,
        "Crop overscan border pixels.",
        NULL,
        "video",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "disabled"
    },
    {
        CORE_NAME "-gfxplugin-accuracy",
        "Glide64: GFX Accuracy",
        NULL,
        NULL,
        NULL,
        "video",
        {
            { "low", "Low" },
            { "medium", "Medium" },
            { "high", "High" },
            { "veryhigh", "Very High" },
            { NULL, NULL },
        },
#if defined(IOS) || defined(ANDROID)
        "medium"
#else
        "veryhigh"
#endif
    },
    {
        CORE_NAME "-filtering",
        "Glide64: Texture Filtering",
        NULL,
        NULL,
        NULL,
        "video",
        {
            { "automatic", "Auto" },
            { "N64 3-point", NULL },
            { "bilinear", "Bilinear" },
            { "nearest", "Nearest" },
            { NULL, NULL },
        },
        "automatic"
    },
    {
        CORE_NAME "-polyoffset-factor",
        "Glide64: Polygon Offset Factor",
        NULL,
        NULL,
        NULL,
        "video",
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
        "Glide64: Polygon Offset Units",
        NULL,
        NULL,
        NULL,
        "video",
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
        CORE_NAME "-gliden64-viewport-hack",
        "GLideN64: Widescreen Hack",
        NULL,
        "Hack that adjusts the viewport to allow unstretched 16:9",
        NULL,
        "video",
        {
            {"disabled", NULL},
            {"enabled", NULL},
            {"steamdeck", "Steam Deck (16:10)"},
            { NULL, NULL },
        },
        "disabled"
    },
    {
        CORE_NAME "-gliden64-EnableNativeResFactor",
        "GLideN64: Native Resolution Factor",
        NULL,
        "Render at N times the native resolution.",
        NULL,
        "video",
        {
            {"0", "disabled"},
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
        "GLideN64: Bilinear Filtering Mode",
        NULL,
        "Select a Bilinear filtering method, 3point is the original system specific way.",
        NULL,
        "video",
        {
            {"standard", "Standard"},
            {"3point", "3-point"},
            { NULL, NULL },
        },
        "standard"
    },
#ifndef HAVE_OPENGLES2
    {
        CORE_NAME "-gliden64-MultiSampling",
        "GLideN64: MSAA Level",
        NULL,
        "Anti-Aliasing level.",
        NULL,
        "video",
        {
            {"0", "disabled"},
            {"2", "2x"},
            {"4", "4x"},
            {"8", "8x"},
            {"16", "16x"},
            { NULL, NULL },
        },
        "0"
    },
#endif
    {
        CORE_NAME "-gliden64-FXAA",
        "GLideN64: FXAA",
        NULL,
        "Fast Approximate Anti-Aliasing shader, moderately blur textures.",
        NULL,
        "video",
        {
            {"0", "disabled"},
            {"1", "enabled"},
            { NULL, NULL },
        },
        "0"
    },
    {
        CORE_NAME "-gliden64-EnableLODEmulation",
        "GLideN64: LOD Emulation",
        NULL,
        "Calculate per-pixel level of details to select texture mip levels and blend them with each other using LOD fraction.",
        NULL,
        "video",
        {
            {"False", "disabled"},
            {"True", "enabled"},
            { NULL, NULL },
        },
        "True"
    },
    {
        CORE_NAME "-gliden64-EnableFBEmulation",
        "GLideN64: Framebuffer Emulation",
        NULL,
        "Frame/depth buffer emulation. Disabling it can shorten input lag for particular games, but also break some special effects.",
        NULL,
        "video",
        {
            {"False", "disabled"},
            {"True", "enabled"},
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
        "GLideN64: Copy Auxiliary Buffers to RDRAM",
        NULL,
        "Copy auxiliary buffers to RDRAM. Fixes some game artifacts like Paper Mario intro.",
        NULL,
        "video",
        {
            {"False", "disabled"},
            {"True", "enabled"},
            { NULL, NULL },
        },
        "False",
    },
    {
        CORE_NAME "-gliden64-EnableCopyColorToRDRAM",
        "GLideN64: Color Buffer to RDRAM",
        NULL,
        "Color buffer copy to RDRAM. 'OFF' trades compatibility for performance.",
        NULL,
        "video",
        {
            {"Off", "disabled"},
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
        CORE_NAME "-gliden64-EnableCopyColorFromRDRAM",
        "GLideN64: Color Buffer From RDRAM",
        NULL,
        "Always copy the color buffer from RDRAM. GLideN64 already does this by itself whenever it detects that the CPU painted the frame the VI is about to show, so this setting only adds a second, unconditional trigger for the titles that detection misses (Donkey Kong 64 anti-aliased shadows, some Indiana Jones effects). Needs Framebuffer Emulation on. Default off for the performance cost.",
        NULL,
        "video",
        {
            {"False", "disabled"},
            {"True", "enabled"},
            { NULL, NULL },
        },
        "False"
    },
    {
        CORE_NAME "-gliden64-EnableCopyDepthToRDRAM",
        "GLideN64: Depth Buffer to RDRAM",
        NULL,
        "Depth buffer copy to RDRAM (Off will trade compatibility for Performance).",
        NULL,
        "video",
        {
            {"Off", "disabled"},
            {"Software", NULL},
            {"FromMem", NULL},
            { NULL, NULL },
        },
        "Software"
    },
    {
        CORE_NAME "-gliden64-GLideN64IniBehaviour",
        "GLideN64: Per-game Settings",
        NULL,
        "When to apply GLideN64's built-in per-game settings. 'Late' lets them override your choices below, 'Early' lets your choices win, 'OFF' ignores them entirely.",
        NULL,
        "video",
        {
            {"disabled", "disabled"},
            {"late", "Late"},
            {"early", "Early"},
            { NULL, NULL },
        },
        "late"
    },
    {
        CORE_NAME "-gliden64-BackgroundMode",
        "GLideN64: Background Mode",
        NULL,
        "Render backgrounds mode (HLE only). One piece (fast), Stripped (precise).",
        NULL,
        "video",
        {
            {"Stripped", NULL},
            {"OnePiece", NULL},
            { NULL, NULL },
        },
        "OnePiece"
    },
    {
        CORE_NAME "-gliden64-EnableHWLighting",
        "GLideN64: Hardware Per-pixel Lighting",
        NULL,
        "Standard per-vertex lighting when disabled. Slightly different rendering.",
        NULL,
        "video",
        {
            {"False", "disabled"},
            {"True", "enabled"},
            { NULL, NULL },
        },
        "False"
    },
    {
        CORE_NAME "-gliden64-CorrectTexrectCoords",
        "GLideN64: Continuous Texrect Coords",
        NULL,
        "Make texrect coordinates continuous to avoid black lines between them.",
        NULL,
        "video",
        {
            {"Off", "disabled"},
            {"Auto", NULL},
            {"Force", NULL},
            { NULL, NULL },
        },
        "Off"
    },
    {
        CORE_NAME "-gliden64-EnableInaccurateTextureCoordinates",
        "GLideN64: Enable Inaccurate Texture Coordinates",
        NULL,
        "Enables inaccurate texture coordinate calculations. This can improve performance and texture pack compatibity at the cost of accuracy.",
        NULL,
        "video",
        {
            {"False", "disabled"},
            {"True", "enabled"},
            { NULL, NULL },
        },
        "False"
    },
    {
        CORE_NAME "-gliden64-EnableNativeResTexrects",
        "GLideN64: Native Resolution 2D Texrects",
        NULL,
        "Render 2D texrects in native resolution to fix misalignment between parts of 2D image (example: Mario Kart driver selection portraits).",
        NULL,
        "video",
        {
            {"Disabled", "disabled"},
            {"Unoptimized", NULL},
            {"Optimized", NULL},
            { NULL, NULL },
        },
        "Disabled"
    },
    {
        CORE_NAME "-gliden64-EnableLegacyBlending",
        "GLideN64: Less Accurate Blending Mode",
        NULL,
        "Do not use shaders to emulate N64 blending modes. Works faster on slow GPU. Can cause glitches.",
        NULL,
        "video",
        {
            {"False", "disabled"},
            {"True", "enabled"},
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
        "GLideN64: GPU Shader Depth Write",
        NULL,
        "Enable writing of fragment depth. Some mobile GPUs do not support it, thus it's optional. Leave enabled.",
        NULL,
        "video",
        {
            {"False", "disabled"},
            {"True", "enabled"},
            { NULL, NULL },
        },
/* GLES2 only. GLSL ES 1.00 has no gl_FragDepth without GL_EXT_frag_depth, and
 * GLideN64 declines to emit writeDepth() there at all, so the setting cannot
 * work on GLES2. GLSL ES 3.00 makes gl_FragDepth core, and a GLES 3.x target is
 * no less capable than the desktop one this already defaults to True for. */
#ifdef HAVE_OPENGLES2
        "False"
#else
        "True"
#endif
    },
    {
        CORE_NAME "-gliden64-EnableN64DepthCompare",
        "GLideN64: N64 Depth Compare",
        NULL,
        "Enable N64 depth compare instead of OpenGL standard one. Experimental, Fast mode will have more glitches.",
        NULL,
        "video",
        {
            {"False", "disabled"},
            {"True", "Fast"},
            {"Compatible", NULL},
        },
        "False"
    },
#if !defined(VC) && !defined(HAVE_OPENGLES2)
    {
        CORE_NAME "-gliden64-EnableShadersStorage",
        "GLideN64: Cache GPU Shaders",
        NULL,
        "Use persistent storage for compiled shaders.",
        NULL,
        "video",
        {
            {"False", "disabled"},
            {"True", "enabled"},
            { NULL, NULL },
        },
        "True"
    },
#endif
    {
        CORE_NAME "-gliden64-EnableTextureCache",
        "GLideN64: Cache Textures",
        NULL,
        "Save texture cache to hard disk.",
        NULL,
        "video",
        {
            {"False", "disabled"},
            {"True", "enabled"},
            { NULL, NULL },
        },
        "True"
    },
    {
        CORE_NAME "-gliden64-EnableOverscan",
        "GLideN64: Overscan",
        NULL,
        "Crop black borders from the overscan region around the screen.",
        NULL,
        "video",
        {
            {"Disabled", "disabled"},
            {"Enabled", "enabled"},
            { NULL, NULL },
        },
        "Disabled"
    },
    {
        CORE_NAME "-gliden64-OverscanTop",
        "GLideN64: Overscan Offset (Top)",
        NULL,
        "Overscan top offset.",
        NULL,
        "video",
        { OVERSCAN_VALUES },
        "0"
    },
    {
        CORE_NAME "-gliden64-OverscanLeft",
        "GLideN64: Overscan Offset (Left)",
        NULL,
        "Overscan left offset.",
        NULL,
        "video",
        { OVERSCAN_VALUES },
        "0"
    },
    {
        CORE_NAME "-gliden64-OverscanRight",
        "GLideN64: Overscan Offset (Right)",
        NULL,
        "Overscan right offset.",
        NULL,
        "video",
        { OVERSCAN_VALUES },
        "0"
    },
    {
        CORE_NAME "-gliden64-OverscanBottom",
        "GLideN64: Overscan Offset (Bottom)",
        NULL,
        "Overscan bottom offset.",
        NULL,
        "video",
        { OVERSCAN_VALUES },
        "0"
    },
    {
        CORE_NAME "-gliden64-txFilterMode",
        "GLideN64: Texture Filter",
        NULL,
        "Select Texture Filtering mode.",
        NULL,
        "video",
        {
            {"None", "disabled"},
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
        "GLideN64: Texture Enhancement",
        NULL,
        "Various texture filters.",
        NULL,
        "video",
        {
            {"None", "disabled"},
            {"As Is", "Cached"},
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
        "GLideN64: Don't Filter Background Textures",
        NULL,
        "Ignore filtering for background textures.",
        NULL,
        "video",
        {
            {"False", "disabled"},
            {"True", "enabled"},
            { NULL, NULL },
        },
        "True"
    },
    {
        CORE_NAME "-gliden64-txHiresEnable",
        "GLideN64: Use High-Res Textures",
        NULL,
        "Enable high-res texture packs if available.",
        NULL,
        "video",
        {
            {"False", "disabled"},
            {"True", "enabled"},
            { NULL, NULL },
        },
        "False"
    },
    {
        CORE_NAME "-gliden64-txCacheCompression",
        "GLideN64: Use High-Res Texture Cache Compression",
        NULL,
        "Compress created texture caches.",
        NULL,
        "video",
        {
            {"False", "disabled"},
            {"True", "enabled"},
            { NULL, NULL },
        },
        "True"
    },
    {
        CORE_NAME "-gliden64-txHiresFullAlphaChannel",
        "GLideN64: Use High-Res Full Alpha Channel",
        NULL,
        "This should be enabled unless it's an old RICE texture pack.",
        NULL,
        "video",
        {
            {"False", "disabled"},
            {"True", "enabled"},
            { NULL, NULL },
        },
        "True"
    },
    {
        CORE_NAME "-gliden64-EnableHiResAltCRC",
        "GLideN64: Use Alternative Method for High-Res Checksums",
        NULL,
        "Use an alternative method for High-Res paletted textures CRC calculations.",
        NULL,
        "video",
        {
            {"False", "disabled"},
            {"True", "enabled"},
            { NULL, NULL },
        },
        "False"
    },
    {
        CORE_NAME "-gliden64-IniBehaviour",
        "GLideN64: INI Behaviour",
        NULL,
        "Specifies INI Settings behaviour. This should really only contain essential options. Changing this can and will break ROM's, if the correct options aren't set manually. Some options may only be set via INI (fbInfoDisabled).",
        NULL,
        "video",
        {
            { "disabled", "Disable INI" },
            { "late", "Prioritize INI over Core Options" },
            { "early", "Prioritize Core Options over INI" },
            { NULL, NULL },
        },
        "late"
    },
    {
        CORE_NAME "-gliden64-LegacySm64ToolsHacks",
        "GLideN64: Patch SM64 Hacks Made With SM64 Editor",
        NULL,
        "Make plugin behave like Jabo in certain case to fix compatibility with legacy SM64 hacks. It adds undocumented behavior that may interfere with real N64 games (unlikely).",
        NULL,
        "video",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-gliden64-RemoveFBBlackBars",
        "GLideN64: Fix VI Resolution",
        NULL,
        "Remove the black bars added by VI emulation of the FrameBuffer.",
        NULL,
        "video",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-send_allist_to_hle_rsp",
        "Audio Processing",
        NULL,
        "Which RSP runs the audio microcode, independently of the one running graphics. 'Same as RSP Plugin' follows the RSP Plugin setting. 'Fast (HLE)' emulates audio lists at a high level instead of cycle-by-cycle, which is faster but glitches in some games. 'Accurate (LLE)' keeps audio cycle-accurate even when graphics run on the HLE RSP, which is the pairing to use if you want HLE speed for graphics without HLE audio glitches.",
        NULL,
        "audio",
        {
            { "disabled", "Same as RSP Plugin" },
            { "hle",      "Fast (HLE)" },
            { "accurate", "Accurate (LLE)" },
            { NULL, NULL },
        },
        "disabled"
    },
    {
        CORE_NAME "-enhanced-hle-audio",
        "Enhanced RSP HLE Audio",
        NULL,
        "Replace the audio microcode's four-tap voice interpolator with a windowed-sinc resampler. The microcode's kernel cannot band-limit a sample that is played back faster than it was recorded, so its image folds back into the audible range - the grain and grit on high-pitched sounds. This removes most of that and is slightly less muffled besides. Only affects the HLE RSP audio path. Deliberately not bit-accurate to the hardware, so leave it off if you want the exact output the console produced.",
        NULL,
        "audio",
        {
            { "disabled", NULL },
            { "enabled",  NULL },
            { NULL, NULL },
        },
        "disabled"
    },
    {
        CORE_NAME "-enhanced-hle-audio-quality",
        "Enhanced RSP HLE Audio Quality",
        NULL,
        "How many taps the enhanced resampler uses. More taps reject more of the folded-back image and cost a little more CPU and memory: 16 taps is already a large improvement over the microcode's four, 32 and 64 taps tighten the filter further. Has no effect unless Enhanced RSP HLE Audio is enabled.",
        NULL,
        "audio",
        {
            { "16", "16 taps" },
            { "32", "32 taps" },
            { "64", "64 taps" },
            { NULL, NULL },
        },
        "32"
    },
    {
        CORE_NAME "-pak1",
        "Player 1 Pak",
        NULL,
        "Controller Pak for port 1.",
        NULL,
        "input",
        { CONTROLLER_PAK_VALUES },
        "none"
    },
    {
        CORE_NAME "-pak2",
        "Player 2 Pak",
        NULL,
        "Controller Pak for port 2.",
        NULL,
        "input",
        { CONTROLLER_PAK_VALUES },
        "none"
    },
    {
        CORE_NAME "-pak3",
        "Player 3 Pak",
        NULL,
        "Controller Pak for port 3.",
        NULL,
        "input",
        { CONTROLLER_PAK_VALUES },
        "none"
    },
    {
        CORE_NAME "-pak4",
        "Player 4 Pak",
        NULL,
        "Controller Pak for port 4.",
        NULL,
        "input",
        { CONTROLLER_PAK_VALUES },
        "none"
    },
    {
        CORE_NAME "-alt-map",
        "Independent C-button Controls",
        NULL,
        "Make C-buttons as separate buttons instead of behind R2 toggle. Restart required.",
        NULL,
        "input",
        {
            { "disabled", NULL },
            { "enabled", NULL },
            { NULL, NULL },
        },
        "disabled"
    },
    {
        CORE_NAME "-astick-deadzone",
        "Analog Deadzone",
        NULL,
        "How far the analog stick must be pushed for the input to register.",
        NULL,
        "input",
        {
            { "0", "0%" },
            { "5", "5%" },
            { "10", "10%" },
            { "15", "15%" },
            { "20", "20%" },
            { "25", "25%" },
            { "30", "30%" },
            { NULL, NULL },
        },
        "15"
    },
    {
        CORE_NAME "-astick-sensitivity",
        "Analog Sensitivity",
        NULL,
        "How far the analog stick can be pushed.",
        NULL,
        "input",
        {
            { "50", "50%" },
            { "55", "55%" },
            { "60", "60%" },
            { "65", "65%" },
            { "70", "70%" },
            { "75", "75%" },
            { "80", "80%" },
            { "85", "85%" },
            { "90", "90%" },
            { "95", "95%" },
            { "100", "100%" },
            { "105", "105%" },
            { "110", "110%" },
            { "115", "115%" },
            { "120", "120%" },
            { "125", "125%" },
            { "130", "130%" },
            { "135", "135%" },
            { "140", "140%" },
            { "145", "145%" },
            { "150", "150%" },
            { "200", "200%" },
            { NULL, NULL },
        },
        "100"
    },
    {
        CORE_NAME "-astick-snap-angle-active",
        "Snap Controller Angle",
        NULL,
        "Snap analog stick angle to multiples of 45 degrees, to better support circular design controllers that should behave like classic N64 controllers.",
        NULL,
        "input",
        {
            { "disabled", NULL },
            { "enabled",  NULL },
            { NULL, NULL },
        },
        "disabled"
    },
    {
        CORE_NAME "-astick-snap-max-angle",
        "Snap Maximum Angle",
        NULL,
        "Maximum deviation from a 45-degree multiple at which snapping is applied (e.g. value of 5 means 85 to 95 degrees are snapped to 90).",
        NULL,
        "input",
        {
            { "1",  NULL },
            { "2",  NULL },
            { "3",  NULL },
            { "4",  NULL },
            { "5",  NULL },
            { "6",  NULL },
            { "7",  NULL },
            { "8",  NULL },
            { "9",  NULL },
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
            { NULL, NULL },
        },
        "15"
    },
    {
        CORE_NAME "-astick-snap-min-displacement-percent",
        "Snap Minimum Displacement",
        NULL,
        "Percentage of stick displacement from the center required before snapping activates (0 means always).",
        NULL,
        "input",
        {
            { "0",  "0%" },
            { "10", "10%" },
            { "20", "20%" },
            { "30", "30%" },
            { "40", "40%" },
            { "50", "50%" },
            { "60", "60%" },
            { "65", "65%" },
            { "70", "70%" },
            { "75", "75%" },
            { "80", "80%" },
            { "85", "85%" },
            { "90", "90%" },
            { "95", "95%" },
            { NULL, NULL },
        },
        "70"
    },
    {
        CORE_NAME "-mouse-mode",
        "Mouse to Analog Stick",
        NULL,
        "Use mouse input to control the N64 analog stick with player 1. The mouse takes over whenever the real stick is neutral. Useful for FPS games.",
        NULL,
        "input",
        {
            {"False", "disabled"},
            {"True", "enabled"},
            { NULL, NULL },
        },
        "False"
    },
    {
        CORE_NAME "-mouse-sensitivity-x",
        "Mouse Sensitivity X (percent)",
        NULL,
        "Horizontal mouse sensitivity. Negative values invert the axis. Set to 0 to disable horizontal movement.",
        NULL,
        "input",
        {
            {"-500", NULL}, {"-400", NULL}, {"-300", NULL}, {"-250", NULL}, {"-200", NULL},
            {"-175", NULL}, {"-150", NULL}, {"-125", NULL}, {"-100", NULL}, {"-75", NULL}, {"-50", NULL},
            {"0", NULL},
            {"50", NULL}, {"75", NULL}, {"100", NULL}, {"125", NULL}, {"150", NULL},
            {"175", NULL}, {"200", NULL}, {"250", NULL}, {"300", NULL}, {"400", NULL}, {"500", NULL},
            { NULL, NULL },
        },
        "100"
    },
    {
        CORE_NAME "-mouse-sensitivity-y",
        "Mouse Sensitivity Y (percent)",
        NULL,
        "Vertical mouse sensitivity. Negative values invert the axis; the default is negative because positive mouse Y points down. Set to 0 for Doom-like horizontal-only control.",
        NULL,
        "input",
        {
            {"-500", NULL}, {"-400", NULL}, {"-300", NULL}, {"-250", NULL}, {"-200", NULL},
            {"-175", NULL}, {"-150", NULL}, {"-125", NULL}, {"-100", NULL}, {"-75", NULL}, {"-50", NULL},
            {"0", NULL},
            {"50", NULL}, {"75", NULL}, {"100", NULL}, {"125", NULL}, {"150", NULL},
            {"175", NULL}, {"200", NULL}, {"250", NULL}, {"300", NULL}, {"400", NULL}, {"500", NULL},
            { NULL, NULL },
        },
        "-100"
    },
    {
        CORE_NAME "-mouse-boost",
        "Mouse Small Movement Boost",
        NULL,
        "Amplifies small mouse movements to make fine aiming practical. Higher = more boost; at 10 the stick follows the square root of the motion.",
        NULL,
        "input",
        {
            {"0", "Off"}, {"1", NULL}, {"2", NULL}, {"3", NULL}, {"4", NULL},
            {"5", NULL}, {"6", NULL}, {"7", NULL}, {"8", NULL}, {"9", NULL}, {"10", NULL},
            { NULL, NULL },
        },
        "0"
    },
    {
        CORE_NAME "-mouse-left",
        "Mouse Left Button",
        NULL,
        "Map mouse left button to an N64 button.",
        NULL,
        "input",
        { MOUSE_MAP_VALUES },
        "Z"
    },
    {
        CORE_NAME "-mouse-right",
        "Mouse Right Button",
        NULL,
        "Map mouse right button to an N64 button.",
        NULL,
        "input",
        { MOUSE_MAP_VALUES },
        "A"
    },
    {
        CORE_NAME "-mouse-middle",
        "Mouse Middle Button",
        NULL,
        "Map mouse middle button to an N64 button.",
        NULL,
        "input",
        { MOUSE_MAP_VALUES },
        "None"
    },
    {
        CORE_NAME "-mouse-wheel-up",
        "Mouse Wheel Up",
        NULL,
        "Map mouse wheel up to an N64 button.",
        NULL,
        "input",
        { MOUSE_MAP_VALUES },
        "L"
    },
    {
        CORE_NAME "-mouse-wheel-down",
        "Mouse Wheel Down",
        NULL,
        "Map mouse wheel down to an N64 button.",
        NULL,
        "input",
        { MOUSE_MAP_VALUES },
        "R"
    },
#if defined(HAVE_GLIDEN64) && !defined(HAVE_OPENGLES2)
    {
        CORE_NAME "-vr-mode",
        "VR Mode",
        NULL,
        "'Stereoscopic' renders each eye separately for real depth. 'Immersive' needs a VR headset: the game fills the view and turning your head turns the game's camera; scenes that can't fill the view fall back to the screen. Both double the rendering work, turn on frame buffer emulation and need a frontend that supports video views. Resolution and aspect ratio apply per eye. Restart required.",
        NULL,
        "vr",
        {
            {"off", "disabled"},
            {"stereoscopic", "Stereoscopic"},
            {"immersive", "Immersive"},
            { NULL, NULL },
        },
        "off"
    },
    {
        CORE_NAME "-vr-separation",
        "VR Eye Separation",
        NULL,
        "Strength of the 3D effect: the parallax of distant objects, in thousandths of the screen width. Lower is more comfortable.",
        NULL,
        "vr",
        {
            {"5", NULL},
            {"10", NULL},
            {"15", NULL},
            {"20", NULL},
            {"25", NULL},
            {"30", NULL},
            {"35", NULL},
            {"40", NULL},
            {"50", NULL},
            {"60", NULL},
            {"80", NULL},
            {"100", NULL},
            {"120", NULL},
            { NULL, NULL },
        },
        "30"
    },
    {
        CORE_NAME "-vr-depth-boost",
        "VR Depth Boost",
        NULL,
        "Spreads deep scenes, such as race tracks, that would otherwise look flat near the horizon.",
        NULL,
        "vr",
        {
            {"0", "0%"},
            {"10", "10%"},
            {"20", "20%"},
            {"30", "30%"},
            {"40", "40%"},
            {"50", "50%"},
            {"60", "60%"},
            {"70", "70%"},
            {"80", "80%"},
            {"90", "90%"},
            {"100", "100%"},
            { NULL, NULL },
        },
        "0"
    },
    {
        CORE_NAME "-vr-immersive-world-scale",
        "VR Immersive World Size",
        NULL,
        "Immersive mode only. Shows the scene at a smaller angle, as if the game's camera had a narrower view. Head turns still match. Below 100% the picture no longer fills the view.",
        NULL,
        "vr",
        {
            {"30", "30%"},
            {"35", "35%"},
            {"40", "40%"},
            {"45", "45%"},
            {"50", "50%"},
            {"55", "55%"},
            {"60", "60%"},
            {"65", "65%"},
            {"70", "70%"},
            {"75", "75%"},
            {"80", "80%"},
            {"85", "85%"},
            {"90", "90%"},
            {"95", "95%"},
            {"100", "100%"},
            { NULL, NULL },
        },
        "100"
    },
    {
        CORE_NAME "-vr-immersive-hud-scale",
        "VR Immersive HUD Size",
        NULL,
        "Immersive mode only. How wide the HUD and overlays are spread. At 100% they cover as much of the view as the game's own camera, which can push them into the corners.",
        NULL,
        "vr",
        {
            {"30", "30%"},
            {"35", "35%"},
            {"40", "40%"},
            {"45", "45%"},
            {"50", "50%"},
            {"55", "55%"},
            {"60", "60%"},
            {"65", "65%"},
            {"70", "70%"},
            {"75", "75%"},
            {"80", "80%"},
            {"85", "85%"},
            {"90", "90%"},
            {"95", "95%"},
            {"100", "100%"},
            { NULL, NULL },
        },
        "100"
    },
    {
        CORE_NAME "-vr-immersive-distance",
        "VR Immersive Camera Distance",
        NULL,
        "Immersive mode only. Moves the camera back along its own axis, in percent of the nearest depth of the scene.",
        NULL,
        "vr",
        {
            {"0", "0%"},
            {"25", "25%"},
            {"50", "50%"},
            {"75", "75%"},
            {"100", "100%"},
            {"125", "125%"},
            {"150", "150%"},
            {"175", "175%"},
            {"200", "200%"},
            {"225", "225%"},
            {"250", "250%"},
            {"275", "275%"},
            {"300", "300%"},
            { NULL, NULL },
        },
        "0"
    },
    {
        CORE_NAME "-vr-aim-controller",
        "VR Laser Pointer Aim",
        NULL,
        "Aim with a VR motion controller like a laser pointer. This drives the N64 analog stick. Needs a headset session.",
        NULL,
        "vr",
        {
            {"off", "disabled"},
            {"left", "Left Controller"},
            {"right", "Right Controller"},
            { NULL, NULL },
        },
        "off"
    },
    {
        CORE_NAME "-vr-aim-output",
        "VR Laser Pointer Output",
        NULL,
        "The laser pointer drives the analog stick, or four configurable buttons. Only used while VR Laser Pointer Aim is set to a controller.",
        NULL,
        "vr",
        {
            {"stick", "Analog Stick (Pad 1)"},
            {"buttons", "Four Buttons (Pad 1)"},
            {"stick-p2", "Analog Stick (Pad 2)"},
            {"buttons-p2", "Four Buttons (Pad 2)"},
            { NULL, NULL },
        },
        "stick"
    },
    {
        CORE_NAME "-vr-aim-invert-y",
        "VR Laser Pointer Invert Y (Thumbstick mode only)",
        NULL,
        "Invert the vertical axis of laser pointer aim. Only used while VR Laser Pointer Aim is set to a controller.",
        NULL,
        "vr",
        {
            {"off", "disabled"},
            {"on", "enabled"},
            { NULL, NULL },
        },
        "off"
    },
    {
        CORE_NAME "-vr-aim-button-threshold",
        "VR Laser Pointer Button Threshold (Four Buttons mode only)",
        NULL,
        "Four Buttons output only. How far from the centre the pointer must be to press a direction, in percent of its range. Releases a little closer to the centre than it presses.",
        NULL,
        "vr",
        {
            {"20", "20%"}, {"30", "30%"}, {"40", "40%"}, {"50", "50%"},
            {"60", "60%"}, {"70", "70%"}, {"80", "80%"}, {"90", "90%"},
            { NULL, NULL },
        },
        "50"
    },
    {
        CORE_NAME "-vr-aim-button-up",
        "VR Laser Pointer Up Button",
        NULL,
        "Four Buttons output only. Button pressed while the pointer is above the threshold.",
        NULL,
        "vr",
        { VR_AIM_BUTTON_VALUES },
        "C-Up"
    },
    {
        CORE_NAME "-vr-aim-button-down",
        "VR Laser Pointer Down Button",
        NULL,
        "Four Buttons output only. Button pressed while the pointer is below the threshold.",
        NULL,
        "vr",
        { VR_AIM_BUTTON_VALUES },
        "C-Down"
    },
    {
        CORE_NAME "-vr-aim-button-left",
        "VR Laser Pointer Left Button",
        NULL,
        "Four Buttons output only. Button pressed while the pointer is left of the threshold.",
        NULL,
        "vr",
        { VR_AIM_BUTTON_VALUES },
        "C-Left"
    },
    {
        CORE_NAME "-vr-aim-button-right",
        "VR Laser Pointer Right Button",
        NULL,
        "Four Buttons output only. Button pressed while the pointer is right of the threshold.",
        NULL,
        "vr",
        { VR_AIM_BUTTON_VALUES },
        "C-Right"
    },
#endif
    {
        CORE_NAME "-aleck64-testmode",
        "Aleck64: Test Mode",
        "Test Mode",
        "Boot the arcade board's test menu instead of the game.",
        NULL,
        "aleck64",
        {
            {"disabled", NULL},
            {"enabled", NULL},
            { NULL, NULL },
        },
        "disabled"
    },
    {
        CORE_NAME "-aleck64-coinage",
        "Aleck64: Coinage",
        "Coinage",
        "Coins needed per credit (Star Soldier, Vivid Dolls, Hi Pai Paradise 1/2).",
        NULL,
        "aleck64",
        {
            {"1 Coin 1 Credit", NULL},
            {"1 Coin 2 Credits", NULL},
            {"1 Coin 3 Credits", NULL},
            {"1 Coin 4 Credits", NULL},
            {"2 Coins 1 Credit", NULL},
            {"3 Coins 1 Credit", NULL},
            {"4 Coins 1 Credit", NULL},
            {"5 Coins 1 Credit", NULL},
            { NULL, NULL },
        },
        "1 Coin 1 Credit"
    },
    {
        CORE_NAME "-aleck64-difficulty",
        "Aleck64: Difficulty",
        "Difficulty",
        "Game difficulty (Star Soldier, Vivid Dolls, Hi Pai Paradise 1/2).",
        NULL,
        "aleck64",
        {
            {"Normal", NULL},
            {"Easy", NULL},
            {"Hard", NULL},
            {"Hardest", NULL},
            { NULL, NULL },
        },
        "Normal"
    },
    {
        CORE_NAME "-aleck64-demosound",
        "Aleck64: Demo Sounds",
        "Demo Sounds",
        "Play sound in attract mode (Star Soldier, Hi Pai Paradise 1/2).",
        NULL,
        "aleck64",
        {
            {"enabled", NULL},
            {"disabled", NULL},
            { NULL, NULL },
        },
        "enabled"
    },
    {
        CORE_NAME "-aleck64-language",
        "Aleck64: Language (Star Soldier)",
        "Language (Star Soldier)",
        "In-game language for Star Soldier: Vanishing Earth.",
        NULL,
        "aleck64",
        {
            {"English", NULL},
            {"Japanese", NULL},
            { NULL, NULL },
        },
        "English"
    },
    {
        CORE_NAME "-aleck64-players",
        "Aleck64: Players (Star Soldier)",
        "Players (Star Soldier)",
        "Number of players supported by the cabinet.",
        NULL,
        "aleck64",
        {
            {"3", NULL},
            {"4", NULL},
            {"2", NULL},
            {"1", NULL},
            { NULL, NULL },
        },
        "3"
    },
    {
        CORE_NAME "-aleck64-joystick",
        "Aleck64: Joystick Type (Star Soldier)",
        "Joystick Type (Star Soldier)",
        "Cabinet joystick type.",
        NULL,
        "aleck64",
        {
            {"2D", NULL},
            {"3D", NULL},
            { NULL, NULL },
        },
        "2D"
    },
    {
        CORE_NAME "-aleck64-autolevel",
        "Aleck64: Auto Level (Star Soldier)",
        "Auto Level (Star Soldier)",
        "Auto level speed.",
        NULL,
        "aleck64",
        {
            {"Normal", NULL},
            {"Slow", NULL},
            {"Fast1", NULL},
            {"Fast2", NULL},
            { NULL, NULL },
        },
        "Normal"
    },
    {
        CORE_NAME "-aleck64-rapid",
        "Aleck64: Rapid Fire (Star Soldier)",
        "Rapid Fire (Star Soldier)",
        "Enable rapid fire.",
        NULL,
        "aleck64",
        {
            {"disabled", NULL},
            {"enabled", NULL},
            { NULL, NULL },
        },
        "disabled"
    },
    {
        CORE_NAME "-aleck64-extend",
        "Aleck64: Extend (Star Soldier)",
        "Extend (Star Soldier)",
        "Score for an extra life.",
        NULL,
        "aleck64",
        {
            {"30,000,000", NULL},
            {"50,000,000", NULL},
            {"70,000,000", NULL},
            {"None", NULL},
            { NULL, NULL },
        },
        "30,000,000"
    },
    {
        CORE_NAME "-aleck64-lives",
        "Aleck64: Lives (Vivid Dolls)",
        "Lives (Vivid Dolls)",
        "Number of lives.",
        NULL,
        "aleck64",
        {
            {"4", NULL},
            {"3", NULL},
            {"2", NULL},
            {"1", NULL},
            { NULL, NULL },
        },
        "4"
    },
    {
        CORE_NAME "-aleck64-freeplay",
        "Aleck64: Free Play (Hi Pai Paradise)",
        "Free Play (Hi Pai Paradise)",
        "Play without credits.",
        NULL,
        "aleck64",
        {
            {"disabled", NULL},
            {"enabled", NULL},
            { NULL, NULL },
        },
        "disabled"
    },
    {
        CORE_NAME "-aleck64-continue",
        "Aleck64: Allow Continue (Hi Pai Paradise)",
        "Allow Continue (Hi Pai Paradise)",
        "Allow continuing after a loss.",
        NULL,
        "aleck64",
        {
            {"disabled", NULL},
            {"enabled", NULL},
            { NULL, NULL },
        },
        "disabled"
    },
    {
        CORE_NAME "-aleck64-kuitan",
        "Aleck64: Kuitan (Hi Pai Paradise)",
        "Kuitan (Hi Pai Paradise)",
        "Mahjong kuitan rule.",
        NULL,
        "aleck64",
        {
            {"disabled", NULL},
            {"enabled", NULL},
            { NULL, NULL },
        },
        "disabled"
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
