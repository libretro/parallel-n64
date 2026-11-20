#!/usr/bin/env python3
"""The Vulkan plugins' cache files: written where something changed, left
as they are where nothing did.

Glide64 on Vulkan keeps one file a game, glide64-vulkan_<crcs>.cache: a
header, the combiners met, then the driver's pipeline cache. A first
session writes it, and a second that meets nothing new leaves it byte for
byte, unopened for writing. The two files a game had before (its records,
<name>.keys, and the driver's cache beside them, <name>.keys.pipelines)
are read into it once and removed. tools/test_glitch64_vk_cache.c covers
the file.

GLideN64 on Vulkan keeps one file a game too, GLideN64.<name>.Vulkan.cache
in the shaders folder: a header, the driver's pipeline cache, then the
keys met, appended as they are. A second session leaves it; one after a
crash cuts the torn record off and keeps the keys. A game without it
builds the keys of the journal every game shared before
(GLideN64.vulkan.keys.journal) and keeps only those it draws with; the
pipeline cache shared before goes, the journal stays for the other games.
tools/test_gliden64_vk_cache.c covers the file.

  vulkan_cache_files.py --core parallel_n64_libretro.so --vk ./lrhost_vk
"""
import argparse, glob, os, struct, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gliden64_hle_dl as g  # noqa: E402

MAGIC = 0x43343647  # "G64C"
JOURNAL_MAGIC, JOURNAL_VERSION = 0x47364b4a, 2
GLNV, GLNV_VERSION = 0x564E4C47, 1  # "GLNV"
OLD_JOURNAL_VERSION = 1
FLIP = 0x0000000011111111
STORAGE = ("parallel-n64-gliden64-EnableShadersStorage=True",)


def state(paths):
    """each file's bytes and when it was last written"""
    out = {}
    for p in paths:
        with open(p, "rb") as f:
            out[p] = (f.read(), os.stat(p).st_mtime_ns)
    return out


def whole(path):
    """the records and the pipeline cache of a whole cache file, or None"""
    with open(path, "rb") as f:
        b = f.read()
    if len(b) < 16:
        return None
    magic, version, count, pipes = struct.unpack_from("<4I", b)
    if magic != MAGIC or version != JOURNAL_VERSION or 16 + count * 72 + pipes != len(b):
        return None
    return b[16:16 + count * 72], b[16 + count * 72:]


