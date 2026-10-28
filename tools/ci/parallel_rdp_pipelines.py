#!/usr/bin/env python3
"""parallel-rdp pipelines-ahead lane.

The core keeps the driver's pipeline cache and a list of the pipelines it
built in the system folder; the next session builds the listed ones on
the pipeline worker before the first frame needs them. lrhost_vk with
LRHOST_COUNT_PIPELINES=1 counts the pipelines created on the frame's
threads (any but the worker) and on the worker.

For each ROM and mode (threaded, single-threaded, 2x), in a folder of its
own:
  cold        no files: builds on the frame's threads, writes the list
  warm        the driver's cache deleted: builds none on the frame's threads
              that the list held (one the session is the first to need,
              where the GPU's timing takes another path, is built as before
              and added to the list)
  warm-cache  both files kept: none on the frame's threads
  foreign     the cache's device UUID changed (another driver): dropped,
              none on the frame's threads, the list still serves
  damaged     a byte of the list flipped: ignored, builds as a cold run,
              and writes a whole list again, which the next run takes
  cut         the list cut short: the same
Every run must present the same frames as the cold one. A list another
ROM wrote serves the pipelines the two share (shared).

  parallel_rdp_pipelines.py --core parallel_n64_libretro.so --host ./lrhost_vk \\
                            [--roms dir] [--out dir]
"""
import argparse, os, re, shutil, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import parallel_rdp_regress as regress

ROMS = ["cube32", "zbuf16", "tlut16", "pal8", "maskcm", "alphacov"]
MODES = {"threaded": ({}, []),
         "single-threaded": ({"PARALLEL_RDP_SINGLE_THREADED_COMMAND": "1"}, []),
         "2x": ({}, ["parallel-n64-upscaling=2x"])}
CACHE = "parallel_rdp_pipeline_cache.bin"
LIST = "parallel_rdp_pipelines.bin"
COUNTS = re.compile(r"pipelines: (\d+) built on the frame's threads, (\d+) on the pipeline worker")


def run(args, rom, sysdir, tag, env_extra, options):
    hashlog = os.path.join(sysdir, tag + ".hash")
    env = dict(os.environ, LRHOST_COUNT_PIPELINES="1", LRHOST_SYSTEM_DIR=sysdir,
               LRHOST_HASHLOG=hashlog, **env_extra)
    p = subprocess.run([args.host, args.core, rom, str(regress.FRAMES)] + regress.OPTIONS + options,
                       env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
    log = p.stdout.decode("utf-8", "replace")
    with open(os.path.join(sysdir, tag + ".log"), "w") as f:
        f.write(log)
    m = COUNTS.search(log)
    if p.returncode != 0 or not m:
        raise RuntimeError("%s: exit %d, %s" % (tag, p.returncode, "counted" if m else "no counts"))
    with open(hashlog) as f:
        frames = f.read()
    return int(m.group(1)), int(m.group(2)), frames


def listed(path):
    """the number of records in a list; 0 when there is none"""
    try:
        with open(path, "rb") as f:
            head = f.read(24)
    except OSError:
        return 0
    return int.from_bytes(head[16:20], sys.byteorder) if len(head) == 24 else 0


def damage(path, how):
    with open(path, "rb") as f:
        data = bytearray(f.read())
    if how == "flip":
        data[len(data) // 2] ^= 0x10
    elif how == "cut":
        data = data[:len(data) - 7]
    elif how == "uuid":
        data[0:16] = b"\xa5" * 16
    with open(path, "wb") as f:
        f.write(bytes(data))


def lane(args, name, mode, env_extra, options):
    rom = regress.fetch(args.roms, name)
    sysdir = os.path.abspath(os.path.join(args.out, name, mode))
    shutil.rmtree(sysdir, ignore_errors=True)
    os.makedirs(sysdir)
    errors = []
    lines = []

    def step(tag, want_frame, prepare=None):
        if prepare:
            prepare()
        before = listed(os.path.join(sysdir, LIST))
        frame, worker, frames = run(args, rom, sysdir, tag, env_extra, options)
        new = listed(os.path.join(sysdir, LIST)) - before
        lines.append("  %-12s %3d on the frame's threads, %3d on the worker, %3d new to the list"
                     % (tag, frame, worker, new))
        if frames != cold_frames[0] and cold_frames[0] is not None:
            errors.append("%s: frames differ from the cold run" % tag)
        if want_frame == "some":
            if frame == 0:
                errors.append("%s: built none on the frame's threads" % tag)
        elif want_frame == 0:
            # what the list did not hold is built as before
            if frame > max(new, 0):
                errors.append("%s: built %d on the frame's threads, %d of them listed"
                              % (tag, frame, frame - max(new, 0)))
        elif frame != want_frame:
            errors.append("%s: built %d on the frame's threads, wanted %d" % (tag, frame, want_frame))
        return frame, worker, frames

    cold_frames = [None]
    _, _, cold_frames[0] = step("cold", "some")
    if not os.path.exists(os.path.join(sysdir, LIST)):
        errors.append("cold: no list written")
    step("warm", 0, lambda: os.remove(os.path.join(sysdir, CACHE)))
    step("warm-cache", 0)
    step("foreign", 0, lambda: damage(os.path.join(sysdir, CACHE), "uuid"))
    for how in ("flip", "cut"):
        tag = "damaged" if how == "flip" else "cut"
        step(tag, "some", lambda h=how: (damage(os.path.join(sysdir, LIST), h),
                                     os.remove(os.path.join(sysdir, CACHE))))
        step(tag + "-next", 0, lambda: os.remove(os.path.join(sysdir, CACHE)))
    return lines, errors


def shared(args, first, second):
    """second, run where first's list is: builds on the frame's threads no
    more than it does alone, and the same frames."""
    base = os.path.abspath(os.path.join(args.out, "shared"))
    alone = os.path.join(base, "alone")
    both = os.path.join(base, "both")
    for d in (alone, both):
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
    a_frame, _, a_frames = run(args, regress.fetch(args.roms, second), alone, "alone", {}, [])
    run(args, regress.fetch(args.roms, first), both, "first", {}, [])
    os.remove(os.path.join(both, CACHE))
    b_frame, b_worker, b_frames = run(args, regress.fetch(args.roms, second), both, "second", {}, [])
    line = "  %s after %s: %d on the frame's threads (%d alone), %d on the worker" % (
        second, first, b_frame, a_frame, b_worker)
    errors = []
    if b_frames != a_frames:
        errors.append("shared: frames differ")
    if b_frame >= a_frame:
        errors.append("shared: the other ROM's list served nothing")
    return line, errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--roms", default="roms")
    ap.add_argument("--out", default="parallel_rdp_pipelines")
    args = ap.parse_args()
    args.core = os.path.abspath(args.core)
    args.host = os.path.abspath(args.host)
    failed = 0
    for name in ROMS:
        for mode, (env_extra, options) in MODES.items():
            try:
                lines, errors = lane(args, name, mode, env_extra, options)
            except Exception as e:
                lines, errors = [], [str(e)]
            print("%s %s: %s" % (name, mode, "FAIL" if errors else "ok"))
            for l in lines:
                print(l)
            for e in errors:
                print("  FAIL: " + e)
            failed += bool(errors)
    line, errors = shared(args, "tlut16", "pal8")
    print("shared: %s\n%s" % ("FAIL" if errors else "ok", line))
    for e in errors:
        print("  FAIL: " + e)
    failed += bool(errors)
    print("%d failed" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
