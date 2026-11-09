#!/usr/bin/env python3
"""The GL plugins without an ubershader build known combiners at load.

The first session writes down the keys it meets. The second must build them
all at load: PARALLEL_N64_COMBINER_STRICT aborts if a draw has to compile.
Rice reaches no combiner on these ROMs, so its keys are written in here, which
tests that they build at load but not that a draw finds them.

  combiner_warm_start.py --core parallel_n64_libretro.so --host ./lrhost_gl [--roms krom-roms]
"""
import argparse, glob, os, random, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parallel_rdp_regress import fetch  # noqa: E402

ROMS = ["cube16", "rottri32", "shade32", "tlut16"]
PLUGINS = {"gln64": "gln64_*.keys", "rice": "rice_*.keys", "glide64": "glide64_*.keys"}
SEEDED = {"rice"}
FRAMES = 40


def run(a, rom, plugin, sysdir, strict):
    env = dict(os.environ, EGL_PLATFORM="surfaceless", LRHOST_SYSTEM_DIR=sysdir)
    if strict:
        env["PARALLEL_N64_COMBINER_STRICT"] = "1"
    return subprocess.run([a.host, a.core, rom, str(FRAMES),
                           "parallel-n64-gfxplugin=" + plugin,
                           "parallel-n64-rspplugin=hle",
                           "parallel-n64-cpucore=cached_interpreter"],
                          env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          timeout=120)


def seed(path, name):
    rnd = random.Random(name)
    with open(path, "a") as f:
        for flags in range(8):
            f.write("%016x %x\n" % (rnd.getrandbits(64), flags & 3))


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
                first = run(a, rom, plugin, sysdir, False).returncode
                found = glob.glob(os.path.join(sysdir, "ParallelN64", "shaders", pattern))
                keys = 0
                if found and plugin in SEEDED:
                    seed(found[0], name)
                if found and plugin == "glide64":
                    keys = 1 if os.path.getsize(found[0]) > 8 else 0
                elif found:
                    with open(found[0]) as f:
                        keys = sum(1 for line in f if len(line) == 19)
                total += keys
                warm = run(a, rom, plugin, sysdir, True)
                second = warm.returncode
                ok = first == 0 and second == 0 and b"Error " not in warm.stdout
                print("%-8s %-10s %d keys, warm run %s" % (plugin, name, keys, "ok" if ok else "FAILED (%d)" % second))
                failures += not ok
        if total == 0:
            print("%-8s met no combiners: nothing was tested" % plugin)
            failures += 1
    print("%d failed" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
