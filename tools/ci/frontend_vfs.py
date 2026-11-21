#!/usr/bin/env python3
"""The core's files through the frontend's VFS.

The core reads its content itself (need_fullpath), so on a frontend whose
content is at a path only its VFS opens (Android's saf:// ones) it must
read through the frontend's VFS, as every file of its does. lrhost with
LRHOST_VFS=1 is such a frontend: it gives a VFS and the content as
saf://<path>, by path only, which no stdio opens.

  - a rom at a saf:// path loads and draws the frames a plain run draws;
  - so does one in a zip there, opened in place;
  - a GL renderer's shader cache is made through it, in the system folder
    at a saf:// path too, and a file of before there found and removed;
  - nothing is tried through stdio (which would make a "saf:" folder).

  frontend_vfs.py --core parallel_n64_libretro.so --host ./lrhost --gl ./lrhost_gl [--roms krom-roms]
"""
import argparse, os, re, subprocess, sys, tempfile, zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parallel_rdp_regress import fetch  # noqa: E402

FRAMES = 60
OPTS = ["parallel-n64-rspplugin=hle", "parallel-n64-cpucore=cached_interpreter"]


def run(host, core, rom, sysdir, vfs, plugin):
    env = dict(os.environ, EGL_PLATFORM="surfaceless", LRHOST_SYSTEM_DIR=sysdir)
    env.pop("LRHOST_VFS", None)
    if vfs:
        env["LRHOST_VFS"] = "1"
    # run from a folder of its own: a saf:// path tried through stdio
    # makes a "saf:" there
    cwd = os.path.join(sysdir + ".cwd")
    os.makedirs(cwd, exist_ok=True)
    p = subprocess.run([host, core, rom, str(FRAMES), "parallel-n64-gfxplugin=" + plugin] + OPTS,
                       env=env, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    if os.listdir(cwd):
        return -1, None, "tried through stdio: %s\n" % os.listdir(cwd) + p.stdout.decode(errors="replace")
    sig = re.search(rb"sig ([0-9a-f]+)", p.stdout)
    return p.returncode, sig.group(1).decode() if sig else None, p.stdout.decode(errors="replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--gl")
    ap.add_argument("--roms", default="krom-roms")
    a = ap.parse_args()
    core, host = os.path.abspath(a.core), os.path.abspath(a.host)
    rom = os.path.abspath(fetch(a.roms, "cube16"))
    failures = []

    def check(ok, what, out=""):
        if not ok:
            failures.append(what)
            print("FAILED: " + what)
            if out:
                print(out[-2000:])

    with tempfile.TemporaryDirectory() as tmp:
        rc, plain, out = run(host, core, rom, os.path.join(tmp, "plain"), False, "angrylion")
        check(rc == 0 and plain, "a plain run", out)
        rc, sig, out = run(host, core, rom, os.path.join(tmp, "vfs"), True, "angrylion")
        check(rc == 0 and sig == plain, "a rom at a saf:// path: loaded, the plain run's frames (%s, %s)"
              % (sig, plain), out)

        zipped = os.path.join(tmp, "cube16.zip")
        with zipfile.ZipFile(zipped, "w", zipfile.ZIP_DEFLATED) as z:
            z.write(rom, "cube16.n64")
        rc, sig, out = run(host, core, zipped, os.path.join(tmp, "zip"), True, "angrylion")
        check(rc == 0 and sig == plain, "a zipped rom at a saf:// path: loaded, the plain run's frames (%s)"
              % sig, out)

        if a.gl:
            sysdir = os.path.join(tmp, "gl")
            shaders = os.path.join(sysdir, "ParallelN64", "shaders")
            os.makedirs(shaders)
            before = os.path.join(shaders, "glide64_0123456789abcdef.keys")
            with open(before, "wb") as f:
                f.write(b"a file of before")
            rc, sig, out = run(os.path.abspath(a.gl), core, rom, sysdir, True, "glide64")
            cache = os.path.join(shaders, "glide64-opengl.cache")
            check(rc == 0 and os.path.exists(cache), "a GL renderer's shader cache made through the VFS", out)
            check(not os.path.exists(before), "a file of before found in the folder and removed through the VFS")

    print("frontend vfs: %s" % ("ok" if not failures else "%d FAILED" % len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
