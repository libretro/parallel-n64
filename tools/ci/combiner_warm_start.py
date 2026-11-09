#!/usr/bin/env python3
"""The GL plugins without an ubershader build known combiners at load.

The first session writes down the keys it meets. The second must build them
all at load: PARALLEL_N64_COMBINER_STRICT aborts if a draw has to compile.

  combiner_warm_start.py --core parallel_n64_libretro.so --host ./lrhost_gl [--roms krom-roms]
"""
import argparse, glob, os, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parallel_rdp_regress import fetch  # noqa: E402

ROMS = ["cube16", "rottri32", "shade32", "tlut16"]
PLUGINS = {"gln64": "gln64_*.keys"}
FRAMES = 40


def run(a, rom, plugin, sysdir, strict):
    env = dict(os.environ, EGL_PLATFORM="surfaceless", LRHOST_SYSTEM_DIR=sysdir)
    if strict:
        env["PARALLEL_N64_COMBINER_STRICT"] = "1"
    return subprocess.run([a.host, a.core, rom, str(FRAMES),
                           "parallel-n64-gfxplugin=" + plugin,
                           "parallel-n64-rspplugin=hle",
                           "parallel-n64-cpucore=cached_interpreter"],
                          env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                          timeout=120).returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--roms", default="krom-roms")
    a = ap.parse_args()
    failures = 0
    for plugin, pattern in PLUGINS.items():
        total = 0
        for name in ROMS:
            rom = fetch(a.roms, name)
            with tempfile.TemporaryDirectory() as sysdir:
                first = run(a, rom, plugin, sysdir, False)
                found = glob.glob(os.path.join(sysdir, "ParallelN64", "shaders", pattern))
                keys = 0
                if found:
                    with open(found[0]) as f:
                        keys = sum(1 for line in f if len(line) == 19)
                total += keys
                second = run(a, rom, plugin, sysdir, True)
                ok = first == 0 and second == 0
                print("%-8s %-10s %d keys, warm run %s" % (plugin, name, keys, "ok" if ok else "FAILED (%d)" % second))
                failures += not ok
        if total == 0:
            print("%-8s met no combiners: nothing was tested" % plugin)
            failures += 1
    print("%d failed" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
