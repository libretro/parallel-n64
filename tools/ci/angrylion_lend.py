#!/usr/bin/env python3
"""angrylion: frames drawn straight into the frontend's buffer.

When the frontend lends its software framebuffer
(RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER), angrylion's VI writes
the frame into it, so that presenting it copies nothing. This runs krom's
RDP tests through tools/lrhost with and without a lent buffer, for each VI
output mode and upscaling factor, with the overscan shown and hidden, and
one of krom's CPU tests, whose screen is interlaced, woven and bobbed; it
holds the two to the same picture (the X byte of XRGB8888 aside, which
nothing shows), and the modes that can draw into the lent buffer to doing
so (the unfiltered views used to draw into a buffer of their own, to be
copied, every frame, and the filtered view did with the overscan hidden or
an interlaced frame bobbed; a woven frame keeps the other field's lines
from the frame before, so it cannot).

  angrylion_lend.py --core parallel_n64_libretro.so --host ./lrhost [--roms krom-roms]
"""
import argparse, os, re, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parallel_rdp_regress import fetch  # noqa: E402
import krom_cpu_regress  # noqa: E402

ROMS = ["cube16", "rottri32aa", "zbuf16"]
OVERSCAN = ["parallel-n64-angrylion-overscan=enabled"]
BOB = ["parallel-n64-angrylion-deinterlace=bob"]
# (VI output, upscaling, other options, must the lent buffer be used)
MODES = [("Filtered", "1x", [], True), ("Filtered", "2x", [], True), ("Filtered", "4x", [], True),
         ("Unfiltered", "1x", [], True), ("Coverage", "1x", [], True), ("Depth", "1x", [], False),
         ("Filtered", "1x", OVERSCAN, True)]
# an interlaced screen
INTERLACED = "div"
INTERLACED_MODES = [("Filtered", "1x", [], False), ("Filtered", "1x", OVERSCAN, False),
                    ("Filtered", "1x", BOB, True), ("Filtered", "1x", BOB + OVERSCAN, True)]
FRAMES = 40
DUMP = (10, 25, 39)


def run(args, rom, mode, scale, extra, lend, tmp):
    for f in os.listdir(tmp):
        os.remove(os.path.join(tmp, f))
    env = dict(os.environ, LRHOST_DUMPDIR=tmp, LRHOST_DUMP_FROM=str(DUMP[0]),
               LRHOST_DUMP_TO=str(DUMP[-1]))
    env.pop("LRHOST_SWFB", None)
    if lend:
        env["LRHOST_SWFB"] = "1"
    cmd = [args.host, args.core, rom, str(FRAMES),
           "parallel-n64-gfxplugin=angrylion", "parallel-n64-rspplugin=hle",
           "parallel-n64-cpucore=cached_interpreter",
           "parallel-n64-angrylion-vioverlay=" + mode,
           "parallel-n64-upscaling=" + scale] + extra
    p = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, timeout=900)
    frames = {}
    for n in DUMP:
        raw = os.path.join(tmp, "%04d.raw" % n)
        if os.path.exists(raw):
            data = open(raw, "rb").read()
            # the picture: every byte but each pixel's X
            frames[n] = (open(raw + ".size").read().split(),
                         bytes(b for i, b in enumerate(data) if i % 4 != 3))
    m = re.search(r"presented from the lent buffer (\d+)", p.stdout)
    return p.returncode, frames, int(m.group(1)) if m else 0, p.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--roms", default="krom-roms")
    args = ap.parse_args()

    cases = [(fetch(args.roms, name), name, m) for name in ROMS for m in MODES]
    cases += [(krom_cpu_regress.fetch(args.roms, INTERLACED), INTERLACED, m) for m in INTERLACED_MODES]
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for rom, name, (mode, scale, extra, must_lend) in cases:
            rc0, own, _, out0 = run(args, rom, mode, scale, extra, False, tmp)
            rc1, lent, hits, out1 = run(args, rom, mode, scale, extra, True, tmp)
            problems = []
            if rc0 or rc1:
                problems.append("exit %d/%d" % (rc0, rc1))
            if any(r in o for o in (out0, out1)
                   for r in ("runtime error:", "ERROR: AddressSanitizer")):
                problems.append("sanitizer report")
            if not own or set(own) != set(lent):
                problems.append("frames missing")
            elif any(own[n] != lent[n] for n in own):
                problems.append("a lent buffer shows another picture")
            if must_lend and hits == 0:
                problems.append("nothing drawn into the lent buffer")
            label = " ".join(o.split("=", 1)[1] for o in extra)
            print("%-10s %-10s %s %-12s %s" % (name, mode, scale, label,
                  "ok (%d from the lent buffer)" % hits if not problems else "FAILED: " + ", ".join(problems)))
            if problems:
                failures += 1
                sys.stdout.write((out1 or out0)[-1500:])
    if failures:
        print("%d failures" % failures)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
