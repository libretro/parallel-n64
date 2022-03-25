#!/bin/bash
# To be used on Manjaro with MinGW packages installed
platform=win64 \
ARCH=win32 \
MSYSTEM=MINGW64 \
AR=i686-w64-mingw32-ar \
AS=i686-w64-mingw32-as \
CC=i686-w64-mingw32-gcc \
CXX=i686-w64-mingw32-g++ \
WINDRES=i686-w64-mingw32-windres \
make WITH_DYNAREC=i686 HAVE_THR_AL=1 HAVE_PARALLEL=1 HAVE_PARALLEL_RSP=1 -j 12
strip --strip-unneeded ./parallel_n64_libretro.dll
