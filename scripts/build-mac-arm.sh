#!/bin/bash
LIBRETRO_APPLE_PLATFORM=arm64-apple-macos11.0 \
CROSS_COMPILE=1 \
platform=osx \
ARCH=aarch64 \
CC=clang \
CXX=clang++ \
HAVE_NEON=1 \
make WITH_DYNAREC=aarch64 HAVE_THR_AL=1 HAVE_PARALLEL=1 HAVE_PARALLEL_RSP=1 -j12