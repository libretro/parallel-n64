#!/usr/bin/env python3
"""GLideN64: a crash does not lose the combiners a session met, and a
session that meets nothing new writes nothing.

Each game has one shader cache file: a header, then records. A key's is
appended as the key is met (tools/test_combiner_journal.c covers writing
them), a program's binary at a clean exit, only for those not stored yet
(tools/test_gliden64_glsl_storage.c covers the file).

Here a session starts on a file a crash left: keys the ROM never meets on
its end, and a record cut short after them. It must build them, cut the
torn record off, and keep them. Another session starts on the files of
before (a keys journal and no cache file): their keys go into the new file
and the old files go. A session after that changes nothing.

  gliden64_keys_journal.py --core parallel_n64_libretro.so --host ./lrhost_gl [--roms krom-roms]
"""
import argparse, glob, os, struct, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parallel_rdp_regress import fetch  # noqa: E402

ROM = "alphacov"
FLIP = 0x0000000011111111
OPTIONS = ["parallel-n64-gfxplugin=gliden64", "parallel-n64-rspplugin=hle",
           "parallel-n64-cpucore=cached_interpreter",
           "parallel-n64-gliden64-EnableShadersStorage=True"]
KEYS_FORMAT_VERSION = 5


def run(a, rom, sysdir, frames):
    env = dict(os.environ, EGL_PLATFORM="surfaceless", LRHOST_SYSTEM_DIR=sysdir)
    p = subprocess.run([a.host, a.core, rom, str(frames)] + OPTIONS, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
    if p.returncode != 0:
        sys.stdout.write(p.stdout.decode("utf-8", "replace")[-2000:])
        raise RuntimeError("exit %d" % p.returncode)


def cache_file(sysdir):
    found = glob.glob(os.path.join(sysdir, "ParallelN64", "shaders", "GLideN64.*.cache"))
    return found[0] if found else None


def records(path):
    """the keys of the file's key records and of its program records, and
    whether its records are whole"""
    with open(path, "rb") as f:
        b = f.read()
    if b[:4] != b"GLNC":
        raise RuntimeError("%s: not a cache file" % path)
    p = 16
    for _ in range(2):
        n, = struct.unpack_from("<I", b, p)
        p += 4 + n
    keys, programs = [], []
    while p < len(b):
        kind = b[p:p + 1]
        if p + 10 > len(b):
            return keys, programs, False
        key, = struct.unpack_from("<Q", b, p + 2)
        if kind == b"K":
            keys.append(key)
            p += 10
        elif kind == b"B":
            if p + 22 > len(b):
                return keys, programs, False
            length, = struct.unpack_from("<i", b, p + 18)
            if length < 1 or p + 22 + length > len(b):
                return keys, programs, False
            programs.append(key)
            p += 22 + length
        else:
            return keys, programs, False
    return keys, programs, True


def check(path, wanted, what):
    keys, programs, whole = records(path)
    if not whole:
        print("FAILED: %s: the file is not whole records" % what)
        return 1
    missing = wanted - set(keys) - set(programs)
    if missing:
        print("FAILED: %s: %d of %d keys were lost" % (what, len(missing), len(wanted)))
        return 1
    if len(keys) != len(set(keys)) or len(programs) != len(set(programs)):
        print("FAILED: %s: a key or program stored twice" % what)
        return 1
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--roms", default="krom-roms")
    a = ap.parse_args()
    rom = fetch(a.roms, ROM)
    failures = 0
    with tempfile.TemporaryDirectory() as sysdir:
        run(a, rom, sysdir, 3)
        path = cache_file(sysdir)
        if path is None:
            print("FAILED: no shader cache file")
            return 1
        keys, programs, whole = records(path)
        met = set(keys) | set(programs)
        # combiners the ROM does not use
        lost = {k ^ FLIP for k in met} - met

        # a crash: their keys appended, then a record cut short
        with open(path, "ab") as f:
            for k in sorted(lost):
                f.write(b"K\0" + struct.pack("<Q", k))
            f.write(b"K\0\x01\x02\x03")
        run(a, rom, sysdir, 3)
        failures += check(path, lost | met, "after a crash")

        # nothing new: nothing written
        with open(path, "rb") as f:
            before = f.read()
        run(a, rom, sysdir, 3)
        with open(path, "rb") as f:
            if f.read() != before:
                print("FAILED: a session that met nothing new changed the file")
                failures += 1

        # the files of before: read into the file once, then gone
        base = path[:-len(".cache")]
        os.remove(path)
        with open(base + ".keys.journal", "w") as f:
            f.write("gliden64-combiner-keys %08x\n" % KEYS_FORMAT_VERSION)
            for k in sorted(lost):
                f.write("%016x 0\n" % k)
            f.write("0123456789ab")  # the line the crash cut short
        run(a, rom, sysdir, 3)
        if os.path.exists(base + ".keys.journal"):
            print("FAILED: the journal of before is still there")
            failures += 1
        if not os.path.exists(path):
            print("FAILED: no cache file from the files of before")
            return 1
        failures += check(path, lost | met, "from the files of before")
        if failures == 0:
            print("gliden64 keys journal: ok (%d keys from a crashed session, %d met)" % (len(lost), len(met)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
