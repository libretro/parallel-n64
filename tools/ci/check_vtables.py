#!/usr/bin/env python3
"""Every class the renderers declare with a virtual function of its own
declared out of line must have its vtable in the build.

A class whose first such function (its key function) is never defined has
no vtable anywhere. Nothing references it while the class is never made,
until a compiler devirtualizes a call speculatively against every class it
can see: GCC 16 did so for a delete of Rice's FrameBufferManager, against
two derived classes nobody defined, and the MinGW link failed on their
vtables. Older compilers link such a build, so this checks for the classes
themselves, in the objects of a finished build.

  check_vtables.py [dir ...]     (run after the build, from the repo root)
"""
import os, re, subprocess, sys

DIRS = sys.argv[1:] or ["gles2rice", "glide2gl", "gles2n64"]
CLASS = re.compile(r"^\s*class\s+([A-Za-z_]\w*)\s*(?::[^{;]*)?\{(.*?)^\};", re.M | re.S)
OUT_OF_LINE_VIRTUAL = re.compile(r"\bvirtual\b[^;{}]*\)\s*(?:const\s*)?;")


def defined_vtables():
    found = set()
    for root, _, files in os.walk("."):
        for f in files:
            if f.endswith(".o"):
                out = subprocess.run(["nm", "--defined-only", os.path.join(root, f)],
                                     stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True).stdout
                found.update(m.group(1) for m in re.finditer(r"\b_ZTV(\d+\w+)", out))
    return found


def main():
    vtables = defined_vtables()
    if not vtables:
        print("check_vtables: no objects; build first")
        return 1
    missing = []
    for d in DIRS:
        for root, _, files in os.walk(d):
            for f in files:
                if not f.endswith((".h", ".hpp")):
                    continue
                path = os.path.join(root, f)
                with open(path, errors="replace") as fh:
                    text = fh.read()
                for m in CLASS.finditer(text):
                    name, body = m.group(1), m.group(2)
                    # pure virtuals (= 0) have no definition to miss
                    if not any("= 0" not in v.group(0) and "=0" not in v.group(0)
                               for v in OUT_OF_LINE_VIRTUAL.finditer(body)):
                        continue
                    if "%d%s" % (len(name), name) not in vtables:
                        missing.append("%s: %s" % (path, name))
    for m in missing:
        print("no vtable in the build for %s (a virtual function it declares is never defined)" % m)
    print("check_vtables: %s" % ("ok" if not missing else "%d classes" % len(missing)))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
