#!/usr/bin/env python3
"""Regression test: run snapper64 (github.com/HailToDodongo/snapper64, MIT)
under the core and check what its result screen says.

snapper64 renders a set of RDP/RCP test cases and compares each against a
snapshot taken on real hardware, then lists "passed/total" per group. This
script presses A on "Run All", lets it finish, and compares the result
column of the final frame, line by line, against the expected one. It also
boots the ROM at each resolution scale and checks that the menu is on
screen, which is what catches the renderer writing outside a colour image
(libdragon then dies with its exception screen, or the frame goes black).

Nothing here needs a GPU: the angrylion renderer and tools/lrhost are
software only.

  snapper64_regress.py --core parallel_n64_libretro.so --host ./lrhost \\
                       --rom snapper64.z64 [--out artifacts] [--update]

--update rewrites the expectations file from what this run produced; read
the frame it saves before committing the result.
"""
import argparse, hashlib, json, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
EXPECT = os.path.join(HERE, "snapper64_expected.json")

# RetroPad B is the N64 A button
PRESS_A = "300-306:0"
RUN_ALL_FRAMES = 23000
CPU_CORES = ["pure_interpreter", "cached_interpreter",
             "dynamic_recompiler", "dynamic_recompiler_ari64"]
SCALES = ["1x", "2x", "4x"]


def run(args, frames, dump_at, options, env_extra, workdir):
    env = dict(os.environ, LRHOST_DUMPDIR=workdir,
               LRHOST_DUMP_FROM=str(dump_at), LRHOST_DUMP_TO=str(dump_at))
    env.update(env_extra)
    cmd = [args.host, args.core, args.rom, str(frames + 1)] + \
          ["%s=%s" % kv for kv in options.items()]
    p = subprocess.run(cmd, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, timeout=1500)
    raw = os.path.join(workdir, "%04d.raw" % dump_at)
    if p.returncode != 0 or not os.path.exists(raw):
        raise RuntimeError("host failed (%d):\n%s" % (p.returncode, p.stdout[-2000:]))
    w, h = [int(v) for v in open(raw + ".size").read().split()]
    data = open(raw, "rb").read()
    os.remove(raw)
    return w, h, data, p.stdout


def save_ppm(path, w, h, data):
    out = bytearray(w * h * 3)
    for i in range(w * h):           # XRGB8888, little endian: B G R X
        out[3 * i] = data[4 * i + 2]
        out[3 * i + 1] = data[4 * i + 1]
        out[3 * i + 2] = data[4 * i]
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        f.write(out)


def result_lines(w, h, data):
    """The text lines of the result column, as one hash each.

    The column is the right-hand part of the menu. A line is a run of rows
    with lit pixels; its hash covers each pixel reduced to one bit per
    channel, so that a digit changing or a line turning from green to red
    both show, while dither noise does not."""
    x0, x1 = (w * 455) // 640, (w * 630) // 640
    rows = []
    for y in range((h * 154) // 240):      # the spinning cube sits below the list
        bits = bytearray()
        lit = False
        for x in range(x0, x1):
            o = 4 * (y * w + x)
            b, g, r = data[o] > 96, data[o + 1] > 96, data[o + 2] > 96
            bits.append((r << 2) | (g << 1) | b)
            lit = lit or r or g or b
        rows.append((lit, bytes(bits)))
    lines, cur = [], None
    for lit, bits in rows:
        if lit:
            cur = (cur or b"") + bits
        elif cur is not None:
            lines.append(hashlib.sha1(cur).hexdigest()[:16])
            cur = None
    if cur is not None:
        lines.append(hashlib.sha1(cur).hexdigest()[:16])
    return lines


def lit_fraction(data):
    lit = sum(1 for i in range(0, len(data), 4 * 53)
              if data[i] > 32 or data[i + 1] > 32 or data[i + 2] > 32)
    return lit / max(1, len(data) // (4 * 53))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--rom", required=True)
    ap.add_argument("--out", default="snapper64-artifacts")
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--cores", default=",".join(CPU_CORES))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    expected = json.load(open(EXPECT)) if os.path.exists(EXPECT) else {}
    produced, failures = {}, []

    with tempfile.TemporaryDirectory() as tmp:
        base = {"parallel-n64-gfxplugin": "angrylion",
                "parallel-n64-rspplugin": "cxd4",
                "parallel-n64-angrylion-multithread": "all threads"}

        # 1. every hardware snapshot, under every CPU core
        for cpu in args.cores.split(","):
            name = "results:" + cpu
            opts = dict(base, **{"parallel-n64-cpucore": cpu})
            w, h, data, _ = run(args, RUN_ALL_FRAMES, RUN_ALL_FRAMES, opts,
                                {"LRHOST_INPUT": PRESS_A}, tmp)
            lines = result_lines(w, h, data)
            produced[name] = lines
            save_ppm(os.path.join(args.out, name.replace(":", "_") + ".ppm"), w, h, data)
            want = expected.get(name)
            if want is None:
                failures.append("%s: no expectation recorded" % name)
            elif want != lines:
                bad = [i for i in range(max(len(want), len(lines)))
                       if i >= len(want) or i >= len(lines) or want[i] != lines[i]]
                failures.append("%s: result lines %s differ from the expected "
                                "screen (line 0 is the column heading, the last "
                                "line the total) - see the saved frame" % (name, bad))
            print("%-36s %s" % (name, "ok" if want == lines else "DIFFERENT"))

        # 2. the menu at every resolution scale, with and without a lent
        #    software framebuffer
        for scale in SCALES:
            for lend in (False, True):
                name = "menu:%s:%s" % (scale, "lent" if lend else "own")
                opts = dict(base, **{"parallel-n64-cpucore": "dynamic_recompiler_ari64",
                                     "parallel-n64-upscaling": scale})
                w, h, data, log = run(args, 600, 600, opts,
                                      {"LRHOST_SWFB": "1"} if lend else {}, tmp)
                frac = lit_fraction(data)
                # black is 0%, libdragon's exception screen close to 100%
                ok = 0.05 < frac < 0.60
                if lend and "presented from the lent buffer 0" in log:
                    ok = False
                    failures.append("%s: no frame came back in the lent buffer" % name)
                if not ok:
                    save_ppm(os.path.join(args.out, name.replace(":", "_") + ".ppm"), w, h, data)
                    failures.append("%s: %.0f%% of the frame is lit - the menu "
                                    "is not on screen" % (name, frac * 100))
                print("%-36s %s (%dx%d, %.0f%% lit)" % (name, "ok" if ok else "BROKEN", w, h, frac * 100))

    if args.update:
        json.dump(produced, open(EXPECT, "w"), indent=1, sort_keys=True)
        print("wrote", EXPECT)
        return 0
    for f in failures:
        print("FAIL:", f)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
