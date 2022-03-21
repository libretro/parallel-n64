#!/bin/bash
# To be used on Manjaro with MinGW packages installed
platform=win64 \
ARCH=x86_64 \
MSYSTEM=MINGW64 \
AR=x86_64-w64-mingw32-ar \
AS=x86_64-w64-mingw32-as \
CC=x86_64-w64-mingw32-gcc \
CXX=x86_64-w64-mingw32-g++ \
WINDRES=x86_64-w64-mingw32-windres \
make WITH_DYNAREC=x86_64 HAVE_THR_AL=1 HAVE_PARALLEL=1 HAVE_PARALLEL_RSP=1 -j 12
strip --strip-unneeded ./parallel_n64_libretro.dll
