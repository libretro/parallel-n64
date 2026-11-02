#!/usr/bin/env python3
"""parallel-RDP and its frontend glue must not use the C++ runtime.

They allocate with malloc and free and construct in place (Util::create,
Util::destroy), call through structs of function pointers instead of
virtual interfaces, and hold their static handles without destructors.
So none of their objects references operator new or delete, the ABI
support library (__cxa_*, the type_info vtables), a personality routine
or the standard library, and none defines a vtable, type_info or type
name. Any of those coming back, in a header used by them as much as in
their own code, fails this, in the objects of a finished build.

  check_cxx_runtime.py [dir ...]     (run after the build, from the repo root)
"""
import os, re, subprocess, sys

DIRS = sys.argv[1:] or ["mupen64plus-video-paraLLEl"]
RUNTIME = re.compile(r"^(_Znw|_Zna|_Zdl|_Zda|__cxa_|_ZTI|_ZTS|_ZTV|__gxx_personality|_ZSt|_ZNSt)")


def main():
    objects = 0
    found = []
    for d in DIRS:
        for root, _, files in os.walk(d):
            for f in sorted(files):
                if not f.endswith(".o"):
                    continue
                path = os.path.join(root, f)
                objects += 1
                out = subprocess.run(["nm", path], stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, text=True).stdout
                names = sorted(set(line.split()[-1] for line in out.splitlines()
                                   if line.split() and RUNTIME.match(line.split()[-1])))
                if names:
                    found.append("%s: %s" % (path, " ".join(names)))
    if not objects:
        print("check_cxx_runtime: no objects; build first")
        return 1
    for line in found:
        print("C++ runtime in %s" % line)
    print("check_cxx_runtime: %s" % ("ok (%d objects)" % objects if not found
                                     else "%d objects use it" % len(found)))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
