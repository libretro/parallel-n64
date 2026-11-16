#!/usr/bin/env python3
"""Glide64's second texel converted from the first, against angrylion's.

In two-cycle mode the texture unit runs twice a pixel, and its second
cycle can convert (TEXTCONV's CONVERT_ONE, as G_TC_FILTCONV sets it): the
second texel is then no tile's, it is the first through the YUV matrix
K0 to K3, the first's blue as the luma. With BI_LERP1 set too it is the
first's blue, in all four. Killer Instinct Gold draws its hit sparks so:
(PRIM - ENV) * TEXEL1 + ENV of an intensity texture, with the tile after
the spark's holding nothing meant. Glide64 read that tile, and the sparks
came out magenta streaks.

A cartridge built here hands the RDP, at each vertical blank, a grid of
textured rectangles in two-cycle mode, each reading one intensity of an
I8 texture: with K0-K3 as Killer Instinct sets them and as BT.601's YUV
to RGB has them, converting or giving the blue, through the second texel
as the multiplier, the addend, the minuend and its alpha. Tile 1 holds
the same texture's bytes as 16-bit colours, which a renderer reading it
draws wrong. Each cell's middle must be the colour angrylion gives there,
on GL (each combiner's own shader and the shared one) and on Vulkan.

Left out: cells where angrylion's 9-bit arithmetic wraps and a GPU's
does not (a converted value past 383 or under -128, or a product that
wraps differently), worked out here from both.

  glide64_convert.py --core parallel_n64_libretro.so --gl ./lrhost_gl [--vk ./lrhost_vk]
"""
import argparse, os, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from glide64_blender import (FB, LIST, WIDTH, HEIGHT, BACK, IN, MEM, A_IN, A_ZERO, B_1MA, B_ONE,  # noqa: E402
                             cartridge, mode_lo)
from gliden64_hle_dl import run  # noqa: E402

COLS, ROWS = 12, 8
CW, CH = WIDTH // COLS, HEIGHT // ROWS
TEX_W = 64
INTENS = (0, 48, 96, 144, 192, 240)
# K0-K3, as SetConvert takes them: Killer Instinct Gold's, BT.601's
KSETS = ((0, 0, 64, 0), (175, -43, -89, 222))
PRIM = (255, 255, 255, 255)
ENV = (255, 0, 0, 0)
# the first cycle's colour (a, b, c, d) and alpha: through the second texel
# as the multiplier, the addend, the minuend, its alpha the multiplier
MODES = (("(PRIM-ENV)*T1+ENV", (3, 5, 2, 5), (7, 7, 7, 1)),
         ("T1", (15, 15, 31, 2), (7, 7, 7, 2)),
         ("(T1-0)*PRIM", (2, 15, 3, 7), (2, 7, 3, 7)),
         ("PRIM*T1.a", (3, 15, 9, 7), (7, 7, 7, 1)))
# angrylion's frame is 5 bits a channel
TOL = 10


def converted(i, k, conv):
    """the second texel as angrylion has it (9 bits), and unwrapped"""
    if conv == 7:
        return (i, i, i, i), (i, i, i, i)
    tf = [2 * x + 1 for x in k]
    raw = (i + ((tf[0] * i + 0x80) >> 8), i + ((tf[1] * i + tf[2] * i + 0x80) >> 8), i + ((tf[3] * i + 0x80) >> 8), i)
    return tuple(v & 0x1ff for v in raw), raw


def ext9(v):
    return v | ~0x1ff if (v & 0x180) == 0x180 else v & 0x1ff


def sign9(v):
    v &= 0x1ff
    return v - 0x200 if v & 0x100 else v


def clamp9(v):
    v &= 0x1ff
    return v if v < 0x100 else (255 if v < 0x180 else 0)


def inputs(sel, slot, t0, t1, ch):
    """an input of the colour combiner's (ch 0-2) or the alpha's (ch 3)"""
    if ch == 3:
        table = {1: t0[3], 2: t1[3], 3: PRIM[3], 5: ENV[3]}
        if slot == 2:
            return table.get(sel, 0)
        return table.get(sel, 256 if sel == 6 else 0)
    if slot == 2 and sel == 9:
        return t1[3]
    table = {1: t0[ch], 2: t1[ch], 3: PRIM[ch], 5: ENV[ch]}
    return table.get(sel, 0)


