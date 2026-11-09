#!/usr/bin/env python3
"""GLideN64's Vulkan backend draws what its OpenGL backend draws.

The same content, frame by frame and pixel by pixel: the generated cartridge
of combine modes, and krom's ROMs that draw, on GL and on Vulkan, with every
combiner through the generic shader (generic) and with the combiners' own
pipelines (own). Any pixel off by more than one step fails.

  gliden64_vulkan.py --core parallel_n64_libretro.so --gl ./lrhost_gl --vk ./lrhost_vk [--roms krom-roms]
"""
import argparse, os, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gliden64_ubershader as cart

OPTIONS = ["parallel-n64-gfxplugin=gliden64", "parallel-n64-rspplugin=hle",
           "parallel-n64-cpucore=cached_interpreter"]
TOLERANCE = 1


def run(host, core, rom, tmp, name, frames, api, env_extra, options=()):
    # one directory, so that the pipeline cache warms up across the runs
    sysdir = os.path.join(tmp, "sys")
    dump = os.path.join(tmp, name)
    os.makedirs(sysdir, exist_ok=True)
    os.makedirs(dump, exist_ok=True)
    env = dict(os.environ, EGL_PLATFORM="surfaceless", LRHOST_SYSTEM_DIR=sysdir, LRHOST_DUMPDIR=dump, GLIDEN64_VULKAN_STRICT="1",
               LRHOST_DUMP_FROM="1", LRHOST_DUMP_TO=str(frames))
    env.update(env_extra)
    p = subprocess.run([host, core, rom, str(frames)] + OPTIONS + list(options) + ["parallel-n64-gliden64-API=" + api],
                       env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
    if p.returncode != 0:
        sys.stdout.write(p.stdout.decode("utf-8", "replace")[-2000:])
        raise RuntimeError("exit %d" % p.returncode)
    out = {}
    for f in sorted(os.listdir(dump)):
        if f.endswith(".raw"):
            with open(os.path.join(dump, f), "rb") as h:
                out[f] = h.read()
    return out


def worst(gl, vk):
    """Largest channel difference: GL's frames are BGRA, Vulkan's RGBA."""
    if len(gl) != len(vk):
        return 256
    m = 0
    for i in range(0, len(gl), 4):
        for j, k in ((0, 2), (1, 1), (2, 0)):
            d = abs(gl[i + j] - vk[i + k])
            if d > m:
                m = d
    return m


def compare(a, name, rom, frames, options=(), modes=(("generic", {"GLIDEN64_VULKAN": "generic"}), ("own", {}))):
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        gl = run(a.gl, a.core, rom, tmp, "gl", frames, "opengl", {}, options)
        if not gl:
            print("FAILED: %s presented nothing" % name)
            return 1
        for label, env in modes:
            vk = run(a.vk, a.core, rom, tmp, label, frames, "vulkan", env, options)
            bad = 0
            top = 0
            # The first presented frame is a dupe (nothing latched yet): GL's
            # host then reads its framebuffer, Vulkan's has no image to read.
            common = sorted(set(gl) & set(vk))[1:]
            if len(common) != len(gl) - 1:
                bad = abs(len(gl) - 1 - len(common))
            for f in common:
                w = worst(gl[f], vk[f])
                top = max(top, w)
                if w > TOLERANCE:
                    bad += 1
            print("%-10s %-8s %s (%d frames, worst channel %d)" % (name, label, "ok" if not bad else "DIFFERENT", len(gl), top))
            failures += 1 if bad else 0
    return failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--gl", required=True)
    ap.add_argument("--vk", required=True)
    ap.add_argument("--roms", default="krom-roms")
    ap.add_argument("--quick", action="store_true", help="the cartridge and one ROM, for sanitizer builds")
    a = ap.parse_args()
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        rom = os.path.join(tmp, "combiners0.z64")
        cart.cartridge(rom, 0)
        failures += compare(a, "cart0", rom, 8)
    for name in (cart.KROM[:1] if a.quick else cart.KROM):
        failures += compare(a, name, cart.fetch(a.roms, name), 20 if a.quick else 60)
    # GPUs without dynamic blending build a pipeline for each state instead
    if not a.quick:
        failures += compare(a, "static", cart.fetch(a.roms, "cube32"), 20,
                            modes=(("no-eds3", {"GLIDEN64_VULKAN_NO_EDS3": "1"}),))
    # what shapes the shaders and the targets
    for opt in () if a.quick else ("MultiSampling=4", "EnableN64DepthCompare=True", "BilinearMode=standard",
                "EnableCopyColorToRDRAM=Sync", "EnableCopyColorToRDRAM=Async"):
        failures += compare(a, opt.split("=")[0][:10], cart.fetch(a.roms, "cube32"), 8,
                            ["parallel-n64-gliden64-" + opt], (("own", {}),))
    print("%d failed" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
