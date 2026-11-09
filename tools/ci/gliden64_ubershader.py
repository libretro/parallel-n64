#!/usr/bin/env python3
"""GLideN64's ubershader draws what a combiner's own program draws.

A cartridge built here draws hundreds of rectangles, each with its own
combine mode, colours and other-mode bits, textured and not, one and two
cycle. Its frame, and those of krom's ROMs that draw, must be the same
with every program compiled as ever (GLIDEN64_UBERSHADER=off), with
everything drawn by the ubershader (GLIDEN64_UBERSHADER=1), with the ubershader standing in while
the driver compiles (the default, where it can), and with each program held
back for a while first (=delay), since a software driver finishes at once.

  gliden64_ubershader.py --core parallel_n64_libretro.so --host ./lrhost_gl [--roms krom-roms]
"""
import argparse, glob, os, random, struct, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parallel_rdp_regress import fetch  # noqa: E402

KROM = ["cube32", "rottri32", "rottri32aa"]
OPTIONS = ["parallel-n64-gfxplugin=gliden64", "parallel-n64-rspplugin=hle",
           "parallel-n64-cpucore=cached_interpreter"]
# Saved keys would be built as the core's own programs at a context reset
# early in a session: only the run that counts combiners has them.
STORAGE = ["parallel-n64-gliden64-EnableShadersStorage=True"]
FB, LIST, TEX = 0x100000, 0x200000, 0x300000
WIDTH, HEIGHT = 320, 240
CELL = 20
PAYLOAD = 0x4000


def mips(op, rs, rt, imm):
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)


def combine(rnd, cycle2):
    """Combine words for a cell. In two-cycle mode the first cycle must not
    read COMBINED: nothing has been combined yet, and what the shaders
    read then is undefined."""
    while True:
        w0, w1 = rnd.getrandbits(24), rnd.getrandbits(32)
        if not cycle2:
            return w0, w1
        rgb = (((w0 >> 20) & 15), ((w1 >> 28) & 15), ((w0 >> 15) & 31), ((w1 >> 15) & 7))
        alpha = (((w0 >> 12) & 7), ((w1 >> 12) & 7), ((w1 >> 9) & 7))
        if 0 not in rgb and rgb[2] != 7 and 0 not in alpha:
            return w0, w1


def rdp_list(seed):
    rnd = random.Random(seed)
    cmds = [0x3F000000 | (2 << 19) | (WIDTH - 1), FB,
            0x2D000000, ((WIDTH * 4) << 12) | (HEIGHT * 4),
            0x3D000000 | (2 << 19) | 31, TEX,
            0x35000000 | (2 << 19) | (8 << 9), 7 << 24,
            0x33000000, (7 << 24) | (1023 << 12),
            0x27000000, 0,
            0x35000000 | (2 << 19) | (8 << 9), 0,
            0x32000000, (0 << 24) | (31 * 4 << 12) | (31 * 4),
            0x35000000 | (2 << 19) | (8 << 9), 1 << 24,
            0x32000000, (1 << 24) | (31 * 4 << 12) | (31 * 4)]
    x, y = 0, 0
    while y + CELL <= HEIGHT:
        cycle2 = rnd.random() < 0.5
        textured = rnd.random() < 0.6
        bilerp = rnd.randrange(4)
        w0, w1 = combine(rnd, cycle2)
        mode_h = ((1 if cycle2 else 0) << 20) | (bilerp << 10) | (2 << 12)
        mode_l = rnd.choice([0, 1, 2]) | (rnd.randrange(2) << 14)
        cmds += [0x27000000, 0,
                 0x2F000000 | mode_h, mode_l,
                 0x3C000000 | w0, w1,
                 0x3A000000, rnd.getrandbits(32),  # primitive
                 0x3B000000, rnd.getrandbits(32),  # environment
                 0x39000000, rnd.getrandbits(32),  # blend
                 0x38000000, rnd.getrandbits(32),  # fog
                 0x2C000000, 0]
        x1, y1 = (x + CELL - 1) * 4, (y + CELL - 1) * 4
        x0, y0 = x * 4, y * 4
        if textured:
            cmds += [0x24000000 | (x1 << 12) | y1, (x0 << 12) | y0,
                     (rnd.randrange(64) << 21) | (rnd.randrange(64) << 5), (1 << 26) | (1 << 10)]
        else:
            cmds += [0x36000000 | (x1 << 12) | y1, (x0 << 12) | y0]
        x += CELL
        if x + CELL > WIDTH:
            x = 0
            y += CELL
    cmds += [0x29000000, 0]
    return cmds


