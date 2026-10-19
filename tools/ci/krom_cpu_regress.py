#!/usr/bin/env python3
"""Regression test: krom's CPU test ROMs on every CPU core.

Each ROM runs a set of instruction cases and draws the results on screen.
The frames listed in tools/ci/krom_cpu_expected.json were checked against
krom's real-hardware screenshots; every CPU core (both interpreters and
both recompilers) must draw exactly that frame, so a change in one core's
handling of an instruction shows up as a frame that differs.

Nothing here needs a GPU: the angrylion renderer and tools/lrhost are
software only. The ROMs are fetched from krom's repository at a pinned
commit.

  krom_cpu_regress.py --core parallel_n64_libretro.so --host ./lrhost \\
                      [--roms dir] [--out artifacts] [--update]

--update rewrites the expectations from the pure interpreter's frames; it
refuses when the cores disagree. Compare the saved frames with krom's
screenshots before committing the result.
"""
import argparse, hashlib, json, os, subprocess, sys, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
EXPECT = os.path.join(HERE, "krom_cpu_expected.json")
KROM = "https://raw.githubusercontent.com/PeterLemon/N64/7085543e4a19d8c539fc9e0a4d2869e788b4ed4b/"
FPU = "CPUTest/CP1/C/FPUCompare/FPUCompare-c.%s.s.N64"
ROMS = {"ll_lld_sc_scd": "CPUTest/CPU/LOADSTORE/LL_LLD_SC_SCD/LL_LLD_SC_SCD.N64"}
for _c in ("f", "un", "eq", "ueq", "olt", "ult", "ole", "ule",
           "sf", "ngle", "seq", "ngl", "lt", "nge", "le", "ngt"):
    ROMS["fpu_c_%s_s" % _c] = FPU % _c
FRAME = 90
CPU_CORES = ["pure_interpreter", "cached_interpreter",
             "dynamic_recompiler", "dynamic_recompiler_ari64"]
OPTIONS = ["parallel-n64-gfxplugin=angrylion", "parallel-n64-rspplugin=hle"]


def fetch(romdir, name):
    path = os.path.join(romdir, name + ".n64")
    if not os.path.exists(path):
        os.makedirs(romdir, exist_ok=True)
        try:
            with urllib.request.urlopen(KROM + ROMS[name], timeout=60) as r, open(path + ".part", "wb") as f:
                f.write(r.read())
        except Exception as e:
            # Some images ship a Python without a CA store; curl uses the system one.
            print("urllib fetch failed (%s), trying curl" % e)
            subprocess.run(["curl", "-fsSL", "--retry", "3", "-o", path + ".part", KROM + ROMS[name]], check=True)
        os.replace(path + ".part", path)
    return path


def run(args, name, rom, core):
    out = os.path.join(args.out, name, core)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        os.remove(os.path.join(out, f))
    env = dict(os.environ, LRHOST_DUMPDIR=out, LRHOST_DUMP_FROM=str(FRAME),
               LRHOST_DUMP_TO=str(FRAME))
    p = subprocess.run([args.host, args.core, rom, str(FRAME + 1)] + OPTIONS +
                       ["parallel-n64-cpucore=" + core], env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
    with open(os.path.join(out, "log.txt"), "w") as f:
        f.write(p.stdout.decode("utf-8", "replace"))
    if p.returncode != 0:
        return None, "exit %d" % p.returncode
    raw = os.path.join(out, "%04d.raw" % FRAME)
    if not os.path.exists(raw):
        return None, "frame %d was not presented" % FRAME
    with open(raw, "rb") as f:
        data = f.read()
    with open(raw + ".size") as f:
        size = f.read().split()
    return "%sx%s:%s" % (size[0], size[1], hashlib.sha256(data).hexdigest()[:32]), None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--roms", default="krom-roms")
    ap.add_argument("--out", default="krom-cpu-artifacts")
    ap.add_argument("--update", action="store_true")
    args = ap.parse_args()

    expected = {}
    if os.path.exists(EXPECT):
        with open(EXPECT) as f:
            expected = json.load(f)

    failures = 0
    got = {}
    for name in sorted(ROMS):
        rom = fetch(args.roms, name)
        frames = {}
        for core in CPU_CORES:
            h, err = run(args, name, rom, core)
            if err:
                print("FAIL %s %s: %s" % (name, core, err))
                failures += 1
                continue
            frames[core] = h
        got[name] = frames.get(CPU_CORES[0])
        want = None if args.update else expected.get(name)
        for core, h in frames.items():
            ref = want if want else got[name]
            status = "ok" if h == ref else "FAIL"
            if h != ref:
                failures += 1
            print("%-4s %-16s %-26s %s" % (status, name, core, h))

    if args.update:
        if failures:
            print("cores disagree; expectations not written")
            return 1
        with open(EXPECT, "w") as f:
            json.dump(got, f, indent=1, sort_keys=True)
            f.write("\n")
        print("wrote %s" % EXPECT)
        return 0

    if failures:
        print("krom_cpu_regress: %d failure(s); frames in %s" % (failures, args.out))
        return 1
    print("krom_cpu_regress: all frames match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
