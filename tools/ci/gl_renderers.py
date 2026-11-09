#!/usr/bin/env python3
"""GL renderers lane.

Runs the core's OpenGL renderers (Glide64, GLideN64, Rice, glN64) under
tools/lrhost_gl, headless on Mesa's llvmpipe (EGL_PLATFORM=surfaceless, no
GPU), over krom's RDP test ROMs, fetched as parallel_rdp_regress.py fetches
them (same folder, same pinned commit). The GL renderers are not bit-exact
across drivers, so the frames are not held to stored hashes; the lane holds them to:

  * every renderer running every ROM to the end, and all but glN64 (which
    presents nothing for display lists sent straight to the RDP) presenting
    frames: Glide64 and Rice used to crash on a display list given as a
    KSEG address, as homebrew gives it, glN64's GBI state, merged into
    GLideN64's under -fcommon, crashed at exit, and Rice crashed in the
    driver putting a frame the CPU left in RDRAM on screen with no program
    bound (shade32, tlut16 and zbuf16 draw once, so Rice shows RDRAM after);
  * every renderer running on when the frontend moves it to a new
    context every 10 runs and calls context_reset (lrhost_gl's
    LRHOST_GL_SWITCH_EVERY), with and without context_destroy first, as
    RetroArch does when its video driver restarts - threaded video
    toggled moves a hardware core between two contexts - and GLideN64
    drawing the same frames as without: Glide64, Rice and glN64 crashed
    in the second context_reset, which drove GLideN64's window change
    whichever renderer was in use, and GLideN64 deleted the lost
    context's objects by name in the new one;
  * GLideN64 drawing the same frames whatever its colour buffer copy to
    RDRAM is set to (Off, Sync, Async, TripleBuffer): the copy writes RDRAM
    and must not disturb what is drawn, its pixel buffers or the vertex
    ring the draws read;
  * GLideN64 drawing the same frames with its vertex rings shrunk to the
    smallest they go (GLIDEN64_RING_BYTES) as with the full 8 MB, which
    these tests never go round: the small rings go round many times, so
    every draw placed at a segment start or back at 0 must index the
    vertices it wrote. The draws used to index from 0 after a move to a
    segment start, and the segments did not start on a vertex: geometry
    torn into garbage triangles once a game went past the first segment.
    Both with persistent mapped rings (GL 4.4 / ARB_buffer_storage) and,
    with Mesa told to hide them, the rings written through
    glMapBufferRange.

CI runs the lane a second time with the core and host built with
SANITIZER=address,undefined (glN64 used to read its texture cache after
freeing it when the context went); any sanitizer report in a run's output
fails it, so undefined behaviour fails the lane even where it does not
crash.

  gl_renderers.py --core parallel_n64_libretro.so --host ./lrhost_gl [--roms krom-roms]
"""
import argparse, os, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parallel_rdp_regress import fetch  # noqa: E402

ROMS = ["cube16", "rottri32", "rottri32aa", "shade32", "tlut16", "zbuf16"]
PLUGINS = ["glide64", "gliden64", "rice", "gln64"]
PRESENTS_NOTHING = {"gln64"}
COPY_MODES = ["Off", "Sync", "Async", "TripleBuffer"]
FRAMES = 40
SMALL_RING = "49152"
# Mesa without buffer storage: GLideN64 writes its rings through glMapBufferRange
NO_BUFFER_STORAGE = {"MESA_GL_VERSION_OVERRIDE": "4.3",
                     "MESA_EXTENSION_OVERRIDE": "-GL_ARB_buffer_storage"}