def cartridge(path, seed):
    t0, t1, t2, t3 = 8, 9, 10, 11
    lui = lambda rt, imm: mips(0x0f, 0, rt, imm)
    ori = lambda rt, rs, imm: mips(0x0d, rs, rt, imm)
    sw = lambda rt, off, base: mips(0x2b, base, rt, off)
    lw = lambda rt, off, base: mips(0x23, base, rt, off)
    bne = lambda rs, rt, off: mips(0x05, rs, rt, off)
    beq = lambda rs, rt, off: mips(0x04, rs, rt, off)
    li = lambda rt, v: [lui(rt, (v >> 16) & 0xffff), ori(rt, rt, v & 0xffff)]

    cmds = rdp_list(seed)
    payload = struct.pack(">%dI" % len(cmds), *cmds)
    texture = b"".join(struct.pack(">H", (u << 11) | (v << 6) | ((u ^ v) << 1) | 1)
                       for v in range(32) for u in range(32))
    list_len = len(payload)
    payload += b"\0" * (TEX - LIST - list_len) + texture

    prog = []
    # PI DMA the list and the texture to RDRAM
    prog += li(t0, 0xA4600000) + li(t1, LIST) + [sw(t1, 0, t0)]
    prog += li(t1, 0x10000000 + PAYLOAD) + [sw(t1, 4, t0)]
    prog += li(t1, len(payload) - 1) + [sw(t1, 0xC, t0)]
    prog += [lui(t0, 0xA440)]
    for k, v in enumerate([0x3002, FB, WIDTH, 2, 0, 0x03E52239, 0x20D, 0xC15, 0x0C150C15,
                           0x006C02EC, 0x002501FF, 0x000E0204, 0x200, 0x400]):
        prog += li(t2, v) + [sw(t2, 4 * k, t0)]
    # The list again at each vertical blank, so that frames keep coming.
    prog += li(t0, 0xA4100000)
    prog += [ori(t1, 0, 1), sw(t1, 0xC, t0)]         # clear XBUS: from RDRAM
    prog += li(t2, 0xA4400010)
    start = len(prog)
    prog += [lw(t1, 0, t2), ori(t3, 0, 0x200), bne(t1, t3, -3), 0]
    prog += li(t1, LIST) + [sw(t1, 0, t0)]
    prog += li(t1, LIST + list_len) + [sw(t1, 4, t0)]
    prog += [lw(t1, 0, t2), ori(t3, 0, 0x200), beq(t1, t3, -3), 0]
    prog += [beq(0, 0, start - len(prog) - 1), 0]

    image = bytearray(max(0x100000, PAYLOAD + len(payload)))
    image[0:4] = (0x80371240).to_bytes(4, "big")
    image[8:12] = (0x80000400).to_bytes(4, "big")
    image[0x20:0x34] = b"GLIDEN64 UBERSHADER "
    assert 0x40 + 4 * len(prog) <= 0x1000
    for k, word in enumerate(prog):
        image[0x40 + 4 * k:0x40 + 4 * k + 4] = (word & 0xffffffff).to_bytes(4, "big")
    image[PAYLOAD:PAYLOAD + len(payload)] = payload
    with open(path, "wb") as f:
        f.write(image)


def run(a, rom, sysdir, env_extra, frames, extra=()):
    hashlog = os.path.join(sysdir, "hash")
    env = dict(os.environ, EGL_PLATFORM="surfaceless", LRHOST_SYSTEM_DIR=sysdir, LRHOST_HASHLOG=hashlog)
    env.update(env_extra)
    p = subprocess.run([a.host, a.core, rom, str(frames)] + OPTIONS + list(extra), env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
    if p.returncode != 0:
        sys.stdout.write(p.stdout.decode("utf-8", "replace")[-2000:])
        raise RuntimeError("exit %d" % p.returncode)
    with open(hashlog) as f:
        return [line.split()[-1] for line in f if line.strip()]


def compare(a, name, rom, frames):
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        base = run(a, rom, os.path.join(tmp, "own"), {"GLIDEN64_UBERSHADER": "off"}, frames)
        run(a, rom, os.path.join(tmp, "count"), {"GLIDEN64_UBERSHADER": "off"}, 2, STORAGE)
        keys = glob.glob(os.path.join(tmp, "count", "ParallelN64", "shaders", "*.keys"))
        count = 0
        if keys:
            with open(keys[0]) as f:
                count = len(f.read().split("\n")) - 4
        if not base:
            print("FAILED: %s presented nothing" % name)
            return 1
        for label, env in (("ubershader", {"GLIDEN64_UBERSHADER": "1"}), ("deferred", {}),
                           ("held-back", {"GLIDEN64_UBERSHADER": "delay"})):
            frames_ = run(a, rom, os.path.join(tmp, label), env, frames)
            same = frames_ == base
            print("%-10s %-10s %s (%d frames, %d combiners)" % (name, label, "ok" if same else "DIFFERENT", len(base), count))
            failures += 0 if same else 1
    return failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--roms", default="krom-roms")
    a = ap.parse_args()
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for seed in range(1):
            rom = os.path.join(tmp, "combiners%d.z64" % seed)
            cartridge(rom, seed)
            failures += compare(a, "cart%d" % seed, rom, 8)
    for name in KROM:
        failures += compare(a, name, fetch(a.roms, name), 60)
    print("%d failed" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
