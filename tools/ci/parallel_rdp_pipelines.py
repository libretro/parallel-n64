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

For those runs lrhost has the core wait at load for the pipelines built
ahead (PARALLEL_RDP_TEST_WAIT_PIPELINES), so that what the frame's threads
build can be counted. A frontend is never kept waiting: with every known
pipeline queued and no wait, load must return at once, every frame must be
presented, and the frames must be the same (as a frontend).

CI runs every case on one ROM and a cold and a warm session on the rest;
--full runs every case on every ROM and mode (takes minutes, not seconds).

  parallel_rdp_pipelines.py --core parallel_n64_libretro.so --host ./lrhost_vk \\
                            [--roms dir] [--out dir] [--full]
"""
import argparse, os, re, shutil, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import parallel_rdp_regress as regress

ROMS = ["cube32", "zbuf16", "tlut16", "pal8", "maskcm", "alphacov"]
MODES = {"threaded": ({}, []),
         "single-threaded": ({"PARALLEL_RDP_SINGLE_THREADED_COMMAND": "1"}, []),
         "2x": ({}, ["parallel-n64-upscaling=2x"])}
# in the system folder, where GLideN64's files are too
CACHE = os.path.join("ParallelN64", "parallel_rdp_pipeline_cache.bin")
LIST = os.path.join("ParallelN64", "parallel_rdp_pipelines.bin")
COUNTS = re.compile(r"pipelines: (\d+) built on the frame's threads, (\d+) on the pipeline worker")


# Frames a run needs: the test ROMs have drawn by now.
FRAMES = 40
FULL_ROM = "cube32"


def run(args, rom, sysdir, tag, env_extra, options):
    hashlog = os.path.join(sysdir, tag + ".hash")
    env = dict(os.environ, LRHOST_COUNT_PIPELINES="1", LRHOST_SYSTEM_DIR=sysdir,
               LRHOST_HASHLOG=hashlog, **env_extra)
    p = subprocess.run([args.host, args.core, rom, str(FRAMES)] + regress.OPTIONS + options,
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


def lane(args, name, mode, env_extra, options, every_case):
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
    if not every_case:
        return lines, errors
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


def known_at_load(args):
    """A first run builds nothing on the frame's threads: everything it can
    need is built at load, with a temporal upscaler at that. One ROM: a software
    driver takes minutes over these."""
    sysdir = os.path.abspath(os.path.join(args.out, "known"))
    shutil.rmtree(sysdir, ignore_errors=True)
    os.makedirs(sysdir)
    frame, worker, _ = run(args, regress.fetch(args.roms, FULL_ROM), sysdir, "known",
                           {"LRHOST_KNOWN_PIPELINES": "1", "LRHOST_UPSCALER": "640,480"}, [])
    ok = frame == 0
    print("known at load: %s (%d on the frame's threads, %d on the worker)"
          % ("ok" if ok else "FAIL", frame, worker))
    return 0 if ok else 1


LOAD = re.compile(r"retro_load_game and context_reset: ([0-9.]+) ms")
RUN_MAX = re.compile(r"per retro_run: .* max ([0-9.]+) ms \| presented (\d+) duped (\d+)")
# A frontend must never be kept waiting for the pipelines built ahead: on
# a software driver building every known one takes over a minute, and a
# load that waited for them took that long. The bounds leave room for a
# slow runner and for a draw building its own pipeline.
FRONTEND_LOAD_MS = 5000.0
FRONTEND_RUN_MS = 5000.0
FRONTEND_WHOLE_S = 60.0
CLOSE = re.compile(r"context_destroy and retro_deinit: ([0-9.]+) ms")
BEGUN_CLOSING = re.compile(r"pipelines begun after content close began: (\d+)")
# Content close waits for the one compile already in the driver, which it
# cannot abandon, and starts none: what is still queued is dropped.
FRONTEND_CLOSE_MS = 20000.0


def as_frontend(args):
    """The core as a frontend runs it, every known pipeline queued at load
    and nothing waiting for them. Load returns at once, the machine runs
    from the first retro_run (every frame presented, none shown twice),
    the frames are those of a run that waited, and content close, with
    the worker still busy and draws' requests queued behind it, starts no
    compile."""
    rom = regress.fetch(args.roms, FULL_ROM)
    errors = []
    frames = {}
    load = run_max = None
    whole = 0.0
    for tag, extra in (("waited", {}), ("frontend", {"LRHOST_KNOWN_PIPELINES": "1", "LRHOST_AS_FRONTEND": "1"})):
        sysdir = os.path.abspath(os.path.join(args.out, "as-frontend", tag))
        shutil.rmtree(sysdir, ignore_errors=True)
        os.makedirs(sysdir)
        hashlog = os.path.join(sysdir, "hash")
        env = dict(os.environ, LRHOST_COUNT_PIPELINES="1", LRHOST_SYSTEM_DIR=sysdir, LRHOST_HASHLOG=hashlog, **extra)
        began = time.time()
        try:
            p = subprocess.run([args.host, args.core, rom, str(FRAMES)] + regress.OPTIONS, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
        except subprocess.TimeoutExpired:
            errors.append("%s: still running after 600 s" % tag)
            continue
        log = p.stdout.decode("utf-8", "replace")
        with open(os.path.join(sysdir, "log"), "w") as f:
            f.write(log)
        if p.returncode != 0:
            errors.append("%s: exit %d" % (tag, p.returncode))
            continue
        # (nothing presented, no log)
        frames[tag] = ""
        if os.path.exists(hashlog):
            with open(hashlog) as f:
                frames[tag] = f.read()
        if tag != "frontend":
            continue
        whole = time.time() - began
        load, run_max = LOAD.search(log), RUN_MAX.search(log)
        if not (load and run_max):
            errors.append("frontend: no timings")
            continue
        if float(load.group(1)) > FRONTEND_LOAD_MS:
            errors.append("retro_load_game and context_reset took %s ms" % load.group(1))
        if float(run_max.group(1)) > FRONTEND_RUN_MS:
            errors.append("a retro_run took %s ms" % run_max.group(1))
        if int(run_max.group(2)) != FRAMES or int(run_max.group(3)) != 0:
            errors.append("%s frames presented and %s shown twice, of %d" % (run_max.group(2), run_max.group(3), FRAMES))
        close, begun = CLOSE.search(log), BEGUN_CLOSING.search(log)
        if not (close and begun):
            errors.append("frontend: no close timings")
        else:
            if int(begun.group(1)) != 0:
                errors.append("%s pipelines begun after content close began" % begun.group(1))
            if float(close.group(1)) > FRONTEND_CLOSE_MS:
                errors.append("content close took %s ms" % close.group(1))
        if whole > FRONTEND_WHOLE_S:
            errors.append("the session took %.0f s" % whole)
    if len(frames) == 2 and frames["frontend"] != frames["waited"]:
        errors.append("frames differ from the run that waited")
    print("as a frontend: %s (load %s ms, longest retro_run %s ms, %s of %d frames presented, %.1f s in all)"
          % ("FAIL" if errors else "ok", load.group(1) if load else "?", run_max.group(1) if run_max else "?",
             run_max.group(2) if run_max else "?", FRAMES, whole))
    for e in errors:
        print("  FAIL: " + e)
    return 1 if errors else 0


def moved_over(args):
    """Files an earlier build left in the system folder itself are moved
    into ParallelN64 and used: the session builds none on the frame's
    threads that the list held, and leaves nothing behind."""
    rom = regress.fetch(args.roms, FULL_ROM)
    sysdir = os.path.abspath(os.path.join(args.out, "moved"))
    shutil.rmtree(sysdir, ignore_errors=True)
    os.makedirs(sysdir)
    run(args, rom, sysdir, "cold", {}, [])
    os.remove(os.path.join(sysdir, CACHE))
    old = os.path.join(sysdir, os.path.basename(LIST))
    os.rename(os.path.join(sysdir, LIST), old)
    count = listed(old)
    frame, worker, _ = run(args, rom, sysdir, "moved", {}, [])
    new = listed(os.path.join(sysdir, LIST)) - count
    errors = []
    if os.path.exists(old):
        errors.append("the list is still in the system folder")
    if count == 0 or new < 0:
        errors.append("the list was not taken (%d records before, %d more after)" % (count, new))
    if frame > max(new, 0):
        errors.append("built %d on the frame's threads" % frame)
    print("moved over: %s (%d records, %d on the frame's threads, %d on the worker)"
          % ("FAIL" if errors else "ok", count, frame, worker))
    for e in errors:
        print("  FAIL: " + e)
    return 1 if errors else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--roms", default="roms")
    ap.add_argument("--out", default="parallel_rdp_pipelines")
    ap.add_argument("--full", action="store_true", help="every case on every ROM and mode")
    args = ap.parse_args()
    args.core = os.path.abspath(args.core)
    args.host = os.path.abspath(args.host)
    failed = 0
    for name in ROMS:
        for mode, (env_extra, options) in MODES.items():
            if not args.full and name != FULL_ROM and mode != "threaded":
                continue
            try:
                lines, errors = lane(args, name, mode, env_extra, options,
                                     args.full or (name == FULL_ROM and mode == "threaded"))
            except Exception as e:
                lines, errors = [], [str(e)]
            print("%s %s: %s" % (name, mode, "FAIL" if errors else "ok"))
            for l in lines:
                print(l)
            for e in errors:
                print("  FAIL: " + e)
            failed += bool(errors)
    failed += known_at_load(args)
    failed += as_frontend(args)
    failed += moved_over(args)
    line, errors = shared(args, "tlut16", "pal8")
    print("shared: %s\n%s" % ("FAIL" if errors else "ok", line))
    for e in errors:
        print("  FAIL: " + e)
    failed += bool(errors)
    print("%d failed" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
