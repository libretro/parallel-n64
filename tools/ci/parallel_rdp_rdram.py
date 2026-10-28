#!/usr/bin/env python3
"""parallel-rdp RDRAM upload lane.

Without VK_EXT_external_memory_host the RDP's RDRAM is a copy that is
brought up to date page by page; where that copy is mapped, cached and
coherent, a page is only written when it differs. This lane runs every
ROM threaded, single-threaded and at 2x, plainly, with the frontend
writing RDRAM through the memory map before every frame (LRHOST_POKE, as
a cheat or achievement frontend may), and across a savestate round trip,
each once as it ships and once with every page uploaded whole
(PARALLEL_RDP_FULL_RDRAM_UPLOAD=1): the frames must be the same. Built
with PARALLEL_RDP_VERIFY_UPLOADS=1, the core also checks every upload and
readback against a full one and stops at the first difference.

  parallel_rdp_rdram.py --core parallel_n64_libretro.so --host ./lrhost_vk \\
                        [--roms dir] [--out dir]
"""
import argparse, os, shutil, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import parallel_rdp_regress as regress

MODES = {"threaded": ({}, []),
         "single-threaded": ({"PARALLEL_RDP_SINGLE_THREADED_COMMAND": "1"}, []),
         "2x": ({}, ["parallel-n64-upscaling=2x"])}
# over the frame buffers the test ROMs draw to, from past a page start;
# the ROMs that draw once and show it after see every write in the frames
POKE = "1049088,393216"
POKE_SHOWN = ("tlut16", "zbuf16")
SAVE_AT = 40


def run(args, rom, out, tag, env_extra, options, frames=regress.FRAMES):
    hashlog = os.path.join(out, tag + ".hash")
    env = dict(os.environ, LRHOST_SYSTEM_DIR=os.path.join(out, "sys"), LRHOST_HASHLOG=hashlog,
               **env_extra)
    p = subprocess.run([args.host, args.core, rom, str(frames)] + regress.OPTIONS + options,
                       env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
    log = p.stdout.decode("utf-8", "replace")
    with open(os.path.join(out, tag + ".log"), "w") as f:
        f.write(log)
    if p.returncode != 0:
        raise RuntimeError("%s: exit %d: %s" % (tag, p.returncode,
                           " / ".join(l for l in log.splitlines() if "parallel-rdp:" in l)[:300]))
    with open(hashlog) as f:
        return f.read()


def lane(args, name, mode, env_extra, options):
    rom = regress.fetch(args.roms, name)
    out = os.path.abspath(os.path.join(args.out, name, mode))
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    state = os.path.join(out, "state")
    errors = []
    frames = {}
    for upload, extra in (("compared", {}), ("full", {"PARALLEL_RDP_FULL_RDRAM_UPLOAD": "1"})):
        e = dict(env_extra, **extra)
        frames[upload, "plain"] = run(args, rom, out, upload + "-plain", e, options)
        frames[upload, "poke"] = run(args, rom, out, upload + "-poke", dict(e, LRHOST_POKE=POKE), options)
        run(args, rom, out, upload + "-save", dict(e, LRHOST_SAVE=state + "." + upload,
                                                   LRHOST_SAVE_AT=str(SAVE_AT)), options, SAVE_AT + 1)
        frames[upload, "state"] = run(args, rom, out, upload + "-state",
                                      dict(e, LRHOST_STATE=state + "." + upload), options)
    # (the states and the folder are large, and of no use once run)
    for upload in ("compared", "full"):
        if os.path.exists(state + "." + upload):
            os.remove(state + "." + upload)
    shutil.rmtree(os.path.join(out, "sys"), ignore_errors=True)
    for kind in ("plain", "poke", "state"):
        if frames["compared", kind] != frames["full", kind]:
            errors.append("%s: frames differ from a full upload" % kind)
    if name in POKE_SHOWN and frames["compared", "poke"] == frames["compared", "plain"]:
        errors.append("poke: the frontend's writes did not reach the frames")
    return errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--roms", default="roms")
    ap.add_argument("--out", default="parallel_rdp_rdram")
    args = ap.parse_args()
    args.core = os.path.abspath(args.core)
    args.host = os.path.abspath(args.host)
    failed = 0
    for name in regress.ROMS:
        for mode, (env_extra, options) in MODES.items():
            try:
                errors = lane(args, name, mode, env_extra, options)
            except Exception as e:
                errors = [str(e)]
            print("%-12s %-15s %s" % (name, mode, "FAIL" if errors else "ok"))
            for e in errors:
                print("  FAIL: " + e)
            failed += bool(errors)
    print("%d failed" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
