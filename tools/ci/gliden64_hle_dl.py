#!/usr/bin/env python3
"""GLideN64 drawing a display list, as the HLE RSP hands it over.

A cartridge built here starts an F3D graphics task every vertical blank,
which the HLE RSP passes to GLideN64 whole: two textured quads, each a
pair of triangles, lit white, the colour TEXEL0 * SHADE. Between the two
draws, one cartridge turns texturing off and on again (gSPTexture) without
setting the combine again, as Mario Kart 64 does; the other does not. On
the N64 that changes nothing, so the frames must be the same, on GL and on
Vulkan.

The option for SM64 editor hacks (on by default) used to set the combine
to SHADE whenever texturing was turned off, in every game: the second
quad, and Mario Kart's track and walls, came out white. It now applies
only to Super Mario 64 and the hacks made of it, so a third cartridge,
named SUPER MARIO 64, must still show the old behaviour: that the test
can tell.

Each frame ends with a half-transparent quad across the first quad's edge,
and the next begins with opaque draws that ask for blending too (their
render mode forces it), so GLideN64's cache holds blending on from one
frame to the next. It turned blending on for draw buffer 0 behind glsm,
which put the state back without it at each frame: from the second frame
on the quad was drawn unblended, as the clouds of Mario Kart 64 were, in
black boxes. The quad must show what is under it.

  gliden64_hle_dl.py --core parallel_n64_libretro.so --gl ./lrhost_gl [--vk ./lrhost_vk]
"""
import argparse, os, struct, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gliden64_ubershader import mips  # noqa: E402

OPTIONS = ["parallel-n64-gfxplugin=gliden64", "parallel-n64-rspplugin=hle",
           "parallel-n64-cpucore=cached_interpreter",
           "parallel-n64-gliden64-LegacySm64ToolsHacks=enabled"]
FB, DL, VTX, MTX, VP, TEX, UCODE, UDATA = (0x100000, 0x200000, 0x201000, 0x202000, 0x203000,
                                           0x204000, 0x205000, 0x206000)
WIDTH, HEIGHT = 320, 240
PAYLOAD = 0x4000
FRAMES = 12


def matrix(rows):
    """an N64 Mtx: the integer halves, then the fractions, s15.16"""
    fixed = [int(round(v * 65536)) & 0xffffffff for row in rows for v in row]
    return (struct.pack(">16H", *[(f >> 16) & 0xffff for f in fixed]) +
            struct.pack(">16H", *[f & 0xffff for f in fixed]))


def vertex(x, y, s, t):
    """F3D: x y z, flag, s t (10.5), r g b a"""
    return struct.pack(">hhhHhhBBBB", x, y, 0, 0, s << 5, t << 5, 255, 255, 255, 255)


def display_list(toggle):
    c = []
    cmd = lambda w0, w1=0: c.extend([w0 & 0xffffffff, w1 & 0xffffffff])
    cmd(0xFF000000 | (0 << 21) | (2 << 19) | (WIDTH - 1), FB)                # SETCIMG RGBA16
    cmd(0xED000000, ((WIDTH * 4) << 12) | (HEIGHT * 4))                     # SETSCISSOR
    # clear, as Mario Kart 64 does: a rectangle in one-cycle mode, opaque,
    # the colour PRIMITIVE. Its render mode forces blending, as that of
    # the quads after it does.
    cmd(0xE7000000)
    cmd(0xEF000000 | (1 << 19) | (6 << 9), 0x0F0A4000)
    cmd(0xFCFFFFFF, 0xFFFDF6FB)
    cmd(0xFA000000, 0x000000FF)
    cmd(0xF6000000 | ((WIDTH * 4) << 12) | (HEIGHT * 4), 0)                  # FILLRECT
    cmd(0xE7000000)
    # one cycle, perspective texturing, point sampled, texels filtered not
    # converted (G_TC_FILT); an opaque surface
    cmd(0xEF000000 | (1 << 19) | (6 << 9), 0x0F0A4000)
    cmd(0xFC000000 | 0x121824, 0xFF33FFFF)                                   # TEXEL0 * SHADE
    cmd(0xB6000000, 0xFFFFFFFF)                                              # clear geometry mode
    cmd(0xB7000000, 0x00000204)                                              # SHADE | SMOOTH
    cmd(0x03800010, VP)                                                      # viewport
    cmd(0x01030040, MTX)                                                     # projection, load
    cmd(0x01020040, MTX + 64)                                                # modelview, load
    cmd(0xBB000001, 0xFFFFFFFF)                                              # texture on, tile 0
    # the texture: 32x32 RGBA16, one block
    cmd(0xFD000000 | (0 << 21) | (2 << 19), TEX)
    cmd(0xF5000000 | (0 << 21) | (2 << 19), 7 << 24)
    cmd(0xE6000000)
    cmd(0xF3000000, (7 << 24) | (1023 << 12) | 256)
    cmd(0xE7000000)
    cmd(0xF5000000 | (0 << 21) | (2 << 19) | (8 << 9), (5 << 14) | (5 << 4))
    cmd(0xF2000000, (0 << 24) | ((31 * 4) << 12) | (31 * 4))
    cmd(0x04300040, VTX)                                                     # four vertices
    cmd(0xBF000000, (0 << 16) | (10 << 8) | 20)
    cmd(0xBF000000, (0 << 16) | (20 << 8) | 30)
    if toggle:
        cmd(0xBB000000, 0xFFFFFFFF)                                          # texture off
        cmd(0xBB000001, 0xFFFFFFFF)                                          # and on again
    cmd(0x04300040, VTX + 64)
    cmd(0xBF000000, (0 << 16) | (10 << 8) | 20)
    cmd(0xBF000000, (0 << 16) | (20 << 8) | 30)
    # the frame's last draw: a half-transparent green quad (G_RM_XLU_SURF,
    # the colour PRIMITIVE) across the first quad's edge
    cmd(0xE7000000)
    cmd(0xEF000000 | (1 << 19) | (6 << 9), 0x00504240)
    cmd(0xFCFFFFFF, 0xFFFDF6FB)
    cmd(0xFA000000, 0x00FF0080)
    cmd(0x04300040, VTX + 128)
    cmd(0xBF000000, (0 << 16) | (10 << 8) | 20)
    cmd(0xBF000000, (0 << 16) | (20 << 8) | 30)
    cmd(0xE9000000)                                                          # FULLSYNC
    cmd(0xB8000000)                                                          # ENDDL
    return struct.pack(">%dI" % len(c), *c)


