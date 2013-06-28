mupen64plus-libretro
====================

DOES NOT WORK

Based on latest version of mupen64plus hg (~2.0 rc2) and patches I originally made for mednafen-ps3.
Emulation, audio, and input work.

By default it will build with a copy of gles2glide64 as the video plugin. By passing WITH_RICE=1 to make the rice video plugin will be built instead (this is not recommended).

video-rice is compiled in but still has many visual errors.

In order to run the video plugins ini file must be copied into RetroArch's system directory.
Use the file 'gles2glide64/data/Glide64mk2.ini' for gles2glide64 and 'mupen64plus-video-rice/data/VideoRiceLinux.ini' for rice video.

TODO:
* Audio sample rate is not set properly, a fixed value is used but in reality each game can set it to a different value. 
* There are many visual errors.
