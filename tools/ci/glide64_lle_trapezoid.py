#!/usr/bin/env python3
"""Glide64 drawing RDP triangles whose L edge does not start where the M
edge ends.

An RDP triangle command gives three edges: H, the major one, from YH to
YL; M from YH to YM; L from YM to YL, starting at XL. Below YM the RDP
takes XL whatever the M edge's walk reached, so one command draws a
trapezoid, or a shape with a step in its minor side.

Glide64 took XL only when it differed from XH, two values that have
nothing to do with each other, and gave the GPU no corner where the M
edge ends: the minor side ran straight from the M edge's start to XL.

A cartridge built here hands the RDP, at each vertical blank, two
triangles: one whose minor side steps out at YM, and one with YH at YM,
XL the same as XH, and XM left at a value the RDP never uses. Every pixel
of the frame clear of an edge must be drawn, or not, as the RDP's edges
say, on GL and on Vulkan.

  glide64_lle_trapezoid.py --core parallel_n64_libretro.so --gl ./lrhost_gl [--vk ./lrhost_vk]
"""
import argparse, os, struct, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gliden64_ubershader import mips  # noqa: E402
from gliden64_hle_dl import run  # noqa: E402

FB, LIST = 0x100000, 0x200000
WIDTH, HEIGHT = 320, 240
PAYLOAD = 0x4000
PRIM = (0xff, 0xc0, 0x40)
# (yh, ym, yl, xh, dxhdy, xm, dxmdy, xl, dxldy), in pixels; H on the right
SHAPES = (
    # the minor side steps out from 110 to 150 at YM
    (20, 60, 110, 200, 0.5, 150, -1.0, 150, 0.5),
    # YH at YM, XL at XH: an apex at the top, XM nothing the RDP reads
    (130, 130, 200, 100, 1.0, 300, 0.0, 100, -1.0),
)


def fixed(v):
    return int(round(v * 65536)) & 0xffffffff


def triangle(yh, ym, yl, xh, dxhdy, xm, dxmdy, xl, dxldy):
    """a non-shaded triangle (0x08), the major edge on the right"""
    return [0x08000000 | ((yl * 4) & 0x3fff), (((ym * 4) & 0x3fff) << 16) | ((yh * 4) & 0x3fff),
            fixed(xl), fixed(dxldy), fixed(xh), fixed(dxhdy), fixed(xm), fixed(dxmdy)]


def rdp_list():
    c = [0x3F000000 | (2 << 19) | (WIDTH - 1), FB + 0x80000,                 # another image first:
         0x3F000000 | (2 << 19) | (WIDTH - 1), FB,                           # Glide64 presents
         0x2D000000, ((WIDTH * 4) << 12) | (HEIGHT * 4),
         0x27000000, 0,
         0x2F000000 | (3 << 20), 0,                                          # fill
         0x37000000, 0,
         0x36000000 | (((WIDTH - 1) * 4) << 12) | ((HEIGHT - 1) * 4), 0,
         0x27000000, 0,
         0x2F000000 | (6 << 9), 0x0F0A4000,                                  # one cycle, opaque
         0x3CFFFFFF, 0xFFFDF6FB,                                             # PRIMITIVE
         0x3A000000, (PRIM[0] << 24) | (PRIM[1] << 16) | (PRIM[2] << 8) | 0xff]
    for s in SHAPES:
        c += triangle(*s)
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
    payload = struct.pack(">%dI" % len(cmds), *cmds)
    prog = []
    # PI DMA the list to RDRAM
    prog += li(t0, 0xA4600000) + li(t1, LIST) + [sw(t1, 0, t0)]
    prog += li(t1, 0x10000000 + PAYLOAD) + [sw(t1, 4, t0)]
    prog += li(t1, len(payload) - 1) + [sw(t1, 0xC, t0)]
    prog += [lui(t0, 0xA440)]
    for k, v in enumerate([0x3002, FB, WIDTH, 2, 0, 0x03E52239, 0x20D, 0xC15, 0x0C150C15,
                           0x006C02EC, 0x002501FF, 0x000E0204, 0x200, 0x400]):
        prog += li(t2, v) + [sw(t2, 4 * k, t0)]
    # the list again at each vertical blank
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
    image[0x20:0x34] = b"GLIDE64 TRAPEZOID   "
    assert 0x40 + 4 * len(prog) <= 0x1000
    for k, word in enumerate(prog):
        image[0x40 + 4 * k:0x40 + 4 * k + 4] = (word & 0xffffffff).to_bytes(4, "big")
    image[PAYLOAD:PAYLOAD + len(payload)] = payload
    with open(path, "wb") as f:
        f.write(image)


def coverage(x, y, margin):
    """True inside a shape, False outside, None within @margin of an edge"""
    for yh, ym, yl, xh, dxhdy, xm, dxmdy, xl, dxldy in SHAPES:
        if y < yh - margin or y > yl + margin:
            continue
        if min(abs(y - yh), abs(y - yl)) < margin:
            return None
        right = xh + dxhdy * (y - yh)
        if y < ym:
            left = xm + dxmdy * (y - yh)
        else:
            left = xl + dxldy * (y - ym)
        if abs(y - ym) < margin and ym > yh:
            return None  # the step
        if min(abs(x - left), abs(x - right)) < margin:
            return None
        if left < x < right:
            return True
    return False


def check(frame, bgra):
    """every pixel clear of an edge drawn as the shapes say: (bad, checked)"""
    w = len(frame) // 4 // 480 if len(frame) >= 640 * 480 * 4 else 0
    if w != 640:
        return None, 0
    bad = checked = 0
    for j in range(480):
        for i in range(640):
            want = coverage((i + 0.5) / 2.0, (j + 0.5) / 2.0, 1.5)
            if want is None:
                continue
            p = frame[(j * 640 + i) * 4:(j * 640 + i) * 4 + 3]
            got = (p[2], p[1], p[0]) if bgra else (p[0], p[1], p[2])
            drawn = max(abs(g - w_) for g, w_ in zip(got, PRIM)) <= 8
            black = max(got) <= 8
            checked += 1
            if not (drawn if want else black):
                bad += 1
    return bad, checked


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--gl", required=True)
    ap.add_argument("--vk")
    a = ap.parse_args()
    core = os.path.abspath(a.core)
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        rom = os.path.join(tmp, "trapezoid.z64")
        cartridge(rom)
        for api, host in (("GL", a.gl),) + ((("Vulkan", a.vk),) if a.vk else ()):
            frame = run(os.path.abspath(host), core, rom, tmp, "opengl", "glide64",
                        "vulkan" if api == "Vulkan" else "opengl")
            bad, checked = check(frame, api == "GL")
            ok = bad == 0 and checked > 100000
            print("glide64 %-7s RDP trapezoids %s (%s of %d pixels wrong)" % (api, "ok" if ok else "FAIL", bad, checked))
            failures += 0 if ok else 1
    print("%d failed" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
