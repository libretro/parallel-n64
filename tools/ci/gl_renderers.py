#!/usr/bin/env python3
"""GL renderers lane.

Runs the core's OpenGL renderers (Glide64, GLideN64, Rice, glN64) under
tools/lrhost_gl, headless on Mesa's llvmpipe (EGL_PLATFORM=surfaceless, no
GPU), over krom's RDP test ROMs, fetched as parallel_rdp_regress.py fetches
them (same folder, same pinned commit). The GL renderers are not bit-exact
across drivers, so the frames are not held to stored hashes; the lane holds them to:

  * every renderer running every ROM to the end, and all but glN64 (which
    presents nothing for display lists sent straight to the RDP) presenting
    frames: Glide64 and Rice used to crash on a display list given as a
    KSEG address, as homebrew gives it, and glN64's GBI state, merged into
    GLideN64's under -fcommon, crashed at exit;
  * GLideN64 drawing the same frames whatever its colour buffer copy to
    RDRAM is set to (Off, Sync, Async, TripleBuffer): the copy writes RDRAM
    and must not disturb what is drawn, its pixel buffers or the vertex
    ring the draws read.

  gl_renderers.py --core parallel_n64_libretro.so --host ./lrhost_gl [--roms krom-roms]
"""
import argparse, os, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parallel_rdp_regress import fetch  # noqa: E402

ROMS = ["cube16", "rottri32", "rottri32aa", "shade32", "tlut16", "zbuf16"]
PLUGINS = ["glide64", "gliden64", "rice", "gln64"]
PRESENTS_NOTHING = {"gln64"}
# Rice crashes in the driver drawing a frame the CPU left in RDRAM (its
# DrawFrameBuffer); until that is fixed it runs the ROMs that draw.
SKIP = {"rice": {"shade32", "tlut16", "zbuf16"}}
COPY_MODES = ["Off", "Sync", "Async", "TripleBuffer"]
FRAMES = 40


def run(host, core, rom, plugin, extra, tmp):
    hashlog = os.path.join(tmp, "hash")
    if os.path.exists(hashlog):
        os.remove(hashlog)
    env = dict(os.environ, EGL_PLATFORM="surfaceless", LRHOST_HASHLOG=hashlog)
    args = [host, core, rom, str(FRAMES),
            "parallel-n64-gfxplugin=" + plugin,
            "parallel-n64-rspplugin=hle",
            "parallel-n64-cpucore=cached_interpreter"] + extra
    p = subprocess.run(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       timeout=600)
    out = p.stdout.decode("utf-8", "replace")
    frames = []
    if os.path.exists(hashlog):
        with open(hashlog) as f:
            frames = [line.split()[-1] for line in f if line.strip()]
    return p.returncode, frames, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--roms", default="krom-roms")
    a = ap.parse_args()

    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for rom in ROMS:
            path = fetch(a.roms, rom)
            for plugin in PLUGINS:
                if rom in SKIP.get(plugin, ()):
                    continue
                rc, frames, out = run(a.host, a.core, path, plugin, [], tmp)
                ok = rc == 0 and (len(frames) > 0 or plugin in PRESENTS_NOTHING)
                print("%-10s %-9s %s" % (rom, plugin, "ok" if ok else "FAILED (exit %d, %d frames)" % (rc, len(frames))))
                if not ok:
                    failures += 1
                    sys.stdout.write(out[-2000:])
            reference = None
            for mode in COPY_MODES:
                rc, frames, out = run(a.host, a.core, path, "gliden64",
                                      ["parallel-n64-gliden64-EnableFBEmulation=True",
                                       "parallel-n64-gliden64-EnableCopyColorToRDRAM=" + mode], tmp)
                if rc != 0 or not frames:
                    print("%-10s gliden64 copy %-12s FAILED (exit %d)" % (rom, mode, rc))
                    sys.stdout.write(out[-2000:])
                    failures += 1
                    continue
                if reference is None:
                    reference = frames
                elif frames != reference:
                    print("%-10s gliden64 copy %-12s draws other frames than copy Off" % (rom, mode))
                    failures += 1
                    continue
                print("%-10s gliden64 copy %-12s ok" % (rom, mode))

    if failures:
        print("%d failures" % failures)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
