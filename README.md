mupen64plus-libretro
====================

To enable a dynarec CPU core you must pass the WITH_DYNAREC value to make:
* make WITH_DYNAREC=x86
* make WITH_DYNAREC=x86_64
* make WITH_DYNAREC=arm

New make options:
* USE_CXD4_NEW - use the most recent version of CXD4 that was verified on Android
* USE_SSE2NEON - enable SSE2 vectorized routines on ARMv7+ via hacked SSE2NEON library

To build Android hardfp library with the new CXD4 RSP + NEON + Parallel RDP do:
* ndk-build -j8 USE_SSE2NEON=1 APP_ABI=armeabi-v7a-hard