def gliden64_file(path):
    """the pipeline cache and the keys of a whole GLideN64 file, or None"""
    with open(path, "rb") as f:
        b = f.read()
    if len(b) < 12:
        return None
    magic, version, pipes = struct.unpack_from("<3I", b)
    if magic != GLNV or version != GLNV_VERSION or 12 + pipes > len(b) or (len(b) - 12 - pipes) % 10:
        return None
    keys = []
    for p in range(12 + pipes, len(b), 10):
        if b[p:p + 1] != b"K":
            return None
        keys.append(struct.unpack_from("<Q", b, p + 2)[0])
    return b[12:12 + pipes], keys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--vk", required=True)
    a = ap.parse_args()
    core, host = os.path.abspath(a.core), os.path.abspath(a.vk)
    failures = 0

    def fail(what):
        nonlocal failures
        print("FAILED: " + what)
        failures += 1

    with tempfile.TemporaryDirectory() as tmp:
        rom = os.path.join(tmp, "cache.z64")
        g.cartridge(rom, 0, "Perfect Dark")
        shaders = os.path.join(tmp, "sys", "ParallelN64", "shaders")

        # Glide64: a first session writes the file, a second leaves it
        g.run(host, core, rom, tmp, "opengl", "glide64", "vulkan")
        files = glob.glob(os.path.join(shaders, "glide64-vulkan_*"))
        caches = [f for f in files if f.endswith(".cache")]
        if len(caches) != 1 or len(files) != 1:
            fail("glide64: %s, not one cache file" % sorted(os.path.basename(f) for f in files))
            return 1
        cache = caches[0]
        parts = whole(cache)
        if parts is None or not parts[0]:
            fail("glide64: the cache file is not whole, or has no combiners")
            return 1
        before = state([cache])
        g.run(host, core, rom, tmp, "opengl", "glide64", "vulkan")
        if state([cache]) != before:
            fail("glide64: a session that met nothing new wrote the cache file")

        # the files of before: read in once, then gone
        base = cache[:-len(".cache")] + ".keys"
        with open(base, "wb") as f:
            f.write(struct.pack("<2I", JOURNAL_MAGIC, JOURNAL_VERSION) + parts[0])
        with open(base + ".pipelines", "wb") as f:
            f.write(parts[1])
        os.remove(cache)
        g.run(host, core, rom, tmp, "opengl", "glide64", "vulkan")
        if os.path.exists(base) or os.path.exists(base + ".pipelines"):
            fail("glide64: the files of before are still there")
        if not os.path.exists(cache) or whole(cache) is None or whole(cache)[0] != parts[0]:
            fail("glide64: the file of the files of before does not hold their combiners")

        # GLideN64: one file a game, the keys met in it; a second session
        # leaves it
        root = os.path.join(tmp, "sys", "ParallelN64")
        g.run(host, core, rom, tmp, "vulkan", options=STORAGE)
        files = glob.glob(os.path.join(shaders, "GLideN64.*"))
        if len(files) != 1 or not files[0].endswith(".Vulkan.cache"):
            fail("gliden64: %s, not one cache file" % sorted(os.path.basename(f) for f in files))
            return 1
        cache = files[0]
        if glob.glob(os.path.join(root, "GLideN64.vulkan.*")):
            fail("gliden64: the files every game shared were written")
        gparts = gliden64_file(cache)
        if gparts is None or not gparts[1] or len(set(gparts[1])) != len(gparts[1]):
            fail("gliden64: the cache file is not whole, has no keys or one twice")
            return 1
        met = set(gparts[1])
        before = state([cache])
        g.run(host, core, rom, tmp, "vulkan", options=STORAGE)
        if state([cache]) != before:
            fail("gliden64: a session that met nothing new wrote the cache file")

        # a crash's: keys the ROM does not meet appended, then a record cut
        # short; the record cut off, the keys kept
        lost = {k ^ FLIP for k in met} - met
        with open(cache, "ab") as f:
            for k in sorted(lost):
                f.write(b"K\0" + struct.pack("<Q", k))
            f.write(b"K\0\x01\x02\x03")
        g.run(host, core, rom, tmp, "vulkan", options=STORAGE)
        after = gliden64_file(cache)
        if after is None or set(after[1]) != met | lost or len(after[1]) != len(met | lost):
            fail("gliden64: after a crash: %s" % ("not whole" if after is None else "the keys %d, not %d"
                 % (len(after[1]), len(met | lost))))

        # the files every game shared: their keys built, those drawn with
        # kept; the pipeline cache gone, the journal left to the others
        os.remove(cache)
        journal = os.path.join(root, "GLideN64.vulkan.keys.journal")
        text = "gliden64-combiner-keys %08x\n" % OLD_JOURNAL_VERSION
        text += "".join("%016x 0\n" % k for k in sorted(met | lost))
        with open(journal, "w") as f:
            f.write(text)
        with open(journal + ".pipelines", "wb") as f:
            f.write(b"\0" * 64)
        g.run(host, core, rom, tmp, "vulkan", options=STORAGE)
        migrated = gliden64_file(cache) if os.path.exists(cache) else None
        if migrated is None or set(migrated[1]) != met:
            fail("gliden64: from the journal of before: %s" % ("no whole file" if migrated is None else
                 "%d keys, not the %d drawn with" % (len(migrated[1]), len(met))))
        if os.path.exists(journal + ".pipelines"):
            fail("gliden64: the pipeline cache every game shared is still there")
        with open(journal) as f:
            if f.read() != text:
                fail("gliden64: the journal every game shared was changed")

    if failures == 0:
        print("vulkan cache files: ok (glide64 %d combiners kept, migrated, left alone; gliden64 %d keys kept, "
              "%d after a crash, migrated, left alone)" % (len(parts[0]) // 72, len(met), len(lost)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
