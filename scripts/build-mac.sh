#!/bin/bash
sh ./scripts/clean.sh
sh ./scripts/build-mac-intel.sh
mv ./parallel_n64_libretro.dylib ./parallel_n64_libretro_x64.dylib

sh ./scripts/clean.sh
sh ./scripts/build-mac-arm.sh
mv ./parallel_n64_libretro.dylib ./parallel_n64_libretro_arm64.dylib

lipo -create -output ./parallel_n64_libretro.dylib ./parallel_n64_libretro_x64.dylib ./parallel_n64_libretro_arm64.dylib
rm ./parallel_n64_libretro_x64.dylib
rm ./parallel_n64_libretro_arm64.dylib
