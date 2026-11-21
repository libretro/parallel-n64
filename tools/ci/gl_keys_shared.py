#!/usr/bin/env python3
"""glN64 and Rice: one journal of combiner keys every game shares.

shaders/<plugin>.keys: the journal's header, then each session's keys after
a line of its game (@ and the 16 hex digits of its CRCs). A game builds the
keys of its own lines at load.

  - a first session under PARALLEL_N64_COMBINER_STRICT aborts (a draw
    compiles), for contrast;
  - a first session writes the one journal, its keys after a line of its
    game (the game's own CRCs: not those of the plugin's first start,
    before the core has the ROM the right way round), nothing else;
  - a second builds every key at load (PARALLEL_N64_COMBINER_STRICT aborts
    if a draw has to compile) and leaves the journal as it is;
  - another game's session adds its lines; both warm after;
  - where the journal is not there, the files of before (<plugin>_<crcs>.keys)
    are read into it, each in lines of its game, and removed; one of
    another version removed and not read.

Rice reaches no combiner on these ROMs: its files of before are written in
here, which tests that they are gathered and built at load.

  gl_keys_shared.py --core parallel_n64_libretro.so --host ./lrhost_gl [--roms krom-roms]
"""
import argparse, os, random, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parallel_rdp_regress import fetch  # noqa: E402

HEADER = "gliden64-combiner-keys %08x\n"
VERSION, OLD_VERSION = 2, 1
FRAMES = 40


def run(a, rom, plugin, sysdir, strict):
    env = dict(os.environ, EGL_PLATFORM="surfaceless", LRHOST_SYSTEM_DIR=sysdir)
    if strict:
        env["PARALLEL_N64_COMBINER_STRICT"] = "1"
    p = subprocess.run([a.host, a.core, rom, str(FRAMES), "parallel-n64-gfxplugin=" + plugin,
                        "parallel-n64-rspplugin=hle", "parallel-n64-cpucore=cached_interpreter"],
                       env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    return p.returncode == 0 and b"Error " not in p.stdout


def game(rom):
    with open(rom, "rb") as f:
        head = f.read(0x18)
    if head[:1] == b"\x37":  # byte-swapped
        head = bytes(head[i ^ 1] for i in range(len(head)))
    return head[0x10:0x18].hex()


def sections(path):
    """the journal's games, in order, each with its keys"""
    with open(path) as f:
        lines = f.read().split("\n")
    if lines[0] != (HEADER % VERSION).strip():
        return None
    out = []
    for line in lines[1:]:
        if line.startswith("@"):
            out.append((line[1:], []))
        elif len(line) == 18 and out:
            out[-1][1].append(line)
    return out


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
    g1, g2 = game(rom1), game(rom2)
    failures = []

    def check(ok, what):
        if not ok:
            failures.append(what)
            print("FAILED: " + what)

    # glN64 meets combiners on these ROMs; with no journal a draw compiles,
    # which PARALLEL_N64_COMBINER_STRICT aborts on
    with tempfile.TemporaryDirectory() as sysdir:
        check(not run(a, rom1, "gln64", sysdir, True), "gln64: a first session under strict did not abort")
    with tempfile.TemporaryDirectory() as sysdir:
        shaders = os.path.join(sysdir, "ParallelN64", "shaders")
        path = os.path.join(shaders, "gln64.keys")
        check(run(a, rom1, "gln64", sysdir, False), "gln64: a first session ran")
        check(sorted(os.listdir(shaders)) == ["gln64.keys"], "gln64: the one journal, nothing else: %s"
              % sorted(os.listdir(shaders)))
        got = sections(path)
        check(got is not None and [g for g, _ in got] == [g1] and got[0][1],
              "gln64: the first game's keys after a line of its own CRCs: %s" % got)
        keys1 = got[0][1] if got else []
        before = state(path)
        check(run(a, rom1, "gln64", sysdir, True), "gln64: a second session built every key at load")
        check(state(path) == before, "gln64: a session that met nothing new wrote the journal")
        check(run(a, rom2, "gln64", sysdir, False), "gln64: another game ran")
        got = sections(path)
        check(got is not None and [g for g, _ in got] == [g1, g2] and got[0][1] == keys1 and got[1][1],
              "gln64: the other game's keys after a line of its own: %s" % got)
        check(run(a, rom2, "gln64", sysdir, True) and run(a, rom1, "gln64", sysdir, True),
              "gln64: both games warm on the one journal")
        both = sections(path)

        # its lines back in files of before, the journal gone
        os.remove(path)
        for g, keys in both:
            with open(os.path.join(shaders, "gln64_%s.keys" % g), "w") as f:
                f.write(HEADER % OLD_VERSION + "".join(k + "\n" for k in keys))
        with open(os.path.join(shaders, "gln64_1111111122222222.keys"), "w") as f:
            f.write(HEADER % 9 + "0123456789abcdef 0\n")
        check(run(a, rom1, "gln64", sysdir, True), "gln64: the files of before built at load")
        check(sorted(os.listdir(shaders)) == ["gln64.keys"], "gln64: the files of before gone: %s"
              % sorted(os.listdir(shaders)))
        got = sections(path)
        check(got is not None and sorted(got) == sorted(both), "gln64: their keys in it, each in lines of its game")
        check(run(a, rom2, "gln64", sysdir, True), "gln64: the other game warm on what was gathered")

    # Rice's files of before, written in here
    with tempfile.TemporaryDirectory() as sysdir:
        shaders = os.path.join(sysdir, "ParallelN64", "shaders")
        os.makedirs(shaders)
        seeded = {}
        for g in (g1, g2):
            rnd = random.Random(g)
            seeded[g] = ["%016x %x" % (rnd.getrandbits(64), flags & 3) for flags in range(6)]
            with open(os.path.join(shaders, "rice_%s.keys" % g), "w") as f:
                f.write(HEADER % OLD_VERSION + "".join(k + "\n" for k in seeded[g]))
        check(run(a, rom1, "rice", sysdir, True), "rice: the files of before built at load")
        path = os.path.join(shaders, "rice.keys")
        check(sorted(os.listdir(shaders)) == ["rice.keys"], "rice: the files of before gone: %s"
              % sorted(os.listdir(shaders)))
        got = sections(path) if os.path.exists(path) else None
        check(got is not None and sorted(got) == sorted((g, seeded[g]) for g in (g1, g2)),
              "rice: their keys in the one journal, each in lines of its game")
        check(run(a, rom2, "rice", sysdir, True), "rice: the other game's keys built at load")

    print("gl keys shared: %s" % ("ok" if not failures else "%d FAILED" % len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
