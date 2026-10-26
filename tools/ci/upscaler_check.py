#!/usr/bin/env python3
"""Checks what parallel-rdp hands a temporal upscaler, from a dump of
tools/lrhost_vk with LRHOST_UPSCALER (frames NNNN.raw, NNNN.mv.raw,
NNNN.depth.raw, NNNN.mask.raw and the LRHOST_UPSCALER_LOG beside them).

  upscaler_check.py DUMPDIR LOG FIRST LAST [--moving]

Every frame must come with inputs of the color's size (the transparency
mask one byte a pixel), its jitter must
follow the Halton (2, 3) sequence, and with --moving the motion vectors
must take each pixel that changed from the frame before to where that
frame had its color (to the pixel), where standing still would not.
"""
import argparse, struct, sys


def halton(i, base):
    r, f, i = 0.0, 1.0, i + 1
    while i:
        f /= base
        r += f * (i % base)
        i //= base
    return r


def read_log(path):
    frames = {}
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) < 3:
                continue
            w, h = map(int, p[1].split("x"))
            if p[2] == "none":
                frames[int(p[0])] = None
                continue
            frames[int(p[0])] = {"size": (w, h), "jitter": (float(p[3]), float(p[4])), "flags": int(p[6]),
                                 "mv": tuple(map(int, p[8].split("x"))), "mv_fmt": int(p[10]),
                                 "depth": tuple(map(int, p[12].split("x"))),
                                 "depth_fmt": int(p[14]),
                                 "mask": tuple(map(int, p[16].split("x"))) if len(p) > 18 else None,
                                 "mask_fmt": int(p[18]) if len(p) > 18 else None}
    return frames


def load(dumpdir, n, w, h, mv_fmt, depth_fmt):
    with open("%s/%04d.raw" % (dumpdir, n), "rb") as f:
        color = f.read()
    with open("%s/%04d.mv.raw" % (dumpdir, n), "rb") as f:
        # RG32F or RG16F
        mv = struct.unpack("<%d%s" % (w * h * 2, "f" if mv_fmt == 103 else "e"), f.read())
    with open("%s/%04d.depth.raw" % (dumpdir, n), "rb") as f:
        raw = f.read()
    depth = struct.unpack("<%df" % (w * h), raw) if depth_fmt == 126 else \
        [v / 65535.0 for v in struct.unpack("<%dH" % (w * h), raw)]
    px = [color[i * 4:i * 4 + 4] for i in range(w * h)]
    return px, mv, depth


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dumpdir")
    ap.add_argument("log")
    ap.add_argument("first", type=int)
    ap.add_argument("last", type=int)
    ap.add_argument("--moving", action="store_true")
    a = ap.parse_args()

    frames = read_log(a.log)
    failures = []
    if not frames:
        failures.append("no frames logged")
    for n, fr in sorted(frames.items()):
        if fr is None:
            failures.append("frame %d: no inputs" % n)
            continue
        if fr["mv"] != fr["size"] or fr["depth"] != fr["size"]:
            failures.append("frame %d: inputs %s/%s for a %s frame" % (n, fr["mv"], fr["depth"], fr["size"]))
        # VK_FORMAT_R8_UNORM
        if fr["mask"] != fr["size"] or fr["mask_fmt"] != 9:
            failures.append("frame %d: transparency mask %s format %s for a %s frame"
                            % (n, fr["mask"], fr["mask_fmt"], fr["size"]))

    # the jittered frames: consecutive Halton points, from wherever it started
    jittered = [n for n in range(a.first, a.last + 1) if frames.get(n) and frames[n]["jitter"] != (0.0, 0.0)]
    if a.moving and len(jittered) != a.last - a.first + 1:
        failures.append("frames %d-%d: only %d jittered" % (a.first, a.last, len(jittered)))
    if jittered:
        seq = [(halton(i, 2) - 0.5, halton(i, 3) - 0.5) for i in range(64)]
        jx, jy = frames[jittered[0]]["jitter"]
        start = [i for i, (x, y) in enumerate(seq) if abs(x - jx) < 1e-5 and abs(y - jy) < 1e-5]
        if not start:
            failures.append("frame %d: jitter %s is not a Halton (2, 3) point" % (jittered[0], (jx, jy)))
        else:
            # (a sequence of 8 to 64 frames, then from the start again)
            i = start[0]
            for n in jittered[1:]:
                got = frames[n]["jitter"]
                near = lambda w: abs(got[0] - w[0]) < 1e-5 and abs(got[1] - w[1]) < 1e-5
                if near(seq[(i + 1) % 64]):
                    i = (i + 1) % 64
                elif near(seq[0]) and i + 1 >= 8:
                    i = 0
                else:
                    failures.append("frame %d: jitter %s, want %s" % (n, got, seq[(i + 1) % 64]))
                    break

    if a.moving:
        for n in range(a.first + 1, a.last + 1):
            cur, prev = frames.get(n), frames.get(n - 1)
            if not cur or not prev or cur["size"] != prev["size"]:
                failures.append("frame %d: no frame before it to compare with" % n)
                continue
            w, h = cur["size"]
            c, mv, _ = load(a.dumpdir, n, w, h, cur["mv_fmt"], cur["depth_fmt"])
            cp, mvp, _ = load(a.dumpdir, n - 1, w, h, prev["mv_fmt"], prev["depth_fmt"])
            jn, jp = cur["jitter"], prev["jitter"]
            # the pixels that changed: each must find its color where the
            # motion vectors say it was (to the pixel: the RDP fills every
            # pixel an edge touches); a pixel carried off the frame proves
            # nothing either way
            hits = {"motion": 0, "still": 0}
            covered = 0
            for i in range(w * h):
                if c[i] == cp[i]:
                    continue
                # a still surface a moving one uncovered was nowhere
                # the frame before
                if mv[2 * i] == 0.0 and mv[2 * i + 1] == 0.0 and \
                        abs(mvp[2 * i]) + abs(mvp[2 * i + 1]) > 0.5:
                    continue
                y, x = divmod(i, w)
                q = {}
                for name, use in (("motion", 1), ("still", 0)):
                    q[name] = (int(round(x - jn[0] + use * mv[2 * i] + jp[0])),
                               int(round(y - jn[1] + use * mv[2 * i + 1] + jp[1])))
                if not all(0 <= qx < w and 0 <= qy < h for qx, qy in q.values()):
                    continue
                covered += 1
                for name, (qx, qy) in q.items():
                    if any(cp[min(h - 1, max(0, qy + dy)) * w + min(w - 1, max(0, qx + dx))] == c[i]
                           for dy in (-1, 0, 1) for dx in (-1, 0, 1)):
                        hits[name] += 1
            if covered < 100:
                failures.append("frame %d: only %d pixels changed, nothing moved" % (n, covered))
                continue
            moving, still = hits["motion"] / covered, hits["still"] / covered
            print("frame %d: %d pixels changed, %.3f found where the motion vectors say, %.3f standing still"
                  % (n, covered, moving, still))
            # (slow motion stays near its pixels anyway: the motion vectors
            # must at least halve what standing still misses)
            if moving < 0.9 or 1.0 - moving > 0.5 * (1.0 - still):
                failures.append("frame %d: %.3f of the changed pixels found where the motion vectors say" % (n, moving))

    if failures:
        print("\n".join(failures))
        return 1
    print("upscaler inputs: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
