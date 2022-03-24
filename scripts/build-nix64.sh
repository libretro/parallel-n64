#!/bin/bash
# To be used on Ubuntu 18.04 with latest gcc installed (gcc11 at time of writing)
# Dockerfile sample can be found at https://git.libretro.com/libretro-infrastructure/libretro-build-amd64-ubuntu/-/blob/master/Dockerfile.xenial-gcc9
make WITH_DYNAREC=x86_64 HAVE_THR_AL=1 HAVE_PARALLEL=1 HAVE_PARALLEL_RSP=1 -j 12
