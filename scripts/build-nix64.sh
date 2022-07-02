#!/bin/bash
# To be used in the Dockerfile in this directory
make WITH_DYNAREC=x86_64 HAVE_THR_AL=1 HAVE_PARALLEL=1 HAVE_PARALLEL_RSP=1 -j `nproc`

# Docker setup:
# docker build -t parallel .

# Building:
# docker run -it --name pl-build parallel
# docker cp <path to parallel-n64 directory> pl-build:/developer
# <build parallel-n64 in docker>
# docker cp pl-build:/developer/parallel-n64/parallel_n64_libretro.so <path to output directory>
# <exit docker container>
# docker rm pl-build
