#!/usr/bin/env python3
"""Glide64's blender against angrylion's.

The RDP's blender gives P*A + M*B, its inputs as the other modes select
them: P and M the pixel, the memory colour, the blend colour or the fog
colour; A the pixel's alpha, the fog alpha, the shade alpha or 0; B 1 - A,
the memory alpha, 1 or 0. In two-cycle mode the first cycle always blends,
into the second's pixel; in the last cycle a pixel blends where FORCE_BL
says (and on edges, with coverage), else it is P.

Glide64 picked a GPU blend from a table of the render modes some games
used, and drew its fog with Glide's fog table. A cartridge built here hands
the RDP, at each vertical blank, a grid of cells over a background, each a
rectangle drawn with its own blender settings: every P, A and M with B
1 - A, 1 or 0, blended; P alone, not blended; and fog-like first cycles
of two. Each cell's middle must be the colour angrylion gives there,
within a few steps (angrylion blends at 5 bits a factor), on GL and on
Vulkan.

Left out, as Glide64 keeps no coverage: B the memory alpha (the
coverage's), and memory * (A + 1). The colours keep every sum under 256,
where angrylion's blender wraps and a GPU's clamps.

  glide64_blender.py --core parallel_n64_libretro.so --gl ./lrhost_gl [--vk ./lrhost_vk]
"""
import argparse, os, struct, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gliden64_ubershader import mips  # noqa: E402
from gliden64_hle_dl import run  # noqa: E402

FB, LIST = 0x100000, 0x200000
WIDTH, HEIGHT = 320, 240
PAYLOAD = 0x4000
COLS, ROWS = 16, 16
CW, CH = WIDTH // COLS, HEIGHT // ROWS
# the background (the memory colour), the pixel (PRIMITIVE), blend, fog
BACK = (40, 88, 120)
PRIM = (100, 60, 20, 0x60)
BLEND = (110, 20, 60, 0x00)
FOG = (20, 110, 40, 0xC0)
SHADE_A = 0x40
IN, MEM, BLC, FOGC = 0, 1, 2, 3
A_IN, A_FOG, A_SHADE, A_ZERO = 0, 1, 2, 3
B_1MA, B_MEM, B_ONE, B_ZERO = 0, 1, 2, 3
TOL = 12


def cells():
    """(cycles, force, first, last): first the first cycle of two, None in
    one-cycle mode; each a (P, A, M, B)"""
    out = []
    for p in (IN, MEM, BLC, FOGC):
        for a in (A_IN, A_FOG, A_SHADE, A_ZERO):
            for m in (IN, MEM, BLC, FOGC):
                for b in (B_1MA, B_ONE, B_ZERO):
                    if p == MEM and m == MEM and b == B_ONE:
                        continue
                    out.append((1, 1, None, (p, a, m, b)))
    for p in (IN, MEM, BLC, FOGC):
        out.append((1, 0, None, (p, A_FOG, MEM, B_1MA)))
    for first in ((FOGC, A_SHADE, IN, B_1MA), (BLC, A_FOG, IN, B_1MA), (IN, A_IN, FOGC, B_ONE),
                  (FOGC, A_FOG, BLC, B_ZERO)):
        out.append((2, 0, first, (IN, A_IN, IN, B_1MA)))
        out.append((2, 1, first, (IN, A_IN, MEM, B_1MA)))
    assert len(out) <= COLS * ROWS
    return out


def mode_lo(first, last, force):
    """the other modes' low word: the blender's cycles, FORCE_BL, IMAGE_READ"""
    f = first or last
    bl = ((f[0] << 30) | (f[1] << 26) | (f[2] << 22) | (f[3] << 18) |
          (last[0] << 28) | (last[1] << 24) | (last[2] << 20) | (last[3] << 16))
    return bl | (force << 14) | 0x40


