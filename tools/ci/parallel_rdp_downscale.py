#!/usr/bin/env python3
"""parallel-RDP's downscaled output, against the upscaled frame it comes from.

  parallel_rdp_downscale.py --core parallel_n64_libretro.so --host ./lrhost_vk [--roms dir]

With parallel-n64-parallel-rdp-downscaling the frame is drawn upscaled and
halved, once per step, as a linear blit halves it: each pixel the mean of
each row's pair of pixels, then the mean of those, every mean rounded half
up. The scale pass does the halving itself now, so its images are never
written and read back. This renders krom's RDP tests upscaled, with and
without the downscale, and holds every byte of the downscaled frame to the
upscaled one halved so.

The halving used to be blits after the scale pass, which blended the
previous frame into the borders at its upscaled coordinates in the
downscaled frame, so a frame's borders showed pieces of the last one.
"""
import argparse, os, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parallel_rdp_regress import OPTIONS, fetch  # noqa: E402

# (ROM, upscaling, downscaling, halvings)
CONFIGS = (("cube16", "2x", "1/2", 1), ("cube16", "4x", "1/4", 2), ("cube16", "8x", "1/8", 3),
           ("shade32", "4x", "1/2", 1), ("rottri32aa", "2x", "1/2", 1))
FRAMES = 12
FRAME = FRAMES - 1


def frame(args, rom, scale, down, tmp):
    for f in os.listdir(tmp):
        os.remove(os.path.join(tmp, f))
    env = dict(os.environ, LRHOST_DUMPDIR=tmp, LRHOST_DUMP_FROM=str(FRAME), LRHOST_DUMP_TO=str(FRAME))
    p = subprocess.run([args.host, args.core, rom, str(FRAMES)] + OPTIONS +
                       ["parallel-n64-upscaling=" + scale,
                        "parallel-n64-parallel-rdp-downscaling=" + down],
                       env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900)
    raw = os.path.join(tmp, "%04d.raw" % FRAME)
    if p.returncode != 0 or not os.path.exists(raw):
        return None, p.stdout.decode("utf-8", "replace")
    with open(raw + ".size") as f:
        w, h = [int(v) for v in f.read().split()[:2]]
    with open(raw, "rb") as f:
        return (w, h, f.read()), ""


def halve(img):
    w, h, data = img
    out = bytearray((w // 2) * (h // 2) * 4)
    for y in range(h // 2):
        r0 = (2 * y) * w * 4
        r1 = r0 + w * 4
        for x in range(w // 2):
            o = (y * (w // 2) + x) * 4
            i = 8 * x
            for c in range(4):
                top = (data[r0 + i + c] + data[r0 + i + 4 + c] + 1) >> 1
                bottom = (data[r1 + i + c] + data[r1 + i + 4 + c] + 1) >> 1
                out[o + c] = (top + bottom + 1) >> 1
    return (w // 2, h // 2, bytes(out))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--roms", default="krom-roms")
    args = ap.parse_args()

    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name, scale, down, steps in CONFIGS:
            rom = fetch(args.roms, name)
            full, log0 = frame(args, rom, scale, "disable", tmp)
            small, log1 = frame(args, rom, scale, down, tmp)
            problem = None
            if not full or not small:
                problem = "no frame"
            else:
                want = full
                for _ in range(steps):
                    want = halve(want)
                if (small[0], small[1]) != (want[0], want[1]):
                    problem = "%dx%d, not %dx%d" % (small[0], small[1], want[0], want[1])
                elif small[2] != want[2]:
                    bad = sum(1 for i in range(0, len(want[2]), 4)
                              if small[2][i:i + 4] != want[2][i:i + 4])
                    problem = "%d pixels differ from the upscaled frame halved" % bad
            print("%-11s %s %-4s %s" % (name, scale, down, "ok" if not problem else "FAILED: " + problem))
            if problem:
                failures += 1
                sys.stdout.write((log1 or log0)[-1500:])
    if failures:
        print("%d failures" % failures)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
