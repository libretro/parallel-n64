#!/usr/bin/env python3
"""Rice drawing a display list, as the HLE RSP hands it over.

Rice only draws what an F3D task gives it (krom's ROMs, straight to the
RDP, it shows nothing of), so it is held here to gliden64_hle_dl.py's
cartridges: two textured quads and a half-transparent one across the
first's edge, with each of their combines (TEXEL0 * SHADE, texturing
turned off and on again between them, TEXEL0 a share of the way to
ENVIRONMENT, TEXEL0 * PRIMITIVE with the primitive colour changed between
the quads, two cycles, back faces culled), IA16 texels, textures that are
not square, and a microcode Rice does not know. GLideN64, drawing the same
cartridges, is the reference:

  * every cartridge runs to its end and is drawn;
  * each quad where GLideN64 draws it: the bounds of what is drawn, within
    two pixels;
  * the combines: where the texture is a checker of four colours (no
    texel edge in sight of a pixel that is not one of them), at least
    the share of pixels it draws now within eight steps of GLideN64's;
  * textures that are not square, and 16-bit texels through the palette:
    the share of pixels within eight steps of GLideN64's too (lower:
    Rice samples these half a texel off, and its 16-bit texels through
    the palette look up other colours than the hardware);
  * the frames the same from one run to the next.

  rice_hle_dl.py --core parallel_n64_libretro.so --gl ./lrhost_gl
"""
import argparse, os, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gliden64_hle_dl as g  # noqa: E402

# the share of pixels within eight steps of GLideN64's, as Rice draws them
# (the rest along texel and quad edges, which Rice samples half a texel
# off), less a margin
COMBINES = [("plain", {}, 0.94), ("toggle", {"toggle": True}, 0.94), ("inter", {"inter": True}, 0.94),
            ("cull", {"cull": True}, 0.96), ("prim", {"prim": True}, 0.94), ("two", {"two": True}, 0.94),
            ("ia", {"ia": True}, 0.94)]
SHAPES = [("s3216", {"shape": (32, 16)}, 0.75), ("s832", {"shape": (8, 32)}, 0.75),
          ("s644", {"shape": (64, 4)}, 0.74), ("tlut", {"shape": (32, 32), "tlut": True}, 0.71)]


def close_share(a, b):
    n = len(a) // 4
    close = sum(1 for i in range(0, len(a), 4) if max(abs(a[i + c] - b[i + c]) for c in range(3)) <= 8)
    return close / float(n)


def bounds(frame):
    """the bounds of what is drawn in the quads' rows, left and right half"""
    out = []
    for lo, hi in ((0, 320), (320, 640)):
        xs, ys = [], []
        for y in range(0, 480, 2):
            for x in range(lo, hi, 2):
                if max(g.pixel(frame, x, y)) >= 8:
                    xs.append(x)
                    ys.append(y)
        out.append((min(xs), min(ys), max(xs), max(ys)) if xs else None)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--gl", required=True)
    a = ap.parse_args()
    core, gl = os.path.abspath(a.core), os.path.abspath(a.gl)
    failures = []

    def check(ok, what):
        print("%-60s %s" % (what, "ok" if ok else "FAILED"))
        if not ok:
            failures.append(what)

    with tempfile.TemporaryDirectory() as tmp:
        def both(name, kw):
            kw = dict(kw)
            toggle = kw.pop("toggle", False)
            rom = os.path.join(tmp, name + ".z64")
            g.cartridge(rom, toggle, "RICE " + name.upper(), **kw)
            return (g.run(gl, core, rom, tmp, "opengl", "rice"), g.run(gl, core, rom, tmp, "opengl", "gliden64"),
                    rom)

        for name, kw, least in COMBINES:
            rice, ref, rom = both(name, kw)
            share = close_share(rice, ref)
            rb, gb = bounds(rice), bounds(ref)
            check(all(r and n and max(abs(p - q) for p, q in zip(r, n)) <= 2 for r, n in zip(rb, gb)),
                  "%s: the quads where GLideN64 draws them" % name)
            check(share >= least, "%s: %.3f of the pixels within 8 of GLideN64's (%.2f)" % (name, share, least))
            if name == "plain":
                check(g.run(gl, core, rom, tmp, "opengl", "rice") == rice, "plain: the same frame again")

        for name, kw, least in SHAPES:
            rice, ref, rom = both(name, kw)
            share = close_share(rice, ref)
            rb, gb = bounds(rice), bounds(ref)
            check(all(r and n and max(abs(p - q) for p, q in zip(r, n)) <= 2 for r, n in zip(rb, gb)),
                  "%s: the quads where GLideN64 draws them" % name)
            check(share >= least, "%s: %.3f of the pixels within 8 of GLideN64's (%.2f)" % (name, share, least))

        rom = os.path.join(tmp, "unknown.z64")
        g.cartridge(rom, False, "RICE UNKNOWN", known=False)
        try:
            g.run(gl, core, rom, tmp, "opengl", "rice")
            ran = True
        except RuntimeError:
            ran = False
        check(ran, "unknown microcode: ran to its end")

    print("rice hle dl: %s" % ("ok" if not failures else "%d FAILED" % len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
