#!/usr/bin/env python3
"""Glide64 on GL: one shader cache file every game shares.

shaders/glide64-opengl.cache: a header (the magic, the shaders' version),
then records of the combiners compiled: the key, the source's length, the
games that compiled it (a bit of each, from its CRCs), the source. At load
a game builds its own and leaves the rest as they are.

  - a first session writes the one file, its records this game's;
  - a second builds every one at load (PARALLEL_N64_COMBINER_STRICT aborts
    if a draw has to compile) and leaves the file byte for byte;
  - another game shares it: the first one's records kept, its bit added to
    those it compiled too, both warm after;
  - a game merges in where they are: one only the other game had gets its
    bit there, one not there goes at the end;
  - a record a crash cut short is cut off, and made again;
  - where the file is not there, the files of before (glide64_<crcs>.keys,
    the records without the games) are read into it, each key once, the
    games from their names, those of other shaders left out, and removed;
  - a file of before beside the one file is left alone.

  glide64_gl_cache.py --core parallel_n64_libretro.so --host ./lrhost_gl [--roms krom-roms]
"""
import argparse, glob, os, struct, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parallel_rdp_regress import fetch  # noqa: E402

MAGIC, OLD_MAGIC = 0x53344c47, 0x47324c49
NAME = "glide64-opengl.cache"
FRAMES = 40


