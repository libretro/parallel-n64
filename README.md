mupen64plus-libretro
====================

Based on recent version of mupen64plus hg (~2.0 rc2) and patches I originally made for mednafen-ps3.
Uses video plugins pulled from https://github.com/paulscode/mupen64plus-ae

The video plugin must be selected at compile time:
* make WITH_RICE=1 for gles2rice
* make WITH_GLIDE=1 for gles2glide64
* make WITH_GLN64=1 for gles2n64 ( gles2n64 does not produce video output at this time )

In order to run the video plugins ini file must be copied into RetroArch's system directory:
* For gles2rice video: gles2rice/data/VideoRiceLinux.ini
* For gles2glide64: gles2glide64/data/Glide64mk2.ini

To enable a dynarec CPU core you must pass the WITH_DYNAREC value to make:
* make WITH_DYNAREC=x86 (untested)
* make WITH_DYNAREC=x86_64
* make WITH_DYNAREC=arm (untested, will compile on android but not iOS)

TODO:
* Allow video plugin to be selected at runtime
* Fixup arm dynarec for iOS