def cartridge(path, toggle, name):
    t0, t1, t2, t3 = 8, 9, 10, 11
    lui = lambda rt, imm: mips(0x0f, 0, rt, imm)
    ori = lambda rt, rs, imm: mips(0x0d, rs, rt, imm)
    sw = lambda rt, off, base: mips(0x2b, base, rt, off)
    lw = lambda rt, off, base: mips(0x23, base, rt, off)
    bne = lambda rs, rt, off: mips(0x05, rs, rt, off)
    beq = lambda rs, rt, off: mips(0x04, rs, rt, off)
    li = lambda rt, v: [lui(rt, (v >> 16) & 0xffff), ori(rt, rt, v & 0xffff)]

    area = {}
    area[DL] = display_list(toggle)
    area[VTX] = b"".join(vertex(x, y, s, t) for (x0, y0) in ((-120, -50), (20, -50))
                         for (x, y, s, t) in ((x0, y0, 0, 32), (x0 + 100, y0, 32, 32),
                                              (x0 + 100, y0 + 100, 32, 0), (x0, y0 + 100, 0, 0)))
    area[VTX] += b"".join(vertex(x, y, 0, 0) for (x, y) in ((-60, -20), (60, -20), (60, 20), (-60, 20)))
    area[MTX] = (matrix([[1 / 160.0, 0, 0, 0], [0, 1 / 120.0, 0, 0], [0, 0, 0.5, 0], [0, 0, 0, 1]]) +
                 matrix([[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]))
    area[VP] = struct.pack(">8h", WIDTH * 2, HEIGHT * 2, 0x1ff, 0, WIDTH * 2, HEIGHT * 2, 0x1ff, 0)
    # a checker of four colours, none of them white
    area[TEX] = b"".join(struct.pack(">H", ((0x1f if (u ^ v) & 8 else 0x08) << 11) |
                                     ((0x14 if u & 16 else 0x04) << 6) | ((0x0c if v & 16 else 0x02) << 1) | 1)
                         for v in range(32) for u in range(32))
    area[UCODE] = b"\0" * 0x1000
    # how GLideN64 tells Fast3D
    area[UDATA] = (b"\0" * 0x40 + b"RSP SW Version: 2.0D, 04-01-96" + b"\0").ljust(0x800, b"\0")
    payload = bytearray(UDATA + 0x800 - DL)
    for addr, data in area.items():
        payload[addr - DL:addr - DL + len(data)] = data

    task = [1, 0, 0, 0, UCODE, 0x1000, UDATA, 0x800, 0, 0, 0, 0, DL, len(area[DL]), 0, 0]
    prog = []
    # PI DMA everything to RDRAM
    prog += li(t0, 0xA4600000) + li(t1, DL) + [sw(t1, 0, t0)]
    prog += li(t1, 0x10000000 + PAYLOAD) + [sw(t1, 4, t0)]
    prog += li(t1, len(payload) - 1) + [sw(t1, 0xC, t0)]
    prog += [lui(t0, 0xA440)]
    for k, v in enumerate([0x3002, FB, WIDTH, 2, 0, 0x03E52239, 0x20D, 0xC15, 0x0C150C15,
                           0x006C02EC, 0x002501FF, 0x000E0204, 0x200, 0x400]):
        prog += li(t2, v) + [sw(t2, 4 * k, t0)]
    # the task, in DMEM where the RSP finds it
    prog += li(t0, 0xA4000FC0)
    for k, v in enumerate(task):
        prog += li(t1, v) + [sw(t1, 4 * k, t0)]
    prog += li(t0, 0xA4040010) + li(t2, 0xA4400010)
    start = len(prog)
    prog += [lw(t1, 0, t2), ori(t3, 0, 0x200), bne(t1, t3, -3), 0]
    # clear halt and broke: the RSP runs the task
    prog += [ori(t1, 0, 0x5), sw(t1, 0, t0)]
    prog += [lw(t1, 0, t2), ori(t3, 0, 0x200), beq(t1, t3, -3), 0]
    prog += [beq(0, 0, start - len(prog) - 1), 0]

    image = bytearray(max(0x100000, PAYLOAD + len(payload)))
    image[0:4] = (0x80371240).to_bytes(4, "big")
    image[8:12] = (0x80000400).to_bytes(4, "big")
    image[0x20:0x34] = name.ljust(20).encode("ascii")
    assert 0x40 + 4 * len(prog) <= 0x1000
    for k, word in enumerate(prog):
        image[0x40 + 4 * k:0x40 + 4 * k + 4] = (word & 0xffffffff).to_bytes(4, "big")
    image[PAYLOAD:PAYLOAD + len(payload)] = payload
    with open(path, "wb") as f:
        f.write(image)


def run(host, core, rom, tmp, api):
    dump = tempfile.mkdtemp(dir=tmp)
    env = dict(os.environ, EGL_PLATFORM="surfaceless", LRHOST_SYSTEM_DIR=os.path.join(tmp, "sys"),
               LRHOST_DUMPDIR=dump, LRHOST_DUMP_FROM=str(FRAMES - 4), LRHOST_DUMP_TO=str(FRAMES - 4))
    p = subprocess.run([host, core, rom, str(FRAMES)] + OPTIONS + ["parallel-n64-gliden64-API=" + api],
                       env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
    if p.returncode != 0:
        sys.stdout.write(p.stdout.decode("utf-8", "replace")[-2000:])
        raise RuntimeError("exit %d" % p.returncode)
    raws = sorted(f for f in os.listdir(dump) if f.endswith(".raw"))
    if not raws:
        raise RuntimeError("%s presented nothing" % rom)
    with open(os.path.join(dump, raws[-1]), "rb") as f:
        return f.read()


def pixel(frame, x, y):
    """a pixel of the 640x480 frame, as three bytes"""
    return frame[(y * 640 + x) * 4:(y * 640 + x) * 4 + 3]


def white_share(frame):
    """the share of pixels that are white, either channel order"""
    px = len(frame) // 4
    white = sum(1 for i in range(0, len(frame), 4) if frame[i] >= 0xf0 and frame[i + 1] >= 0xf0 and frame[i + 2] >= 0xf0)
    return white / float(px)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--gl", required=True)
    ap.add_argument("--vk")
    a = ap.parse_args()
    a.core = os.path.abspath(a.core)
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        roms = {}
        for label, toggle, name in (("plain", False, "GLIDEN64 HLE DL"), ("toggled", True, "GLIDEN64 HLE DL"),
                                    ("sm64", True, "SUPER MARIO 64")):
            roms[label] = os.path.join(tmp, label + ".z64")
            cartridge(roms[label], toggle, name)
        hosts = [("opengl", a.gl)] + ([("vulkan", a.vk)] if a.vk else [])
        for api, host in hosts:
            frames = dict((label, run(os.path.abspath(host), a.core, rom, tmp, api)) for label, rom in roms.items())
            plain_white = white_share(frames["plain"])
            checks = (("textured", plain_white < 0.01, "%.1f%% white" % (100 * plain_white)),
                      ("texture off and on", frames["toggled"] == frames["plain"], "same as without"),
                      ("SM64 editor hack, SM64 only", frames["sm64"] != frames["plain"], "differs on SUPER MARIO 64"),
                      # over the first quad and over the background beside it
                      ("blended at the frame's end", pixel(frames["plain"], 240, 240) != pixel(frames["plain"], 320, 240),
                       "%s over the quad, %s over the background" % (pixel(frames["plain"], 240, 240).hex(),
                                                                     pixel(frames["plain"], 320, 240).hex())))
            for what, ok, detail in checks:
                print("%-7s %-28s %s (%s)" % (api, what, "ok" if ok else "FAIL", detail))
                failures += 0 if ok else 1
    print("%d failed" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
