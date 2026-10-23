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

Threaded and single-threaded command processing are both run, as are a
frontend without Vulkan negotiation v2 (the core's v1 device) and the
shader variants for devices without 8-bit SSBO storage, and all must
produce the same frames; with v2 the core must create its device through
the frontend's wrapper. Each ROM is also rendered at 2x, with and without
8-bit storage, which must agree and match "<rom>@2x".
"""
import argparse, concurrent.futures, hashlib, json, os, subprocess, sys, urllib.request

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
    # 8bpp framebuffer, 8-bit TMEM/TLUT, tile mask/shift/mirror bytes, 8-bit
    # texels and coverage: the paths that differ without 8-bit storage.
    "pal8": "RDP/8BPP/Rectangle/TextureRectangle/TLUT/InternalPalette/InternalPalette.N64",
    "tlutb8": "RDP/LoadTLUT/LoadTLUT_b8_b8.N64",
    "maskcm": "RDP/TexturesMaskShiftMirror/TexturesMaskCMShiftT1_1.N64",
    "i8tri16": "RDP/16BPP/Triangle/TextureTriangle/Cycle1TextureTriangleI8B320x240/Cycle1TextureTriangle16BPPI8B320X240.N64",
    "alphacov": "RDP/AlphaCoverage/AlphaCoverage.N64",
}
FRAMES = 120
DUMP = (30, 60, 90, 120)
OPTIONS = ["parallel-n64-gfxplugin=parallel", "parallel-n64-rspplugin=hle",
           "parallel-n64-cpucore=cached_interpreter"]
MODES = {"threaded": {}, "single-threaded": {"PARALLEL_RDP_SINGLE_THREADED_COMMAND": "1"},
         # a frontend without Vulkan negotiation v2 gets the v1 device
         "negotiation-v1": {"LRHOST_VK_NEGOTIATION": "1"},
         # the shader variants for devices without 8-bit SSBO storage
         "no-8bit-storage": {"PARALLEL_RDP_NO_STORAGE_8BIT": "1"}}
# Rendered at 2x, which adds the upscaling domain passes: both must agree and
# match the "<rom>@2x" expectations.
UPSCALED_OPTIONS = ["parallel-n64-upscaling=2x"]
UPSCALED_MODES = {"2x": {}, "2x-no-8bit-storage": {"PARALLEL_RDP_NO_STORAGE_8BIT": "1"}}


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


def run(args, name, rom, mode, env_extra, options=()):
    out = os.path.join(args.out, name, mode)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        os.remove(os.path.join(out, f))
    # only the hashed frames are written
    env = dict(os.environ, LRHOST_DUMPDIR=out, LRHOST_DUMP_FROM=str(min(DUMP)),
               LRHOST_DUMP_TO=str(max(DUMP)), LRHOST_DUMP_EVERY=str(DUMP[1] - DUMP[0]), **env_extra)
    p = subprocess.run([args.host, args.core, rom, str(FRAMES)] + OPTIONS + list(options), env=env,
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


def compare(name, group, ref, key, args, expected, results, failures):
    """Every mode of a group must render what ref does, and ref must match
    the expectations under key."""
    for mode in group:
        if ref in group and group[mode] != group[ref]:
            failures.append("%s: %s and %s differ" % (name, mode, ref))
    if ref in group:
        results[key] = group[ref]
        want = expected.get(key)
        if not args.update and want != group[ref]:
            bad = [n for n in group[ref] if not want or want.get(n) != group[ref][n]]
            failures.append("%s: %s frames %s differ from the expectations" % (name, ref, ", ".join(bad)))


def check_rom(args, name, expected):
    rom = fetch(args.roms, name)
    results, failures = {}, []
    for modes, options, ref, key in ((MODES, (), "threaded", name),
                                     (UPSCALED_MODES, UPSCALED_OPTIONS, "2x", name + "@2x")):
        group = {}
        for mode, env_extra in modes.items():
            hashes, err = run(args, name, rom, mode, env_extra, options)
            if err:
                failures.append("%s (%s): %s" % (name, mode, err))
                continue
            group[mode] = hashes
        compare(name, group, ref, key, args, expected, results, failures)
    return results, failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--roms", default="krom-roms")
    ap.add_argument("--out", default="parallel-rdp-artifacts")
    ap.add_argument("--expected", default=os.path.join(os.path.dirname(__file__), "parallel_rdp_expected.json"))
    ap.add_argument("--update", action="store_true")
    # ROMs run side by side; each run is its own process
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    args = ap.parse_args()

    expected = {}
    if os.path.exists(args.expected):
        with open(args.expected) as f:
            expected = json.load(f)

    results, failures = {}, []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        jobs = [(name, pool.submit(check_rom, args, name, expected)) for name in ROMS]
        for name, job in jobs:
            rom_results, rom_failures = job.result()
            results.update(rom_results)
            failures += rom_failures
            print("%-10s %s" % (name, "FAIL" if rom_failures else "ok"), flush=True)

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
