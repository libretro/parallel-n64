#!/usr/bin/env python3
"""parallel-RDP on a device whose storage buffer bindings hold 128 MiB.

  parallel_rdp_binding_fit.py --core parallel_n64_libretro.so --host ./lrhost_vk [--roms dir]

Lavapipe and many phone GPUs bind at most 128 MiB of a storage buffer
(PARALLEL_RDP_MAX_STORAGE_BUFFER_RANGE gives any device that limit here).
Upscaling buffers that do not fit lower the factor, but where only the
super-sampled read-back's write mask does not fit, the read-back goes
rather than half the resolution: 4x with it renders at 4x without it
(it used to fall back to 2x). The frame each run presents must be the
size of the factor it should render at, and the on-screen message must
say what was dropped.
"""
import argparse, os, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parallel_rdp_regress import OPTIONS, fetch  # noqa: E402

SSAA = "parallel-n64-parallel-rdp-super-sampled-read-back=enabled"
# (upscaling, read-back asked for, factor rendered at, message expected)
CASES = (("2x", True, 2, None),
         ("4x", False, 4, None),
         ("4x", True, 4, "Super-sampled read-back is off"),
         ("8x", False, 4, "rendering at 4x."),
         ("8x", True, 4, "rendering at 4x without super-sampled read-back"))
FRAMES = 4


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--roms", default="krom-roms")
    args = ap.parse_args()
    rom = fetch(args.roms, "cube16")

    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        native = None
        for scale, ssaa, want, message in (("1x", False, 1, None),) + CASES:
            for f in os.listdir(tmp):
                os.remove(os.path.join(tmp, f))
            env = dict(os.environ, LRHOST_DUMPDIR=tmp, LRHOST_DUMP_FROM=str(FRAMES - 1),
                       LRHOST_DUMP_TO=str(FRAMES - 1), LRHOST_VERBOSE="1",
                       PARALLEL_RDP_MAX_STORAGE_BUFFER_RANGE=str(128 << 20))
            p = subprocess.run([args.host, args.core, rom, str(FRAMES)] + OPTIONS +
                               ["parallel-n64-upscaling=" + scale] + ([SSAA] if ssaa else []),
                               env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900)
            out = p.stdout.decode("utf-8", "replace")
            size = os.path.join(tmp, "%04d.raw.size" % (FRAMES - 1))
            problems = []
            if p.returncode != 0 or not os.path.exists(size):
                problems.append("no frame (exit %d)" % p.returncode)
            else:
                w, h = [int(v) for v in open(size).read().split()[:2]]
                if native is None:
                    native = (w, h)
                elif (w, h) != (native[0] * want, native[1] * want):
                    problems.append("%dx%d, not %ux" % (w, h, want))
            if message and message not in out:
                problems.append("no message saying so")
            if not message and ("storage buffer" in out and "GPU allows" in out):
                problems.append("a fallback message")
            label = "%s%s" % (scale, " + read-back" if ssaa else "")
            print("%-16s %s" % (label, "ok (%ux)" % want if not problems else "FAILED: " + ", ".join(problems)))
            if problems:
                failures += 1
                sys.stdout.write(out[-1500:])
    if failures:
        print("%d failures" % failures)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
