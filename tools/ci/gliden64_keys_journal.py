#!/usr/bin/env python3
"""GLideN64: a crash does not lose the combiners a session met.

Keys reached the keys file only at a clean exit; now each is also written
to a journal as it is met (tools/test_combiner_journal.c covers writing it).
Here a session starts on a journal a crash left, with keys the ROM never
meets and no keys file: it must build them and, at a clean exit, have them
in the keys file and the journal gone.

  gliden64_keys_journal.py --core parallel_n64_libretro.so --host ./lrhost_gl [--roms krom-roms]
"""
import argparse, glob, os, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parallel_rdp_regress import fetch  # noqa: E402

ROM = "alphacov"
FLIP = 0x0000000011111111
OPTIONS = ["parallel-n64-gfxplugin=gliden64", "parallel-n64-rspplugin=hle",
           "parallel-n64-cpucore=cached_interpreter",
           "parallel-n64-gliden64-EnableShadersStorage=True"]


def run(a, rom, sysdir, frames):
    env = dict(os.environ, EGL_PLATFORM="surfaceless", LRHOST_SYSTEM_DIR=sysdir)
    p = subprocess.run([a.host, a.core, rom, str(frames)] + OPTIONS, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
    if p.returncode != 0:
        sys.stdout.write(p.stdout.decode("utf-8", "replace")[-2000:])
        raise RuntimeError("exit %d" % p.returncode)


def shaders(sysdir, suffix):
    found = glob.glob(os.path.join(sysdir, "ParallelN64", "shaders", "*." + suffix))
    return found[0] if found else None


def keys_in(path):
    with open(path) as f:
        return {l.strip()[2:] for l in f.read().split("\n")[3:] if l.strip()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--roms", default="krom-roms")
    a = ap.parse_args()
    rom = fetch(a.roms, ROM)
    with tempfile.TemporaryDirectory() as sysdir:
        run(a, rom, sysdir, 3)
        keys = shaders(sysdir, "keys")
        met = keys_in(keys)
        with open(keys) as f:
            version = int(f.readline().strip(), 16)
        # combiners the ROM does not use
        lost = {"%016x" % (int(k, 16) ^ FLIP) for k in met} - met
        for found in glob.glob(os.path.join(os.path.dirname(keys), "*")):
            os.remove(found)
        with open(keys + ".journal", "w") as f:
            f.write("gliden64-combiner-keys %08x\n" % version)
            for k in sorted(lost):
                f.write("%s 0\n" % k)
            f.write("0123456789ab")  # the line the crash cut short

        run(a, rom, sysdir, 3)
        kept = keys_in(keys)
        missing = lost - kept
        if missing:
            print("FAILED: %d of %d keys were lost" % (len(missing), len(lost)))
            return 1
        if os.path.exists(keys + ".journal"):
            print("FAILED: the journal is still there after a clean exit")
            return 1
        print("gliden64 keys journal: ok (%d keys from a crashed session)" % len(lost))
    return 0


if __name__ == "__main__":
    sys.exit(main())
