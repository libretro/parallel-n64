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

The test ROMs only ever have the GPU write whole 16-byte chunks of a
frame, so a synthetic cartridge made here covers the rest: its own boot
code fills a 16-bit frame with a pattern, has the RDP fill narrow
rectangles at odd x over it (so the readback sees chunks the GPU wrote
part of), copies the frame elsewhere with the CPU and shows the copy.
Its frames must be PARTIAL_FRAME however the RDRAM is uploaded, threaded
and single-threaded.

  parallel_rdp_rdram.py --core parallel_n64_libretro.so --host ./lrhost_vk \\
                        [--roms dir] [--out dir] [--only rom|partial-writes]
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

# The synthetic cartridge's frame, as lrhost_vk's hash log has it.
PARTIAL_FRAME = "640x240 0f03add558c6ed8a"


def mips(op, rs, rt, imm):
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)


def partial_writes_image(path):
    """A cartridge whose boot code (run from DMEM, as an IPL3 the core
    does not know is) draws partly written 16-byte chunks; see above."""
    t0, t1, t2, t3, t4, t5 = 8, 9, 10, 11, 12, 13
    lui = lambda rt, imm: mips(0x0f, 0, rt, imm)
    ori = lambda rt, rs, imm: mips(0x0d, rs, rt, imm)
    addiu = lambda rt, rs, imm: mips(0x09, rs, rt, imm)
    sw = lambda rt, off, base: mips(0x2b, base, rt, off)
    lw = lambda rt, off, base: mips(0x23, base, rt, off)
    bne = lambda rs, rt, off: mips(0x05, rs, rt, off)
    beq = lambda rs, rt, off: mips(0x04, rs, rt, off)
    li = lambda rt, v: [lui(rt, v >> 16), ori(rt, rt, v & 0xffff)]
    fb, copy, w, h = 0x100000, 0x200000, 320, 240
    end = 0xA0000000 | (fb + w * h * 2)

    cmds = [0x3F000000 | (2 << 19) | (w - 1), fb,              # 16-bit colour image
            0x2D000000, ((w * 4) << 12) | (h * 4),             # scissor
            0x2F000000 | (3 << 20), 0]                         # fill mode
    for i in range(24):
        x0, width = 1 + i * 13 + (i % 7), 1 + (i % 5)
        y0, height = 4 + (i % 6) * 9, 3 + (i % 4)
        colour = ((0x07c1 * (i + 3)) & 0xffff) | 1
        cmds += [0x27000000, 0,                                # sync pipe
                 0x37000000, (colour << 16) | colour,          # fill colour
                 0x36000000 | (((x0 + width - 1) * 4) << 12) | ((y0 + height - 1) * 4),
                 ((x0 * 4) << 12) | (y0 * 4)]                  # fill rectangle
    cmds += [0x29000000, 0]                                    # sync full

    prog = [lui(t0, 0xA000 | (fb >> 16))] + li(t1, end) + li(t2, 0x5A5AA5A5)
    prog += [sw(t2, 0, t0), addiu(t0, t0, 4), bne(t0, t1, -3), 0]
    prog += [lui(t4, 0xA410), ori(t5, 0, 2), sw(t5, 0xC, t4),  # DP from DMEM
             ori(t5, 0, 0x800), sw(t5, 0, t4),
             ori(t5, 0, 0x800 + 4 * len(cmds)), sw(t5, 4, t4)]
    prog += [lui(t0, 0xA000 | (fb >> 16))] + li(t1, end) + [lui(t3, 0xA000 | (copy >> 16))]
    prog += [lw(t2, 0, t0), addiu(t0, t0, 4), sw(t2, 0, t3), bne(t0, t1, -4), addiu(t3, t3, 4)]
    prog += [lui(t4, 0xA440)]
    for k, v in enumerate([0x3002, copy, w, 2, 0, 0x03E52239, 0x20D, 0xC15, 0x0C150C15,
                           0x006C02EC, 0x002501FF, 0x000E0204, 0x200, 0x400]):
        prog += li(t5, v) + [sw(t5, 4 * k, t4)]
    prog += [beq(0, 0, -1), 0]

    image = bytearray(0x100000)
    image[0:4] = (0x80371240).to_bytes(4, "big")
    image[8:12] = (0x80000400).to_bytes(4, "big")
    image[0x20:0x34] = b"RDP PARTIAL WRITES  "
    assert 0x40 + 4 * len(prog) <= 0x800 and 0x800 + 4 * len(cmds) <= 0x1000
    for base, words in ((0x40, prog), (0x800, cmds)):
        for k, word in enumerate(words):
            image[base + 4 * k:base + 4 * k + 4] = (word & 0xffffffff).to_bytes(4, "big")
    with open(path, "wb") as f:
        f.write(image)


def partial_lane(args):
    out = os.path.abspath(os.path.join(args.out, "partial-writes"))
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    rom = os.path.join(out, "partial-writes.z64")
    partial_writes_image(rom)
    errors = []
    for mode in ("threaded", "single-threaded"):
        for upload, extra in (("compared", {}), ("full", {"PARALLEL_RDP_FULL_RDRAM_UPLOAD": "1"})):
            env = dict(MODES[mode][0], **extra)
            log = run(args, rom, out, mode + "-" + upload, env, [], 40)
            # the frames once the copy is shown
            shown = set(" ".join(l.split()[2:4]) for l in log.splitlines()[-30:])
            if shown != {PARTIAL_FRAME}:
                errors.append("%s %s: frames %s, wanted %s" % (mode, upload, sorted(shown), PARTIAL_FRAME))
    shutil.rmtree(os.path.join(out, "sys"), ignore_errors=True)
    return errors


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
    ap.add_argument("--only", help="run only this ROM, or partial-writes")
    args = ap.parse_args()
    args.core = os.path.abspath(args.core)
    args.host = os.path.abspath(args.host)
    failed = 0
    for name in regress.ROMS:
        if args.only and args.only != name:
            continue
        for mode, (env_extra, options) in MODES.items():
            try:
                errors = lane(args, name, mode, env_extra, options)
            except Exception as e:
                errors = [str(e)]
            print("%-12s %-15s %s" % (name, mode, "FAIL" if errors else "ok"))
            for e in errors:
                print("  FAIL: " + e)
            failed += bool(errors)
    if not args.only or args.only == "partial-writes":
        try:
            errors = partial_lane(args)
        except Exception as e:
            errors = [str(e)]
        print("%-28s %s" % ("partial-writes", "FAIL" if errors else "ok"))
        for e in errors:
            print("  FAIL: " + e)
        failed += bool(errors)
    print("%d failed" % failed)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
