#!/usr/bin/env python3
"""parallel-RDP under Vulkan synchronization validation.

  parallel_rdp_syncval.py --core parallel_n64_libretro.so --host ./lrhost_vk [--roms dir]

Runs a few krom ROMs for a few frames through tools/lrhost_vk with the
Khronos validation layer and synchronization validation on, natively, upscaled
2x, and upscaled 2x and downscaled back (the scale pass with its downscale
folded in), and fails on any validation error, and on any write-after-read
or write-after-write hazard: work that overwrites memory another submission
may still be using, such as an initialisation submitted where it lands
after the first frame that reads it.

Read-after-write hazards are counted but not failed on: the layer in
Ubuntu 24.04 (1.3.275) reports one every frame at the VI's VRAM handoff,
which is ordered by a timeline semaphore that version does not track, and
which goes away when the same work is ordered with binary semaphores.

Needs the layer (Debian/Ubuntu: vulkan-validationlayers).
"""

import argparse, os, re, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parallel_rdp_regress import OPTIONS, fetch

ROMS = ("cube16", "rottri32xlu")
CONFIGS = (("native", ()), ("2x", ("parallel-n64-upscaling=2x",)),
           ("2x-downscaled", ("parallel-n64-upscaling=2x", "parallel-n64-parallel-rdp-downscaling=1/2")))
FRAMES = 8

HAZARD = re.compile(r"SYNC-HAZARD-([A-Z-]+)")
VUID = re.compile(r"(VUID-[A-Za-z0-9_-]+)")


def layer_installed():
    dirs = [d for d in os.environ.get("VK_LAYER_PATH", "").split(os.pathsep) if d]
    dirs += ["/usr/share/vulkan/explicit_layer.d", "/usr/local/share/vulkan/explicit_layer.d",
             "/etc/vulkan/explicit_layer.d"]
    for d in dirs:
        if os.path.isdir(d) and any("khronos_validation" in f.lower() for f in os.listdir(d)):
            return True
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--host", required=True)
    ap.add_argument("--roms", default="krom-roms")
    args = ap.parse_args()

    env = dict(os.environ,
               VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation",
               VK_LAYER_ENABLES="VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT",
               VK_KHRONOS_VALIDATION_DEBUG_ACTION="VK_DBG_LAYER_ACTION_LOG_MSG",
               VK_KHRONOS_VALIDATION_REPORT_FLAGS="error,warn")

    if not layer_installed():
        print("the Khronos validation layer is not installed")
        return 2

    failures = 0
    for name in ROMS:
        rom = fetch(args.roms, name)
        for config, options in CONFIGS:
            p = subprocess.run([args.host, args.core, rom, str(FRAMES)] + OPTIONS + list(options),
                               env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
            out = p.stdout.decode("utf-8", "replace")
            hazards = {}
            for h in HAZARD.findall(out):
                hazards[h] = hazards.get(h, 0) + 1
            vuids = {}
            for v in VUID.findall(out):
                vuids[v] = vuids.get(v, 0) + 1

            bad = {h: n for h, n in hazards.items() if h.startswith("WRITE-AFTER")}
            ok = p.returncode == 0 and not bad and not vuids
            summary = " ".join("%s:%d" % kv for kv in sorted(hazards.items())) or "no hazards"
            print("%-12s %-7s %s %s%s" % (name, config, "ok  " if ok else "FAIL", summary,
                                          "".join(" %s:%d" % kv for kv in sorted(vuids.items()))))
            if not ok:
                failures += 1
                if p.returncode != 0:
                    print("  exit status %d" % p.returncode)
                for line in out.splitlines():
                    if any("SYNC-HAZARD-" + h in line for h in bad) or VUID.search(line):
                        print("  " + line[:600])
                        break

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