def run(a, rom, sysdir, strict, options=()):
    env = dict(os.environ, EGL_PLATFORM="surfaceless", LRHOST_SYSTEM_DIR=sysdir)
    if strict:
        env["PARALLEL_N64_COMBINER_STRICT"] = "1"
    p = subprocess.run([a.host, a.core, rom, str(FRAMES), "parallel-n64-gfxplugin=glide64",
                        "parallel-n64-glide64-API=opengl", "parallel-n64-rspplugin=hle",
                        "parallel-n64-cpucore=cached_interpreter"] + list(options),
                       env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    return p.returncode == 0 and b"Error " not in p.stdout


def crcs(rom):
    with open(rom, "rb") as f:
        head = f.read(0x18)
    if head[:1] == b"\x37":  # byte-swapped
        head = bytes(head[i ^ 1] for i in range(len(head)))
    return struct.unpack(">2I", head[0x10:0x18])


def bit(c):
    h = (c[0] * 0x9E3779B1 ^ c[1] * 0x85EBCA77) & 0xffffffff
    return 1 << ((h ^ h >> 16) >> 27)


def parse(path):
    """the version and the records (key, games, source) of a whole file, or None"""
    with open(path, "rb") as f:
        b = f.read()
    if len(b) < 8 or struct.unpack_from("<I", b)[0] != MAGIC:
        return None
    out, pos = [], 8
    while pos + 32 <= len(b):
        f = struct.unpack_from("<6i2I", b, pos)
        if pos + 32 + f[6] > len(b):
            return None
        out.append((f[:6], f[7], b[pos + 32:pos + 32 + f[6]]))
        pos += 32 + f[6]
    return (struct.unpack_from("<I", b, 4)[0], out) if pos == len(b) else None


def write(path, version, records, magic=MAGIC):
    with open(path, "wb") as f:
        f.write(struct.pack("<2I", magic, version))
        for key, games, src in records:
            f.write(struct.pack("<6i", *key) + struct.pack("<I", len(src)))
            if magic == MAGIC:
                f.write(struct.pack("<I", games))
            f.write(src)


def state(path):
    with open(path, "rb") as f:
        return f.read(), os.stat(path).st_mtime_ns


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--roms", default="krom-roms")
    a = ap.parse_args()
    a.core, a.host = os.path.abspath(a.core), os.path.abspath(a.host)
    rom1, rom2 = fetch(a.roms, "cube16"), fetch(a.roms, "shade32")
    c1, c2 = crcs(rom1), crcs(rom2)
    b1, b2 = bit(c1), bit(c2)
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)
            print("FAILED: " + what)

    if b1 == b2:
        print("FAILED: the two cartridges have one bit")
        return 1
    with tempfile.TemporaryDirectory() as sysdir:
        shaders = os.path.join(sysdir, "ParallelN64", "shaders")
        path = os.path.join(shaders, NAME)

        # the first session: the one file, this game's records
        check(run(a, rom1, sysdir, False), "a first session ran")
        check(sorted(os.listdir(shaders)) == [NAME], "the one file, nothing else: %s" % sorted(os.listdir(shaders)))
        got = parse(path)
        if got is None or not got[1]:
            print("FAILED: the file is not whole, or has no records")
            return 1
        version, one = got
        check(all(g == b1 for _, g, _ in one), "the first game's records have its bit only")

        # a second: warm, the file left as it is
        before = state(path)
        check(run(a, rom1, sysdir, True), "a second session built every combiner at load")
        check(state(path) == before, "a session that compiled nothing wrote the file")

        # another game shares it; both warm after. A third game's records
        # first in the file, kept as they are where they are
        b3 = next(1 << k for k in range(32) if not (1 << k) & (b1 | b2))
        third = [((0x7100 + i, 1, 2, 3, 4, 5), b3, b"third game %d" % i) for i in range(3)]
        write(path, version, third + one)
        check(run(a, rom2, sysdir, False), "another game ran")
        got = parse(path)
        both = got[1] if got else []
        check(both[:3] == third, "a third game's records kept as they are where they are")
        keys = [k for k, _, _ in both]
        check(len(keys) == len(set(keys)), "each key once")
        mine = {k: s for k, _, s in one}
        check(all(k in keys for k in mine), "the first game's records kept")
        check(all(g & b1 for k, g, s in both if k in mine) and all(s == mine[k] for k, g, s in both if k in mine),
              "the first game's records as they were, its bit on")
        check(any(g & b2 for _, g, _ in both) and all(g & ~(b1 | b2) == 0 for _, g, _ in both[3:]),
              "the other game's bit on what it compiled, no other bit")
        check(any(g == b1 | b2 for _, g, _ in both), "a combiner of both games once, with both bits")
        check(run(a, rom2, sysdir, True) and run(a, rom1, sysdir, True), "both games warm on the one file")
        both = parse(path)[1]

        # merged where they are: a record only the other game had gets this
        # one's bit there, one not there goes at the end, as made again
        firsts = [i for i, (k, g, s) in enumerate(both) if g & b1]
        if len(firsts) >= 2:
            only, gone = firsts[0], firsts[-1]
            crafted = [(k, (b2 if i == only else g), s) for i, (k, g, s) in enumerate(both) if i != gone]
            want = [(k, (g | b1 if i == only else g), s) for i, (k, g, s) in enumerate(crafted)]
            want.append((both[gone][0], b1, both[gone][2]))
            write(path, version, crafted)
            stray = os.path.join(shaders, "glide64_0000000000000000.keys")
            with open(stray, "wb") as f:
                f.write(b"left alone")
            check(run(a, rom1, sysdir, False), "a session on a file without one of its records ran")
            got = parse(path)
            check(got is not None and got[1] == want, "this game's records merged where they are, the new one at the end")
            with open(stray, "rb") as f:
                check(f.read() == b"left alone", "a file of before beside the one file left alone")
            os.remove(stray)
        else:
            check(False, "the first game compiled fewer than two combiners")
        merged = parse(path)[1]

        # a record cut short: cut off, made again
        with open(path, "r+b") as f:
            f.truncate(os.path.getsize(path) - 5)
        check(run(a, rom1, sysdir, False), "a session on a file cut short ran")
        got = parse(path)
        last = [i for i, (k, g, s) in enumerate(merged) if g & b1][-1] == len(merged) - 1
        check(got is not None and (got[1] == merged if last else got[1][:-1] == merged[:-1]),
              "the record cut short cut off and made again")

        # the files of before: one each game's, a key of the first in the
        # other's too, one of other shaders; the one file not there
        merged = parse(path)[1]
        g1 = [(k, 0, s) for k, g, s in merged if g & b1]
        g2 = [(k, 0, s) for k, g, s in merged if g & b2 and not g & b1] + g1[:1]
        os.remove(path)
        write(os.path.join(shaders, "glide64_%08x%08x.keys" % c1), version, g1, OLD_MAGIC)
        write(os.path.join(shaders, "glide64_%08x%08x.keys" % c2), version, g2, OLD_MAGIC)
        write(os.path.join(shaders, "glide64_1111111122222222.keys"), version + 1,
              [((1, 2, 3, 4, 5, 6), 0, b"void main() {}")], OLD_MAGIC)
        # names of no game: not read, removed all the same; another
        # prefix: not read, left alone
        for odd in ("glide64_0123456789abcdeg.keys", "glide64_0123456789abcdef0.keys", "glide64_0123456789ABCDEF.keys"):
            write(os.path.join(shaders, odd), version, [((9, 9, 9, 9, 9, 9), 0, b"odd")], OLD_MAGIC)
        other = os.path.join(shaders, "glide64x0123456789abcdef.keys")
        write(other, version, [((8, 8, 8, 8, 8, 8), 0, b"other")], OLD_MAGIC)
        # one cut short by a crash: its whole records read, the last not
        c4 = next((0x41000000 + i, 0x42000000) for i in range(64) if bit((0x41000000 + i, 0x42000000)) not in (b1, b2))
        cut = [((0x6100, i, 0, 0, 0, 0), 0, b"cut %d" % i) for i in range(3)]
        write(os.path.join(shaders, "glide64_%08x%08x.keys" % c4), version, cut, OLD_MAGIC)
        with open(os.path.join(shaders, "glide64_%08x%08x.keys" % c4), "r+b") as f:
            f.truncate(os.path.getsize(f.name) - 2)
        # a game of many: more than the room a session has
        c3 = next((0x31000000 + i, 0x32000000) for i in range(64) if bit((0x31000000 + i, 0x32000000)) not in (b1, b2))
        many = [((0x6000, i, 0, 0, 0, 0), 0, (b"many %d " % i).ljust(3000, b"x")) for i in range(1600)]
        write(os.path.join(shaders, "glide64_%08x%08x.keys" % c3), version, many, OLD_MAGIC)
        check(run(a, rom1, sysdir, True), "a session on the files of before built every combiner at load")
        check(sorted(os.listdir(shaders)) == sorted([NAME, os.path.basename(other)]),
              "the files of before gone, another prefix's left: %s" % sorted(os.listdir(shaders)))
        got = parse(path)
        want = {}
        for games, recs in ((b1, g1), (b2, g2), (bit(c3), many), (bit(c4), cut[:-1])):
            for k, _, s in recs:
                want[k] = (want.get(k, (0, s))[0] | games, s)
        check(got is not None and len(got[1]) == len(want) and
              all(want.get(k) == (g, s) for k, g, s in got[1]),
              "their records in it, each key once, the games from their names, other shaders' and other names' "
              "left out, past the room a session has")
        check(run(a, rom2, sysdir, True), "the other game warm on what was gathered")
        os.remove(other)

    # the N64's 3-point filter in the key: built at load with it
    with tempfile.TemporaryDirectory() as sysdir:
        three = ("parallel-n64-filtering=N64 3-point",)
        check(run(a, rom1, sysdir, False, three), "a session with the 3-point filter ran")
        got = parse(os.path.join(sysdir, "ParallelN64", "shaders", NAME))
        check(got is not None and any(k[1] or k[2] for k, _, _ in got[1]), "records of the 3-point filter")
        check(run(a, rom1, sysdir, True, three), "the 3-point filter's combiners built at load")

    print("glide64 gl cache: %s (%d records of %d games)" % ("ok" if not failures else "%d FAILED" % len(failures),
                                                             len(want), 4))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