def expect(cell):
    """angrylion's colour, and the unwrapped one a GPU gives"""
    k, conv, (name, rgb, alpha), i = cell
    t1_hw, t1_raw = converted(i, k, conv)
    t0 = (i, i, i, i)
    hw, fl = [], []
    for ch in range(3):
        a, b, c, d = (inputs(rgb[s], s, t0, t1_hw, ch) for s in range(4))
        hw.append(clamp9(((ext9(a) - ext9(b)) * sign9(c) + (ext9(d) << 8) + 0x80) >> 8))
        a, b, c, d = (inputs(rgb[s], s, t0, t1_raw, ch) for s in range(4))
        fl.append(int(round(min(1.0, max(0.0, ((a - b) / 255.0) * (c / 255.0) + d / 255.0)) * 255)))
    return tuple(hw), tuple(fl)


def cells():
    out = []
    for k in KSETS:
        for conv in (5, 7):
            for mode in MODES:
                for i in range(len(INTENS)):
                    cell = (k, conv, mode, INTENS[i])
                    hw, fl = expect(cell)
                    if max(abs(x - y) for x, y in zip(hw, fl)) <= 3:
                        out.append((k, conv, mode, i))
    assert len(out) <= COLS * ROWS
    return out


def describe(cell):
    k, conv, (name, _, _), i = cell
    return "K %s %s %s, intensity %d" % (k, "convert" if conv == 5 else "blue", name, INTENS[i])


def combine(rgb0, a0, rgb1, a1):
    return [0x3C000000 | (rgb0[0] << 20) | (rgb0[2] << 15) | (a0[0] << 12) | (a0[2] << 9) | (rgb1[0] << 5) | rgb1[2],
            (rgb0[1] << 28) | (rgb1[1] << 24) | (a1[0] << 21) | (a1[2] << 18) | (rgb0[3] << 15) | (a0[1] << 12) |
            (a0[3] << 9) | (rgb1[3] << 6) | (a1[1] << 3) | a1[3]]


