#!/usr/bin/env python3
"""GLideN64's Vulkan backend draws what its OpenGL backend draws.

The same content, frame by frame and pixel by pixel: the generated cartridge
of combine modes, and krom's ROMs that draw, on GL and on Vulkan, with every
combiner through the generic shader (generic) and with the combiners' own
pipelines (own). Any pixel off by more than one step fails.

  gliden64_vulkan.py --core parallel_n64_libretro.so --gl ./lrhost_gl --vk ./lrhost_vk [--roms krom-roms]
"""
import argparse, os, re, struct, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gliden64_ubershader as cart

OPTIONS = ["parallel-n64-gfxplugin=gliden64", "parallel-n64-rspplugin=hle",
           "parallel-n64-cpucore=cached_interpreter"]
TOLERANCE = 1


def run(host, core, rom, tmp, name, frames, api, env_extra, options=()):
    # one directory, so that the pipeline cache warms up across the runs
    sysdir = os.path.join(tmp, "sys")
    dump = os.path.join(tmp, name)
    os.makedirs(sysdir, exist_ok=True)
    os.makedirs(dump, exist_ok=True)
    env = dict(os.environ, EGL_PLATFORM="surfaceless", LRHOST_SYSTEM_DIR=sysdir, LRHOST_DUMPDIR=dump, GLIDEN64_VULKAN_STRICT="1",
               LRHOST_DUMP_FROM="1", LRHOST_DUMP_TO=str(frames))
    env.update(env_extra)
    p = subprocess.run([host, core, rom, str(frames)] + OPTIONS + list(options) + ["parallel-n64-gliden64-API=" + api],
                       env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
    if p.returncode != 0:
        sys.stdout.write(p.stdout.decode("utf-8", "replace")[-2000:])
        raise RuntimeError("exit %d" % p.returncode)
    out = {}
    for f in sorted(os.listdir(dump)):
        if f.endswith(".raw"):
            with open(os.path.join(dump, f), "rb") as h:
                out[f] = h.read()
    return out


def culling_cartridge(path, resize=False, shadow=False, gamma=False):
    """An F3D task, run by the HLE RSP: triangles of both windings, under each
    cull mode. The other test content goes straight to the RDP and never culls.
    With resize the game's image changes width every few frames. With shadow
    the rows are depth tested at three depths and a texrect then reads the
    depth image, which a game named BEETLE draws as a shadow map through the
    z and palette lookup textures. With gamma the VI asks for gamma correction,
    which GLideN64 does as a post-processing pass."""
    t0, t1, t2, t3, t4, t5, t6, t7 = 8, 9, 10, 11, 12, 13, 14, 15
    mips = cart.mips
    FB, ZB, DATA = 0x100000, 0x180000, 0x200000
    W, H = cart.WIDTH, cart.HEIGHT
    lui = lambda rt, imm: mips(0x0f, 0, rt, imm)
    ori = lambda rt, rs, imm: mips(0x0d, rs, rt, imm)
    sw = lambda rt, off, base: mips(0x2b, base, rt, off)
    lw = lambda rt, off, base: mips(0x23, base, rt, off)
    bne = lambda rs, rt, off: mips(0x05, rs, rt, off)
    beq = lambda rs, rt, off: mips(0x04, rs, rt, off)
    li = lambda rt, v: [lui(rt, (v >> 16) & 0xffff), ori(rt, rt, v & 0xffff)]

    UCODE_TEXT, PROJ, VIEWPORT, MODEL, VERTS, PAL, DLIST = 0x0, 0x1000, 0x1040, 0x1060, 0x1100, 0x1800, 0x2000
    text = b"RSP SW Version: 2.0D, 04-01-96\n".ljust(0x800, b"\0")
    proj = struct.pack(">32H", *([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 100] + [0] * 16))  # units of 1%
    model = struct.pack(">32H", *([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1] + [0] * 16))
    viewport = struct.pack(">8h", W * 2, H * 2, 511, 0, W * 2, H * 2, 511, 0)

    def vtx(x, y, r, g, b, z=0):
        return struct.pack(">3hH2h4B", x, y, z, 0, 0, 0, r, g, b, 255)

    # counter-clockwise on the left half, clockwise on the right, one row per cull mode
    verts = b""
    tris = []
    rows = [(0, (255, 0, 0), (0, 255, 0)), (0x2000, (0, 0, 255), (255, 255, 0)), (0x1000, (255, 0, 255), (0, 255, 255))]
    for r, (cull, left, right) in enumerate(rows):
        y0 = 70 - 60 * r
        for c, colour in enumerate((left, right)):
            x0 = -80 + 80 * c
            a, b, d = (x0 - 30, y0 - 20), (x0 + 30, y0 - 20), (x0, y0 + 20)
            if gamma:  # mid tones, which gamma changes
                colour = tuple(v * 120 // 255 for v in colour)
            for p in ((a, b, d) if c == 0 else (a, d, b)):
                verts += vtx(p[0], p[1], *colour, z=(40 * r - 40) if shadow else 0)
        tris.append(cull)

    cmds = []
    if shadow:  # clear the depth image the way games do
        cmds += [0xFE000000, ZB,
                 0xFF100000 | (W - 1), ZB,
                 0xED000000, ((W * 4) << 12) | (H * 4),
                 0xEF300000, 0,
                 0xF7000000, 0xFFFCFFFC,
                 0xF6000000 | ((W - 1) << 14) | ((H - 1) << 2), 0,
                 0xE7000000, 0]
    cmds += [0xFF100000 | (W - 1), FB,
            0xED000000, ((W * 4) << 12) | (H * 4),
            0xEF300000, 0,
            0xF7000000, 0x00010001,
            0xF6000000 | ((W - 1) << 14) | ((H - 1) << 2), 0,
            0xE7000000, 0,
            0xEF000000 | (2 << 12), 0x00552078 if shadow else 0,
            0xFCFFFFFF, 0xFFFE793C,
            0x03800010, DATA + VIEWPORT,
            0x01030040, DATA + PROJ,
            0x01020040, DATA + MODEL]
    for r, cull in enumerate(tris):
        cmds += [0xB6000000, 0xFFFFFFFF,
                 0xB7000000, 0x0204 | cull | (1 if shadow else 0),
                 0x04000000 | (5 << 20) | 96, DATA + VERTS + r * 96]
        cmds += [0xBF000000, (0 << 16) | (10 << 8) | 20]
        cmds += [0xBF000000, (30 << 16) | (40 << 8) | 50]
    if shadow:
        cmds += [0xE7000000, 0,
                 0xFD100000, DATA + PAL,              # the TLUT, to TMEM's upper half
                 0xF5000100, 0x07000000,
                 0xF0000000, 0x073FC000,
                 0xE7000000, 0,
                 0xFD100000 | (W - 1), ZB,            # the depth image as the texture
                 0xF8000000, 0x2040C0FF,
                 0xE4000000 | ((280 * 4) << 12) | (200 * 4), (40 * 4) << 12 | (40 * 4),
                 0xB4000000, 0,
                 0xB3000000, (1 << 26) | (1 << 10)]
    cmds += [0xE7000000, 0, 0xE9000000, 0, 0xB8000000, 0]

    data = bytearray(DLIST + 8 * len(cmds))
    data[UCODE_TEXT:UCODE_TEXT + len(text)] = text
    data[PROJ:PROJ + len(proj)] = proj
    data[MODEL:MODEL + len(model)] = model
    data[VIEWPORT:VIEWPORT + len(viewport)] = viewport
    data[VERTS:VERTS + len(verts)] = verts
    data[PAL:PAL + 512] = struct.pack(">256H", *(((i * 7) & 0xff) << 8 | i for i in range(256)))
    data[DLIST:] = struct.pack(">%dI" % len(cmds), *cmds)

    prog = li(t0, 0xA4600000) + li(t1, DATA) + [sw(t1, 0, t0)]
    prog += li(t1, 0x10000000 + cart.PAYLOAD) + [sw(t1, 4, t0)]
    prog += li(t1, len(data) - 1) + [sw(t1, 0xC, t0)]
    prog += [lui(t0, 0xA440)]
    for k, v in enumerate([0x300A if gamma else 0x3002, FB, W, 2, 0, 0x03E52239, 0x20D, 0xC15, 0x0C150C15,
                           0x006C02EC, 0x002501FF, 0x000E0204, 0x200, 0x400]):
        prog += li(t2, v) + [sw(t2, 4 * k, t0)]
    prog += li(t0, 0xA4000FC0)
    task = {0: 1, 0x10: DATA + 0x8000, 0x14: 0x1000, 0x18: DATA + UCODE_TEXT, 0x1C: 0x800,
            0x24: 0x400, 0x30: DATA + DLIST, 0x34: 8 * len(cmds)}
    for off, v in task.items():
        prog += li(t1, v) + [sw(t1, off, t0)]
    prog += li(t0, 0xA4040010) + li(t2, 0xA4400010)
    start = len(prog)
    prog += [lw(t1, 0, t2), ori(t3, 0, 0x200), bne(t1, t3, -3), 0]
    if resize:
        prog += [mips(0x09, t4, t4, 1), mips(0x0c, t4, t1, 8)]  # eight frames of each width
        prog += li(t5, 0xFF100000 | (W - 1)) + li(t6, W) + li(t7, FB)
        prog += [beq(t1, 0, 7), 0] + li(t5, 0xFF100000 | 255) + li(t6, 256) + li(t7, FB + 0x80000)
        prog += li(t1, 0xA0000000 | (DATA + DLIST)) + [sw(t5, 0, t1), sw(t7, 4, t1)]
        prog += li(t1, 0xA4400000) + [sw(t7, 4, t1), sw(t6, 8, t1)]
    prog += [ori(t1, 0, 0x125), sw(t1, 0, t0)]  # go, and ask for no interrupt
    prog += [lw(t1, 0, t2), ori(t3, 0, 0x200), beq(t1, t3, -3), 0]
    prog += [beq(0, 0, start - len(prog) - 1), 0]

    image = bytearray(max(0x100000, cart.PAYLOAD + len(data)))
    image[0:4] = (0x80371240).to_bytes(4, "big")
    image[8:12] = (0x80000400).to_bytes(4, "big")
    image[0x20:0x34] = b"BEETLE SHADOW MAP   " if shadow else b"GLIDEN64 CULLING    "
    assert 0x40 + 4 * len(prog) <= 0x1000
    for k, word in enumerate(prog):
        image[0x40 + 4 * k:0x40 + 4 * k + 4] = (word & 0xffffffff).to_bytes(4, "big")
    image[cart.PAYLOAD:cart.PAYLOAD + len(data)] = data
    with open(path, "wb") as f:
        f.write(image)


def validate(a, name, rom, frames, options=()):
    """Vulkan under the Khronos validation layer with synchronization validation:
    no validation error and no read or write hazard."""
    layers = [d for d in os.environ.get("VK_LAYER_PATH", "").split(os.pathsep) if d]
    layers += ["/usr/share/vulkan/explicit_layer.d", "/usr/local/share/vulkan/explicit_layer.d", "/etc/vulkan/explicit_layer.d"]
    if not any(os.path.isdir(d) and any("khronos_validation" in f.lower() for f in os.listdir(d)) for d in layers):
        print("%-10s validation skipped: the Khronos validation layer is not installed" % name)
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        env = dict(os.environ, EGL_PLATFORM="surfaceless", LRHOST_SYSTEM_DIR=tmp,
                   VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation",
                   VK_LAYER_ENABLES="VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT",
                   VK_KHRONOS_VALIDATION_DEBUG_ACTION="VK_DBG_LAYER_ACTION_LOG_MSG",
                   VK_KHRONOS_VALIDATION_REPORT_FLAGS="error,warn")
        p = subprocess.run([a.vk, a.core, rom, str(frames)] + OPTIONS + list(options) + ["parallel-n64-gliden64-API=Vulkan"],
                           env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
    out = p.stdout.decode("utf-8", "replace")
    found = {}
    for m in re.findall(r"(VUID-[A-Za-z0-9_-]+|SYNC-HAZARD-[A-Z-]+)", out):
        found[m] = found.get(m, 0) + 1
    bad = p.returncode != 0 or found
    print("%-10s validation %s%s" % (name, "FAILED" if bad else "ok", "".join(" %s:%d" % kv for kv in sorted(found.items()))))
    return 1 if bad else 0


def worst(gl, vk):
    """Largest channel difference: GL's frames are BGRA, Vulkan's RGBA."""
    if len(gl) != len(vk):
        return 256
    m = 0
    for i in range(0, len(gl), 4):
        for j, k in ((0, 2), (1, 1), (2, 0)):
            d = abs(gl[i + j] - vk[i + k])
            if d > m:
                m = d
    return m


def compare(a, name, rom, frames, options=(), modes=(("generic", {"GLIDEN64_VULKAN": "generic"}), ("own", {}))):
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        gl = run(a.gl, a.core, rom, tmp, "gl", frames, "opengl", {}, options)
        if not gl:
            print("FAILED: %s presented nothing" % name)
            return 1
        for label, env in modes:
            vk = run(a.vk, a.core, rom, tmp, label, frames, "vulkan", env, options)
            bad = 0
            top = 0
            # The first presented frame is a dupe (nothing latched yet): GL's
            # host then reads its framebuffer, Vulkan's has no image to read.
            common = sorted(set(gl) & set(vk))[1:]
            if len(common) != len(gl) - 1:
                bad = abs(len(gl) - 1 - len(common))
            for f in common:
                w = worst(gl[f], vk[f])
                top = max(top, w)
                if w > TOLERANCE:
                    bad += 1
            print("%-10s %-8s %s (%d frames, worst channel %d)" % (name, label, "ok" if not bad else "DIFFERENT", len(gl), top))
            failures += 1 if bad else 0
    return failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--gl", required=True)
    ap.add_argument("--vk", required=True)
    ap.add_argument("--roms", default="krom-roms")
    ap.add_argument("--quick", action="store_true", help="the cartridge and one ROM, for sanitizer builds")
    a = ap.parse_args()
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        rom = os.path.join(tmp, "combiners0.z64")
        cart.cartridge(rom, 0)
        failures += compare(a, "cart0", rom, 8)
        rom = os.path.join(tmp, "culling.z64")
        culling_cartridge(rom)
        failures += compare(a, "culling", rom, 8)
        failures += compare(a, "culling fb", rom, 8, ["parallel-n64-gliden64-EnableFBEmulation=True"])
        # the overscan target follows the game's image: resizing replaces its texture
        culling_cartridge(rom, resize=True)
        failures += compare(a, "resize", rom, 40, ["parallel-n64-gliden64-EnableFBEmulation=True", "parallel-n64-gliden64-EnableOverscan=Enabled",
                                                  "parallel-n64-gliden64-EnableNativeResFactor=2"])
        # FXAA then gamma correction: each pass draws into a buffer the other reads
        culling_cartridge(rom, gamma=True)
        pp = ["parallel-n64-gliden64-EnableFBEmulation=True", "parallel-n64-gliden64-FXAA=1"]
        failures += compare(a, "fxaa gamma", rom, 8, pp)
        failures += validate(a, "fxaa gamma", rom, 8, pp)
        # the depth image read back through the z and palette lookup textures
        culling_cartridge(rom, shadow=True)
        failures += compare(a, "shadow", rom, 8, ["parallel-n64-gliden64-EnableFBEmulation=True"])
        failures += validate(a, "shadow", rom, 8, ["parallel-n64-gliden64-EnableFBEmulation=True"])
    for name in (cart.KROM[:1] if a.quick else cart.KROM):
        failures += compare(a, name, cart.fetch(a.roms, name), 20 if a.quick else 60)
        failures += compare(a, name + " fb", cart.fetch(a.roms, name), 20,
                            ["parallel-n64-gliden64-EnableFBEmulation=True"])
    # GPUs without dynamic blending build a pipeline for each state instead
    if not a.quick:
        failures += compare(a, "static", cart.fetch(a.roms, "cube32"), 20,
                            modes=(("no-eds3", {"GLIDEN64_VULKAN_NO_EDS3": "1"}),))
    # what shapes the shaders and the targets
    for opt in () if a.quick else ("MultiSampling=4", "EnableN64DepthCompare=True", "BilinearMode=standard",
                "EnableCopyColorToRDRAM=Sync", "EnableCopyColorToRDRAM=Async"):
        failures += compare(a, opt.split("=")[0][:10], cart.fetch(a.roms, "cube32"), 8,
                            ["parallel-n64-gliden64-" + opt], (("own", {}),))
    print("%d failed" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
