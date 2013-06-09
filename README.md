mupen64plus-libretro
====================

DOES NOT WORK

Based on latest version of mupen64plus hg (~2.0 rc2) and patches I originally made for mednafen-ps3.
Emulation, audio, and input work.
video-rice is compiled in but still has many visual errors.

TODO:
* Audio sample rate is not set properly, a fixed value is used but in reality each game can set it to a different value. 
* The C buttons are not mapped, and the analog stick's logic is incorrect.
* There are many visual errors.
* The libco thread break needs to be moved. It must be placed where the original call to SDL_GL_SwapBuffers was. (Ideally libco would be removed completely).
