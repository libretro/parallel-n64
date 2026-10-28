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

The ROMs in UPSCALER_ROMS also run for a frontend temporal upscaler
(lrhost_vk's LRHOST_UPSCALER): the frames, their motion vectors and depth
must match "<rom>@upscaler" threaded and single-threaded alike and
whatever the options it overrides say, and pass tools/ci/upscaler_check.py,
which checks the jitter and, for the ROMs that move, that the motion
vectors take every changed pixel back to where the frame before had it.
The frame is black where the VI blanks its guard band at the sides (cube16
and rottri32 fill the framebuffer to its edges). The RDP's coverage
antialiasing stays on: rottri32aa's edges blend with the background
(without it the triangles of a face leave seam pixels between them),
alphacov's cut-outs through the coverage stay cut out, and the
transparency mask marks the triangles blended without depth (rottri32xlu,
rottri32 with FORCE_BLEND) and nothing else.
"""
import argparse, concurrent.futures, hashlib, json, os, subprocess, sys, urllib.request

import upscaler_check

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
# For a temporal upscaler asking for 640x480: a 320x240 framebuffer at 2x.
# Consecutive frames, for the motion vector check; True for the ROMs
# that move every frame (not rottri32aa: its blended edge pixels have no
# colour to find in the frame before).
UPSCALER_ROMS = {"rottri32": True, "cube16": True, "zbuf16": False, "alphacov": False, "rottri32xlu": True,
                 "rottri32aa": False}
# Those that blend a triangle without depth (FORCE_BLEND, no Z update):
# the transparency mask marks it; the others have none
UPSCALER_TRANSLUCENT = ("rottri32xlu",)
# A krom test with commands changed, run for the upscaler only:
# rottri32's moving triangle with FORCE_BLEND, and with AA_EN blending the
# blend colour over the memory colour by its coverage (IM_RD,
# ALPHA_CVG_SEL) on a background filled without coverage
DERIVED = {"rottri32xlu": ("rottri32", (("2f00283080000000", "2f00283080004000"),)),
           "rottri32aa": ("rottri32", (("2f00283080000000", "2f00283080402048"),
                                       ("37000000ffff00ff", "37000000ffff0000")))}
UPSCALER_FRAMES = (30, 36)
# The options the upscaler overrides change nothing: super-sampled
# read-back (SSAA), downsampling, the VI filters and the factor.
UPSCALER_MODES = {"upscaler": ({}, ()),
                  "upscaler-single-threaded": ({"PARALLEL_RDP_SINGLE_THREADED_COMMAND": "1"}, ()),
                  "upscaler-overridden": ({}, ("parallel-n64-upscaling=4x",
                                               "parallel-n64-parallel-rdp-super-sampled-read-back=enabled",
                                               "parallel-n64-parallel-rdp-downscaling=1/2",
                                               "parallel-n64-parallel-rdp-vi-aa=enabled",
                                               "parallel-n64-parallel-rdp-divot-filter=enabled",
                                               "parallel-n64-parallel-rdp-dither-filter=enabled"))}


def boot_crc(rom):
    """The two words of the header the boot code (CIC 6102) checks the
    first megabyte against"""
    m = 0xffffffff
    rom = rom + bytes(max(0, 0x101000 - len(rom)))
    t1 = t2 = t3 = t4 = t5 = t6 = 0xF8CA4DDC
    for i in range(0x1000, 0x101000, 4):
        d = int.from_bytes(rom[i:i + 4], "big")
        k = (t6 + d) & m
        if k < t6:
            t4 = (t4 + 1) & m
        t6 = k
        t3 ^= d
        s = d & 31
        r = ((d << s) | (d >> (32 - s))) & m if s else d
        t5 = (t5 + r) & m
        t2 = t2 ^ r if t2 > d else t2 ^ t6 ^ d
        t1 = (t1 + (t5 ^ d)) & m
    return ((t6 ^ t4 ^ t3) & m).to_bytes(4, "big") + ((t5 ^ t2 ^ t1) & m).to_bytes(4, "big")


def fetch(romdir, name):
    path = os.path.join(romdir, name + ".n64")
    if name in DERIVED and not os.path.exists(path):
        base, changes = DERIVED[name]
        with open(fetch(romdir, base), "rb") as f:
            data = f.read()
        if data[0x10:0x18] != boot_crc(data):
            raise RuntimeError("%s: %s's CRC is not CIC 6102's" % (name, base))
        for old, new in changes:
            if data.count(bytes.fromhex(old)) != 1:
                raise RuntimeError("%s: %s is not in %s once" % (name, old, base))
            data = data.replace(bytes.fromhex(old), bytes.fromhex(new))
        data = data[:0x10] + boot_crc(data) + data[0x18:]
        with open(path + ".part", "wb") as f:
            f.write(data)
        os.replace(path + ".part", path)
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


def coverage_aa(out, n):
    """alphacov draws its cases twice, the lower half with antialiasing on
    (226 lines below the upper), in columns 38 pixels wide from x = 10 and
    x = 320. For the upscaler as without it, the columns without
    CVG_TIMES_ALPHA (0, 2, 4, 6) come out the same in both halves (their
    edges lie on pixels), and those with it (1, 3, 5, 7) cut out their
    texels through the coverage in the lower half only."""
    with open(os.path.join(out, "%04d.raw.size" % n)) as f:
        w, h = map(int, f.read().split())
    with open(os.path.join(out, "%04d.raw" % n), "rb") as f:
        px = f.read()
    scale = w // 640

    def column(left, k, top):
        x0, x1 = (left + 38 * k) * scale * 4, (left + 38 * k + 38) * scale * 4
        return b"".join(px[y * w * 4 + x0:y * w * 4 + x1] for y in range(top * scale, (top + 220) * scale))

    errors = []
    for left in (10, 320):
        for k in range(8):
            same = column(left, k, 0) == column(left, k, 226)
            if same != (k % 2 == 0):
                errors.append("x=%d column %d %s" % (left, k, "cut-outs drawn opaque" if same else "edges antialiased"))
    return errors


def guard_band_black(out, n):
    """The VI blanks 8 output pixels at the left of its scanout and 7 at
    the right: with the ROMs' 640 pixels over a 320 pixel framebuffer, the
    frame's first 4 and last 3.5 framebuffer pixels are black, and the ones
    beside them, which the ROM filled, are not."""
    with open(os.path.join(out, "%04d.raw.size" % n)) as f:
        w, h = map(int, f.read().split())
    with open(os.path.join(out, "%04d.raw" % n), "rb") as f:
        px = f.read()
    scale = w // 320

    def black(x):
        return all(px[(y * w + x) * 4 + c] == 0 for y in range(0, h, 7) for c in range(3))

    left, right = 4 * scale, w - 3 * scale
    return all(black(x) for x in range(0, left)) and not black(left) \
        and all(black(x) for x in range(right, w)) and not black(right - 1)


def edges_blended(out, n):
    """rottri32aa's antialiased triangle is drawn over a background without
    coverage: its edge pixels blend the two colours, so the frame has
    more than the two."""
    with open(os.path.join(out, "%04d.raw" % n), "rb") as f:
        px = f.read()
    return len(set(px[i:i + 4] for i in range(0, len(px), 4))) > 2


def run_upscaler(args, name, rom, mode, env_extra, options, moving):
    out = os.path.join(args.out, name, mode)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        os.remove(os.path.join(out, f))
    log_path = os.path.join(out, "upscaler.txt")
    first, last = UPSCALER_FRAMES
    env = dict(os.environ, LRHOST_DUMPDIR=out, LRHOST_DUMP_FROM=str(first), LRHOST_DUMP_TO=str(last),
               LRHOST_DUMP_EVERY="1", LRHOST_UPSCALER="640,480", LRHOST_UPSCALER_LOG=log_path,
               LRHOST_VERBOSE="1", **env_extra)
    p = subprocess.run([args.host, args.core, rom, str(last + 1)] + OPTIONS + list(options), env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
    log = p.stdout.decode("utf-8", "replace")
    with open(os.path.join(out, "log.txt"), "w") as f:
        f.write(log)
    if p.returncode != 0:
        return None, "exit %d" % p.returncode
    if "asked for in retro_load_game 1," not in log:
        return None, "the core did not ask for the upscaler in retro_load_game"
    if "Temporal upscaler: drawing at 2x" not in log:
        return None, "the core did not say it draws for the upscaler"
    check = subprocess.run([sys.executable, upscaler_check.__file__, out, log_path, str(first), str(last)]
                           + (["--moving"] if moving else []), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    with open(os.path.join(out, "check.txt"), "wb") as f:
        f.write(check.stdout)
    if check.returncode != 0:
        return None, "upscaler_check.py: " + check.stdout.decode("utf-8", "replace").strip().splitlines()[-1]
    if name == "alphacov":
        errors = coverage_aa(out, first)
        if errors:
            return None, "coverage antialiasing: " + ", ".join(errors)
    if name == "rottri32aa" and not edges_blended(out, first):
        return None, "coverage antialiasing: the triangle's edges are not blended"
    if name in ("cube16", "rottri32") and not guard_band_black(out, first):
        return None, "the frame is not black where the VI blanks its guard band"
    for n in range(first, last + 1):
        path = os.path.join(out, "%04d.mask.raw" % n)
        if not os.path.exists(path):
            return None, "frame %d has no transparency mask" % n
        with open(path, "rb") as f:
            marked = sum(1 for b in f.read() if b)
        if (marked != 0) != (name in UPSCALER_TRANSLUCENT):
            return None, "frame %d: the transparency mask marks %d pixels" % (n, marked)
    hashes = {}
    for n in range(first, last + 1):
        h = hashlib.sha256()
        for part in ("raw", "mv.raw", "depth.raw", "mask.raw"):
            path = os.path.join(out, "%04d.%s" % (n, part))
            if not os.path.exists(path):
                return None, "frame %d has no %s" % (n, part)
            with open(path, "rb") as f:
                h.update(f.read())
        with open(os.path.join(out, "%04d.raw.size" % n)) as f:
            size = f.read().split()
        hashes[str(n)] = "%sx%s:%s" % (size[0], size[1], h.hexdigest()[:32])
    return hashes, None


def run_unfit(args, name, rom):
    """A request no factor of the framebuffer fits: the core says so
    and renders as usual, handing over nothing."""
    env = dict(os.environ, LRHOST_UPSCALER="1000,700,900,650,1100,750", LRHOST_VERBOSE="1")
    p = subprocess.run([args.host, args.core, rom, "5"] + OPTIONS, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
    log = p.stdout.decode("utf-8", "replace")
    out = os.path.join(args.out, name, "upscaler-unfit")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "log.txt"), "w") as f:
        f.write(log)
    if p.returncode != 0:
        return "exit %d" % p.returncode
    if "no factor of 320x240 fits" not in log or "frames with inputs 0" not in log:
        return "a request no factor fits was not said, or got inputs"
    return None


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
    for modes, options, ref, key in (() if name in DERIVED else
                                     ((MODES, (), "threaded", name),
                                      (UPSCALED_MODES, UPSCALED_OPTIONS, "2x", name + "@2x"))):
        group = {}
        for mode, env_extra in modes.items():
            hashes, err = run(args, name, rom, mode, env_extra, options)
            if err:
                failures.append("%s (%s): %s" % (name, mode, err))
                continue
            group[mode] = hashes
        compare(name, group, ref, key, args, expected, results, failures)
    if name in UPSCALER_ROMS:
        group = {}
        for mode, (env_extra, options) in UPSCALER_MODES.items():
            hashes, err = run_upscaler(args, name, rom, mode, env_extra, options, UPSCALER_ROMS[name])
            if err:
                failures.append("%s (%s): %s" % (name, mode, err))
                continue
            group[mode] = hashes
        compare(name, group, "upscaler", name + "@upscaler", args, expected, results, failures)
        if UPSCALER_ROMS[name]:
            err = run_unfit(args, name, rom)
            if err:
                failures.append("%s (upscaler-unfit): %s" % (name, err))
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
        # (the derived ROMs after the ones they come from, fetched by then)
        for name in ROMS:
            fetch(args.roms, name)
        jobs = [(name, pool.submit(check_rom, args, name, expected)) for name in list(ROMS) + list(DERIVED)]
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