def rect(x0, y0, x1, y1, shade_a):
    """a shaded triangle (0x0C) as a rectangle: the major edge on the right"""
    fx = lambda v: (v << 16) & 0xffffffff
    edges = [0x0C000000 | ((y1 * 4) & 0x3fff), (((y0 * 4) & 0x3fff) << 16) | ((y0 * 4) & 0x3fff),
             fx(x0), 0, fx(x1), 0, fx(x0), 0]
    shade = [0, shade_a] + [0] * 14
    return edges + shade


def rdp_list():
    c = [0x3F000000 | (2 << 19) | (WIDTH - 1), FB + 0x80000,                 # another image first:
         0x3F000000 | (2 << 19) | (WIDTH - 1), FB,                           # Glide64 presents
         0x2D000000, ((WIDTH * 4) << 12) | (HEIGHT * 4),
         0x27000000, 0,
         0x2F000000 | (3 << 20), 0,                                          # fill
         0x37000000, ((((BACK[0] >> 3) << 11) | ((BACK[1] >> 3) << 6) | ((BACK[2] >> 3) << 1) | 1) * 0x10001),
         0x36000000 | (((WIDTH - 1) * 4) << 12) | ((HEIGHT - 1) * 4), 0,
         0x27000000, 0,
         0x3CFFFFFF, 0xFFFDF6FB,                                             # PRIMITIVE, both cycles
         0x3A000000, (PRIM[0] << 24) | (PRIM[1] << 16) | (PRIM[2] << 8) | PRIM[3],
         0x39000000, (BLEND[0] << 24) | (BLEND[1] << 16) | (BLEND[2] << 8) | BLEND[3],
         0x38000000, (FOG[0] << 24) | (FOG[1] << 16) | (FOG[2] << 8) | FOG[3]]
    for k, (cycles, force, first, last) in enumerate(cells()):
        x, y = (k % COLS) * CW, (k // COLS) * CH
        # no dither (RGB and alpha dither off): angrylion's pixels exact
        hi = ((cycles - 1) << 20) | (3 << 6) | (3 << 4)
        c += [0x27000000, 0, 0x2F000000 | hi, mode_lo(first, last, force)]
        c += rect(x + 2, y + 2, x + CW - 2, y + CH - 2, SHADE_A)
    c += [0x29000000, 0]
    return c


def cartridge(path):
    t0, t1, t2, t3 = 8, 9, 10, 11
    lui = lambda rt, imm: mips(0x0f, 0, rt, imm)
    ori = lambda rt, rs, imm: mips(0x0d, rs, rt, imm)
    sw = lambda rt, off, base: mips(0x2b, base, rt, off)
    lw = lambda rt, off, base: mips(0x23, base, rt, off)
    bne = lambda rs, rt, off: mips(0x05, rs, rt, off)
    beq = lambda rs, rt, off: mips(0x04, rs, rt, off)
    li = lambda rt, v: [lui(rt, (v >> 16) & 0xffff), ori(rt, rt, v & 0xffff)]

    cmds = rdp_list()
    payload = struct.pack(">%dI" % len(cmds), *[w & 0xffffffff for w in cmds])
    prog = []
    prog += li(t0, 0xA4600000) + li(t1, LIST) + [sw(t1, 0, t0)]
    prog += li(t1, 0x10000000 + PAYLOAD) + [sw(t1, 4, t0)]
    prog += li(t1, len(payload) - 1) + [sw(t1, 0xC, t0)]
    prog += [lui(t0, 0xA440)]
    for k, v in enumerate([0x3002, FB, WIDTH, 2, 0, 0x03E52239, 0x20D, 0xC15, 0x0C150C15,
                           0x006C02EC, 0x002501FF, 0x000E0204, 0x200, 0x400]):
        prog += li(t2, v) + [sw(t2, 4 * k, t0)]
    prog += li(t0, 0xA4100000)
    prog += [ori(t1, 0, 1), sw(t1, 0xC, t0)]         # clear XBUS: from RDRAM
    prog += li(t2, 0xA4400010)
    start = len(prog)
    prog += [lw(t1, 0, t2), ori(t3, 0, 0x200), bne(t1, t3, -3), 0]
    prog += li(t1, LIST) + [sw(t1, 0, t0)]
    prog += li(t1, LIST + len(payload)) + [sw(t1, 4, t0)]
    prog += [lw(t1, 0, t2), ori(t3, 0, 0x200), beq(t1, t3, -3), 0]
    prog += [beq(0, 0, start - len(prog) - 1), 0]

    image = bytearray(max(0x100000, PAYLOAD + len(payload)))
    image[0:4] = (0x80371240).to_bytes(4, "big")
    image[8:12] = (0x80000400).to_bytes(4, "big")
    image[0x20:0x34] = b"GLIDE64 BLENDER     "
    assert 0x40 + 4 * len(prog) <= 0x1000
    for k, word in enumerate(prog):
        image[0x40 + 4 * k:0x40 + 4 * k + 4] = (word & 0xffffffff).to_bytes(4, "big")
    image[PAYLOAD:PAYLOAD + len(payload)] = payload
    with open(path, "wb") as f:
        f.write(image)


def middles(frame, bgra):
    """each cell's middle: the frame's size from its length, 4:3"""
    px = len(frame) // 4
    w = 640 if px % 640 == 0 and px // 640 in (240, 480) else 320
    h = px // w
    out = []
    for k in range(len(cells())):
        x = int(((k % COLS) * CW + CW / 2.0) * w / WIDTH)
        y = int(((k // COLS) * CH + CH / 2.0) * h / HEIGHT)
        p = frame[(y * w + x) * 4:(y * w + x) * 4 + 3]
        out.append((p[2], p[1], p[0]) if bgra else (p[0], p[1], p[2]))
    return out


def describe(cell):
    names = (("IN", "MEM", "BLEND", "FOG"), ("A_IN", "A_FOG", "A_SHADE", "0"), ("IN", "MEM", "BLEND", "FOG"),
             ("1-A", "A_MEM", "1", "0"))
    cyc = lambda c: "%s*%s+%s*%s" % (names[0][c[0]], names[1][c[1]], names[2][c[2]], names[3][c[3]])
    cycles, force, first, last = cell
    return "%s%s%s" % ("" if first is None else cyc(first) + " then ", cyc(last), "" if force else " (not forced)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--gl", required=True)
    ap.add_argument("--vk")
    ap.add_argument("-v", action="store_true", help="every cell that differs")
    a = ap.parse_args()
    core = os.path.abspath(a.core)
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        rom = os.path.join(tmp, "blender.z64")
        cartridge(rom)
        ref = middles(run(os.path.abspath(a.gl), core, rom, tmp, "opengl", "angrylion"), True)
        if len(set(ref)) < 20:
            print("angrylion drew %d colours only" % len(set(ref)))
            return 1
        for api, host in (("GL", a.gl),) + ((("Vulkan", a.vk),) if a.vk else ()):
            # strict: a draw whose pipeline (its blend among them) was not
            # built at load aborts
            got = middles(run(os.path.abspath(host), core, rom, tmp, "opengl", "glide64",
                              "vulkan" if api == "Vulkan" else "opengl",
                              {"GLIDEN64_VULKAN_STRICT": "1"} if api == "Vulkan" else None), api == "GL")
            bad = [(k, r, g) for k, (r, g) in enumerate(zip(ref, got)) if max(abs(x - y) for x, y in zip(r, g)) > TOL]
            ok = not bad
            print("glide64 %-7s blender as angrylion's %s (%d of %d cells off)" % (api, "ok" if ok else "FAIL",
                                                                                  len(bad), len(ref)))
            for k, r, g in (bad if a.v else bad[:6]):
                print("   %-40s angrylion %s, glide64 %s" % (describe(cells()[k]), r, g))
            failures += 0 if ok else 1
    print("%d failed" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
