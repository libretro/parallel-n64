#!/usr/bin/env python3
"""Glide64's frame copied to RDRAM: the same from Vulkan as from GL.

Games that read their frame back (Perfect Dark among them) have Glide64
copy the colour image to RDRAM after each display list: the window is
read back, at 1:1 straight, scaled up through a GPU blit to the N64's
size first, and each pixel converted to RDRAM's format as it is read. On
Vulkan the rows are read from where the GPU put them, lent until the
next read; nothing else holds them.

A cartridge named Perfect Dark hands the HLE RSP a display list each
frame; at the end the core's RDRAM is dumped, and the colour image there
must be what Glide64 on GL put there, byte for byte, with the window at
1:1, 2x and 4x the image.

  glide64_fb_copy.py --core parallel_n64_libretro.so --gl ./lrhost_gl --vk ./lrhost_vk
"""
import argparse, os, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gliden64_hle_dl as g  # noqa: E402

SIZES = ("320x240", "640x480", "1280x960")


def copied(host, core, rom, tmp, api, size):
    ram = os.path.join(tmp, "%s-%s.ram" % (api, size))
    g.run(os.path.abspath(host), core, rom, tmp, "opengl", "glide64", api, {"LRHOST_RAMDUMP": ram},
          options=["parallel-n64-screensize=" + size])
    with open(ram, "rb") as f:
        return f.read()[g.FB:g.FB + g.WIDTH * g.HEIGHT * 2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--gl", required=True)
    ap.add_argument("--vk", required=True)
    a = ap.parse_args()
    core = os.path.abspath(a.core)
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        rom = os.path.join(tmp, "copy.z64")
        g.cartridge(rom, 0, "Perfect Dark")
        for size in SIZES:
            gl = copied(a.gl, core, rom, tmp, "opengl", size)
            vk = copied(a.vk, core, rom, tmp, "vulkan", size)
            drawn = sum(1 for b in gl if b)
            off = sum(1 for x, y in zip(gl, vk) if x != y)
            ok = drawn > len(gl) // 10 and off == 0
            print("glide64 frame copied to RDRAM, window %-8s Vulkan as GL %s (%d of %d bytes differ, %d set)" % (
                size, "ok" if ok else "FAIL", off, len(gl), drawn))
            failures += 0 if ok else 1
    print("%d failed" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
