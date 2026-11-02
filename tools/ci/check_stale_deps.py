#!/usr/bin/env python3
"""An incremental build survives sources and headers that were removed
or renamed since the last build.

Each object's .d file (gcc -MMD) names the source and headers it was
built from. When one of those goes away (a header deleted, a .cpp that
became a .c), the next make must rebuild the object from what is there,
not stop with "No rule to make target". This writes such a .d for one
object and checks that make plans to rebuild it from its current source.

  check_stale_deps.py        (from the repo root; builds nothing)
"""
import os, subprocess, sys

OBJ = "mupen64plus-video-paraLLEl/parallel-rdp/vulkan/cookie.o"
SRC = "mupen64plus-video-paraLLEl/parallel-rdp/vulkan/cookie.c"
DEP = OBJ[:-2] + ".d"
STALE = "%s: \\\n %s \\\n %s\n" % (
    OBJ, OBJ[:-2] + "_removed.cpp", "mupen64plus-video-paraLLEl/parallel-rdp/vulkan/removed_header.hpp")


def main():
    saved = None
    if os.path.exists(DEP):
        with open(DEP) as f:
            saved = f.read()
    try:
        with open(DEP, "w") as f:
            f.write(STALE)
        # an object older than nothing it names is rebuilt; make sure it is
        if os.path.exists(OBJ):
            os.utime(OBJ, (0, 0))
        r = subprocess.run(["make", "-n", "platform=unix", "HAVE_PARALLEL=1", OBJ],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    finally:
        if saved is None:
            os.remove(DEP)
        else:
            with open(DEP, "w") as f:
                f.write(saved)
    if r.returncode != 0:
        print(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else "make failed")
        print("check_stale_deps: FAIL: make stops on a removed source or header")
        return 1
    if SRC not in r.stdout:
        print("check_stale_deps: FAIL: %s is not rebuilt from %s" % (OBJ, SRC))
        return 1
    print("check_stale_deps: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
