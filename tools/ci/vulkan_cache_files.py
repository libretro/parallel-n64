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

GLideN64 on Vulkan keeps its pipeline cache and its keys' journal for
every game (the journal made when a first key is met): a session that
meets nothing new rewrites neither.

  vulkan_cache_files.py --core parallel_n64_libretro.so --vk ./lrhost_vk
"""
import argparse, glob, os, struct, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gliden64_hle_dl as g  # noqa: E402

MAGIC = 0x43343647  # "G64C"
JOURNAL_MAGIC, JOURNAL_VERSION = 0x47364b4a, 2


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

        # GLideN64: a second session rewrites neither file (the journal
        # made only where a key was met)
        g.run(host, core, rom, tmp, "vulkan")
        kept = glob.glob(os.path.join(tmp, "sys", "ParallelN64", "GLideN64.vulkan.*"))
        if not any(f.endswith(".pipelines") for f in kept):
            fail("gliden64: %s, no pipeline cache" % sorted(os.path.basename(f) for f in kept))
        else:
            before = state(kept)
            g.run(host, core, rom, tmp, "vulkan")
            if state(kept) != before:
                fail("gliden64: a session that met nothing new wrote its files")

    if failures == 0:
        print("vulkan cache files: ok (glide64 %d combiners kept, migrated, left alone; gliden64 left alone)"
              % (len(parts[0]) // 72))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
