mupen64plus-libretro
====================

Based on recent version of mupen64plus hg (~2.0 rc2) and patches I originally made for mednafen-ps3.
Uses video plugins pulled from https://github.com/paulscode/mupen64plus-ae

The video plugin must be selected at compile time:
* make WITH_VIDEO=rice for gles2rice
* make WITH_VIDEO=glide for gles2glide64 (The default)
* make WITH_VIDEO=gln64 for gles2n64 ( gles2n64 does not produce video output at this time )

In order to run the video plugin's ini file must be copied into RetroArch's system directory:
* For gles2rice video: gles2rice/data/VideoRiceLinux.ini
* For gles2glide64: gles2glide64/data/Glide64mk2.ini

To enable a dynarec CPU core you must pass the WITH_DYNAREC value to make:
* make WITH_DYNAREC=x86 (untested)
* make WITH_DYNAREC=x86_64
* make WITH_DYNAREC=arm (untested, will compile on android but not iOS)

TODO:
* Allow video plugin to be selected at runtime
* Fixup arm dynarec for iOS
* Enable save states
* Make sure all filesystem access targets directories a libretro core would be expected to access
* Make core options for important settings