def run(host, core, rom, plugin, extra, tmp, env_extra=None):
    hashlog = os.path.join(tmp, "hash")
    if os.path.exists(hashlog):
        os.remove(hashlog)
    env = dict(os.environ, EGL_PLATFORM="surfaceless", LRHOST_HASHLOG=hashlog)
    env.update(env_extra or {})
    args = [host, core, rom, str(FRAMES),
            "parallel-n64-gfxplugin=" + plugin,
            "parallel-n64-rspplugin=hle",
            "parallel-n64-cpucore=cached_interpreter"] + extra
    p = subprocess.run(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       timeout=600)
    out = p.stdout.decode("utf-8", "replace")
    if "runtime error:" in out or "ERROR: AddressSanitizer" in out:
        # a sanitizer report: the run fails even if it got to the end
        return p.returncode or 1, [], out
    frames = []
    if os.path.exists(hashlog):
        with open(hashlog) as f:
            frames = [line.split()[-1] for line in f if line.strip()]
    return p.returncode, frames, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--roms", default="krom-roms")
    a = ap.parse_args()

    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        # a frontend that gives no context: the load fails, nothing crashes
        path = fetch(a.roms, ROMS[0])
        for plugin in PLUGINS:
            rc, frames, out = run(a.host, a.core, path, plugin, [], tmp, {"LRHOST_REFUSE_HW_RENDER": "1"})
            ok = rc == 1 and "retro_load_game failed" in out
            print("%-10s %-9s no context: %s" % (ROMS[0], plugin, "load fails" if ok else "FAILED (exit %d)" % rc))
            failures += not ok
        for rom in ROMS:
            path = fetch(a.roms, rom)
            for plugin in PLUGINS:
                rc, frames, out = run(a.host, a.core, path, plugin, [], tmp)
                ok = rc == 0 and (len(frames) > 0 or plugin in PRESENTS_NOTHING)
                print("%-10s %-9s %s" % (rom, plugin, "ok" if ok else "FAILED (exit %d, %d frames)" % (rc, len(frames))))
                if not ok:
                    failures += 1
                    sys.stdout.write(out[-2000:])
            for plugin in PLUGINS:
                rc, base, out = run(a.host, a.core, path, plugin, [], tmp)
                for name, env_extra in (("switch", {"LRHOST_GL_SWITCH_EVERY": "10"}),
                                        ("destroy+switch", {"LRHOST_GL_SWITCH_EVERY": "10",
                                                            "LRHOST_GL_SWITCH_DESTROY": "1"})):
                    rc, frames, out = run(a.host, a.core, path, plugin, [], tmp, env_extra)
                    ok = rc == 0 and len(frames) == len(base)
                    if ok and plugin == "gliden64" and frames != base:
                        ok = False
                    print("%-10s %-9s context %-14s %s" % (rom, plugin, name,
                          "ok" if ok else "FAILED (exit %d, %d of %d frames%s)" % (
                              rc, len(frames), len(base),
                              ", other frames" if rc == 0 and len(frames) == len(base) else "")))
                    if not ok:
                        failures += 1
                        sys.stdout.write(out[-2000:])
            reference = None
            for mode in COPY_MODES:
                rc, frames, out = run(a.host, a.core, path, "gliden64",
                                      ["parallel-n64-gliden64-EnableFBEmulation=True",
                                       "parallel-n64-gliden64-EnableCopyColorToRDRAM=" + mode], tmp)
                if rc != 0 or not frames:
                    print("%-10s gliden64 copy %-12s FAILED (exit %d)" % (rom, mode, rc))
                    sys.stdout.write(out[-2000:])
                    failures += 1
                    continue
                if reference is None:
                    reference = frames
                elif frames != reference:
                    print("%-10s gliden64 copy %-12s draws other frames than copy Off" % (rom, mode))
                    failures += 1
                    continue
                print("%-10s gliden64 copy %-12s ok" % (rom, mode))
            for name, mesa in (("buffer storage", {}), ("map range", NO_BUFFER_STORAGE)):
                runs = []
                for ring in (None, SMALL_RING):
                    env_extra = dict(mesa)
                    if ring is not None:
                        env_extra["GLIDEN64_RING_BYTES"] = ring
                    runs.append(run(a.host, a.core, path, "gliden64", [], tmp, env_extra))
                bad = [r for r in runs if r[0] != 0 or not r[1]]
                if bad:
                    print("%-10s gliden64 ring %-14s FAILED (exit %d)" % (rom, name, bad[0][0]))
                    sys.stdout.write(bad[0][2][-2000:])
                    failures += 1
                elif runs[0][1] != runs[1][1]:
                    differ = sum(1 for x, y in zip(runs[0][1], runs[1][1]) if x != y)
                    print("%-10s gliden64 ring %-14s draws other frames with a small ring (%d of %d differ)"
                          % (rom, name, differ, len(runs[0][1])))
                    failures += 1
                else:
                    print("%-10s gliden64 ring %-14s ok" % (rom, name))

    if failures:
        print("%d failures" % failures)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