def rdp_list(tex_addr):
    rgba = lambda c: (c[0] << 24) | (c[1] << 16) | (c[2] << 8) | c[3]
    settile = lambda tile, fmt, siz, line, tmem: [0x35000000 | (fmt << 21) | (siz << 19) | (line << 9) | tmem, tile << 24]
    tilesize = lambda op, tile, w, h: [op | 0, (tile << 24) | (((w - 1) << 2) << 12) | ((h - 1) << 2)]
    c = [0x3F000000 | (2 << 19) | (WIDTH - 1), FB + 0x80000,                 # another image first:
         0x3F000000 | (2 << 19) | (WIDTH - 1), FB,                           # Glide64 presents
         0x2D000000, ((WIDTH * 4) << 12) | (HEIGHT * 4),
         0x27000000, 0,
         0x2F000000 | (3 << 20), 0,                                          # fill
         0x37000000, ((((BACK[0] >> 3) << 11) | ((BACK[1] >> 3) << 6) | ((BACK[2] >> 3) << 1) | 1) * 0x10001),
         0x36000000 | (((WIDTH - 1) * 4) << 12) | ((HEIGHT - 1) * 4), 0,
         0x27000000, 0,
         # the texture: I8, TEX_W x 2, into TMEM through tile 7
         0x3D000000 | (4 << 21) | (1 << 19) | (TEX_W - 1), tex_addr]
    c += settile(7, 4, 1, TEX_W // 8, 0) + [0x26000000, 0] + tilesize(0x34000000, 7, TEX_W, 2)
    # tile 0 the texture; tile 1 its bytes as 16-bit colours, which only a
    # renderer reading the wrong tile draws
    c += [0x28000000, 0] + settile(0, 4, 1, TEX_W // 8, 0) + tilesize(0x32000000, 0, TEX_W, 2)
    c += settile(1, 0, 2, TEX_W // 8, 0) + tilesize(0x32000000, 1, TEX_W // 2, 2)
    c += [0x3A000000, rgba(PRIM), 0x3B000000, rgba(ENV)]
    first, last = (IN, A_ZERO, IN, B_ONE), (IN, A_IN, MEM, B_1MA)
    for n, (k, conv, (name, rgb, alpha), i) in enumerate(cells()):
        x, y = (n % COLS) * CW, (n // COLS) * CH
        k0, k1, k2, k3 = (v & 0x1ff for v in k)
        # two cycles, no dither, point sampled, TEXTCONV as the cell has it
        hi = (1 << 20) | (conv << 9) | (3 << 6) | (3 << 4)
        c += [0x27000000, 0, 0x2C000000 | (k0 << 13) | (k1 << 4) | (k2 >> 5), ((k2 & 0x1f) << 27) | (k3 << 18)]
        c += [0x2F000000 | hi, mode_lo(first, last, 0)]
        c += combine(rgb, alpha, (15, 15, 31, 0), (7, 7, 7, 0))
        # the intensity's run of four texels, its second; s barely steps
        s = (4 * i + 1) << 5
        c += [0x24000000 | (((x + CW - 2) * 4) << 12) | ((y + CH - 2) * 4),
              (((x + 2) * 4) << 12) | ((y + 2) * 4), (s << 16) | 0, (1 << 16) | 0]
    c += [0x29000000, 0]
    return c


def texture():
    row = bytes(INTENS[min(j // 4, len(INTENS) - 1)] for j in range(TEX_W))
    return row + row


def middles(frame, bgra, n):
    px = len(frame) // 4
    w = 640 if px % 640 == 0 and px // 640 in (240, 480) else 320
    h = px // w
    out = []
    for k in range(n):
        x = int(((k % COLS) * CW + CW / 2.0) * w / WIDTH)
        y = int(((k // COLS) * CH + CH / 2.0) * h / HEIGHT)
        p = frame[(y * w + x) * 4:(y * w + x) * 4 + 3]
        out.append((p[2], p[1], p[0]) if bgra else (p[0], p[1], p[2]))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--gl", required=True)
    ap.add_argument("--vk")
    ap.add_argument("-v", action="store_true", help="every cell that differs")
    a = ap.parse_args()
    core = os.path.abspath(a.core)
    cs = cells()
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        rom = os.path.join(tmp, "convert.z64")
        cmds = rdp_list(0)
        cartridge(rom, b"GLIDE64 CONVERT", rdp_list(LIST + 4 * len(cmds)), texture())
        ref = middles(run(os.path.abspath(a.gl), core, rom, tmp, "opengl", "angrylion"), True, len(cs))
        want = [expect((k, conv, mode, INTENS[i]))[0] for k, conv, mode, i in cs]
        off = [n for n, (r, w) in enumerate(zip(ref, want)) if max(abs(x - (y & 0xf8)) for x, y in zip(r, w)) > 8]
        if off:
            print("angrylion is not as worked out here in %d of %d cells, e.g. %s: %s, not %s" % (
                len(off), len(cs), describe(cs[off[0]]), ref[off[0]], want[off[0]]))
            return 1
        runs = (("GL", a.gl, None), ("GL shared", a.gl, {"GLITCH64_COMBINER": "uber"}))
        if a.vk:
            runs += (("Vulkan", a.vk, {"GLIDEN64_VULKAN_STRICT": "1"}),)
        for api, host, env in runs:
            got = middles(run(os.path.abspath(host), core, rom, tmp, "opengl", "glide64",
                              "vulkan" if api == "Vulkan" else "opengl", env), api != "Vulkan", len(cs))
            bad = [(n, r, g) for n, (r, g) in enumerate(zip(ref, got)) if max(abs(x - y) for x, y in zip(r, g)) > TOL]
            print("glide64 %-9s second texel converted as angrylion's %s (%d of %d cells off)" % (
                api, "FAIL" if bad else "ok", len(bad), len(cs)))
            for n, r, g in (bad if a.v else bad[:6]):
                print("   %-50s angrylion %s, glide64 %s" % (describe(cs[n]), r, g))
            failures += 1 if bad else 0
    print("%d failed" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
