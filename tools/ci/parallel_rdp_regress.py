#!/usr/bin/env python3
"""parallel-rdp regression lane.

Runs the core with the parallel-rdp renderer under tools/lrhost_vk on any
Vulkan driver (CI uses Mesa's lavapipe, no GPU), over a set of krom's RDP
test ROMs fetched at a pinned commit, and compares a hash of every dumped
frame with tools/ci/parallel_rdp_expected.json. parallel-rdp is bit-exact
by design, so a frame that changes is a rendering change: either a
regression, or an intended fix whose expectations are refreshed with
--update in the same commit.

  parallel_rdp_regress.py --core parallel_n64_libretro.so --host ./lrhost_vk \\
                          [--roms dir] [--out artifacts] [--update]

Threaded and single-threaded command processing are both run and must
produce the same frames.
"""
import argparse, hashlib, json, os, subprocess, sys, urllib.request

KROM = "https://raw.githubusercontent.com/PeterLemon/N64/7085543e4a19d8c539fc9e0a4d2869e788b4ed4b/"
ROMS = {
    "cube16": "RDP/16BPP/Triangle/Cube/FillTriangle320x240/CubeFillTriangle16BPP320X240.N64",
    "cube32": "RDP/32BPP/Triangle/Cube/FillTriangle320x240/CubeFillTriangle32BPP320X240.N64",
    "rotline16": "RDP/16BPP/Line/Rotate/FillLine/RotateFillLine16BPP320X240.N64",
    "rottri32": "RDP/32BPP/Triangle/Rotate/FillTriangle/LeftMajorTriangle320x240/RotateLeftMajorTriangle32BPP320X240.N64",
    "rightmajor16": "RDP/16BPP/Triangle/Plot/FillTriangle/RightMajorTriangle320x240/RightMajorTriangle16BPP320X240.N64",
    "plotline32": "RDP/32BPP/Line/Plot/FillLine/PlotFillLine32BPP320X240.N64",
    # Drawn once at boot: black until parallel-RDP was started before the
    # guest's first frame.
    "shade32": "RDP/32BPP/Triangle/ShadeTriangle320x240/Cycle1ShadeTriangle32BPP320X240.N64",
    "zbuf16": "RDP/16BPP/Triangle/FillZBufferTriangle320x240/Cycle1FillZBufferTriangle16BPP320X240.N64",
    "tlut16": "RDP/16BPP/Rectangle/TextureRectangle/TLUT/CopyTextureRectangleTLUTRGBA8B320x240/CopyTextureRectangle16BPPTLUTRGBA8B320X240.N64",
}
FRAMES = 120
DUMP = (30, 60, 90, 120)
OPTIONS = ["parallel-n64-gfxplugin=parallel", "parallel-n64-rspplugin=hle",
           "parallel-n64-cpucore=cached_interpreter"]
MODES = {"threaded": {}, "single-threaded": {"PARALLEL_RDP_SINGLE_THREADED_COMMAND": "1"}}


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


def run(args, name, rom, mode, env_extra):
    out = os.path.join(args.out, name, mode)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        os.remove(os.path.join(out, f))
    env = dict(os.environ, LRHOST_DUMPDIR=out, LRHOST_DUMP_FROM=str(min(DUMP)),
               LRHOST_DUMP_TO=str(max(DUMP)), **env_extra)
    p = subprocess.run([args.host, args.core, rom, str(FRAMES)] + OPTIONS, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
    log = p.stdout.decode("utf-8", "replace")
    with open(os.path.join(out, "log.txt"), "w") as f:
        f.write(log)
    if p.returncode != 0:
        return None, "exit %d" % p.returncode
    hashes = {}
    for n in DUMP:
        raw = os.path.join(out, "%04d.raw" % n)
        if not os.path.exists(raw):
            return None, "frame %d was not presented" % n
        with open(raw, "rb") as f:
            data = f.read()
        with open(raw + ".size") as f:
            size = f.read().split()
        hashes[str(n)] = "%sx%s:%s" % (size[0], size[1], hashlib.sha256(data).hexdigest()[:32])
    return hashes, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--roms", default="krom-roms")
    ap.add_argument("--out", default="parallel-rdp-artifacts")
    ap.add_argument("--expected", default=os.path.join(os.path.dirname(__file__), "parallel_rdp_expected.json"))
    ap.add_argument("--update", action="store_true")
    args = ap.parse_args()

    expected = {}
    if os.path.exists(args.expected):
        with open(args.expected) as f:
            expected = json.load(f)

    results, failures = {}, []
    for name in ROMS:
        rom = fetch(args.roms, name)
        per_mode = {}
        for mode, env_extra in MODES.items():
            hashes, err = run(args, name, rom, mode, env_extra)
            if err:
                failures.append("%s (%s): %s" % (name, mode, err))
                continue
            per_mode[mode] = hashes
        if len(per_mode) == len(MODES) and per_mode["threaded"] != per_mode["single-threaded"]:
            failures.append("%s: threaded and single-threaded command processing differ" % name)
        if "threaded" in per_mode:
            results[name] = per_mode["threaded"]
            want = expected.get(name)
            if not args.update and want != per_mode["threaded"]:
                bad = [n for n in per_mode["threaded"] if not want or want.get(n) != per_mode["threaded"][n]]
                failures.append("%s: frames %s differ from the expectations" % (name, ", ".join(bad)))
        print("%-10s %s" % (name, "ok" if not any(f.startswith(name) for f in failures) else "FAIL"))

    if args.update:
        with open(args.expected, "w") as f:
            json.dump(results, f, indent=2, sort_keys=True)
            f.write("\n")
        print("wrote", args.expected)
    if failures:
        print("\n".join(failures))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
