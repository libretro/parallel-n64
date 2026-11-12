#!/usr/bin/env python3
"""GLideN64 on Vulkan drawing for a frontend's temporal upscaler
(RETRO_ENVIRONMENT_GET_HW_RENDER_UPSCALER_INTERFACE), as parallel-RDP does.

tools/lrhost_vk offers one (LRHOST_UPSCALER) that asks for 640x480 frames
and dumps what each frame brings: its colour, motion vectors, depth and
translucency mask. Two cartridges, each an F3D display list the HLE RSP
hands GLideN64 every vertical blank, with frame buffer emulation on and
off:

still   a rectangle of one colour drawn as two triangles, its left and top
        edges half way through a pixel of the frame. Each frame is moved
        by a sub-pixel jitter, so the pixel on either edge is drawn in the
        frames whose jitter is to the left (or up) and not in the others:
        that is how the jitter reported is known to be the jitter drawn,
        and the right way round. The jitter must follow the Halton (2, 3)
        sequence.

moving  the same seen through a perspective projection, its right edge
        twice as far as its left, sliding to the right: the motion vectors
        must take each pixel it newly covers to where it was a frame ago
        (tools/ci/upscaler_check.py --moving).

Every frame from the first one checked must come with all three inputs, of
the frame's size, which must be the size asked for. GLIDEN64_VULKAN_STRICT
is set: a pipeline built while drawing, rather than at load, aborts.

The moving cartridge also runs under the Khronos validation layer, with
synchronization validation, where that is installed: the pass that draws
the inputs must bring no validation error and no hazard.

A request no whole factor of the N64's frame fits (900 to 1100 wide: 640
is too small, 1280 too large) must leave the frames as they are without
one, and without inputs.

  gliden64_upscaler.py --core parallel_n64_libretro.so --vk ./lrhost_vk
"""
import argparse, os, re, struct, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gliden64_hle_dl as hle  # noqa: E402
import upscaler_check  # noqa: E402

FRAMES = 30
FIRST, LAST = 12, 27
QUAD = (0xE0, 0x40, 0x20)
BACK = (0x10, 0x20, 0x60)
# the modelview's translation: the integer halves of x and y share a word
MODELVIEW_XY = hle.MTX + 64 + 24
MODELVIEW_XY_FRACTION = hle.MTX + 64 + 32 + 24
STEP = 8


def display_list():
    c = []
    cmd = lambda w0, w1=0: c.extend([w0 & 0xffffffff, w1 & 0xffffffff])
    cmd(0xFF000000 | (2 << 19) | (hle.WIDTH - 1), hle.FB)                    # SETCIMG RGBA16
    cmd(0xED000000, ((hle.WIDTH * 4) << 12) | (hle.HEIGHT * 4))             # SETSCISSOR
    cmd(0xE7000000)
    # one cycle, opaque; the colour PRIMITIVE
    cmd(0xEF000000 | (1 << 19) | (6 << 9), 0x0F0A4000)
    cmd(0xFCFFFFFF, 0xFFFDF6FB)
    cmd(0xFA000000, (BACK[0] << 24) | (BACK[1] << 16) | (BACK[2] << 8) | 0xFF)
    cmd(0xF6000000 | ((hle.WIDTH * 4) << 12) | (hle.HEIGHT * 4), 0)          # FILLRECT, the whole frame
    cmd(0xE7000000)
    cmd(0xFA000000, (QUAD[0] << 24) | (QUAD[1] << 16) | (QUAD[2] << 8) | 0xFF)
    cmd(0xB6000000, 0xFFFFFFFF)                                              # clear geometry mode
    cmd(0x03800010, hle.VP)                                                  # viewport
    cmd(0x01030040, hle.MTX)                                                 # projection, load
    cmd(0x01020040, hle.MTX + 64)                                            # modelview, load
    cmd(0xBB000000, 0xFFFFFFFF)                                              # texture off
    cmd(0x04300040, hle.VTX)                                                 # four vertices
    cmd(0xBF000000, (0 << 16) | (10 << 8) | 20)
    cmd(0xBF000000, (0 << 16) | (20 << 8) | 30)
    cmd(0xE9000000)                                                          # FULLSYNC
    cmd(0xB8000000)                                                          # ENDDL
    return struct.pack(">%dI" % len(c), *c)


def vertex(x, y, z):
    return struct.pack(">hhhHhhBBBB", x, y, z, 0, 0, 0, 255, 255, 255, 255)


def cartridge(path, moving):
    areas = {hle.DL: display_list()}
    if moving:
        # x over -z, a right angle wide: the left edge 200 away, the right 400
        near, far = 10.0, 1000.0
        projection = [[1, 0, 0, 0], [0, 4 / 3.0, 0, 0],
                      [0, 0, -(far + near) / (far - near), -1], [0, 0, -2 * far * near / (far - near), 0]]
        areas[hle.VTX] = (vertex(-100, -50, -200) + vertex(100, -50, -400) +
                          vertex(100, 50, -400) + vertex(-100, 50, -200))
        areas[hle.MTX] = hle.matrix(projection) + hle.matrix([[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0],
                                                              [0, 0, 0, 1]])
        hle.cartridge(path, False, "GLIDEN64 UPSCALER", areas=areas, step=(MODELVIEW_XY, STEP << 16))
    else:
        # 128 units half the frame either way (an exact scale, where a
        # unit a pixel is not, in a 16.16 matrix): a fifth of a unit to the
        # right and three fifths up put the left edge at 40.25 pixels and
        # the top at 69.75, half way through a pixel at twice the size
        areas[hle.VTX] = b"".join(vertex(x, y, 0) for (x, y) in ((-96, -50), (-16, -50), (-16, 53), (-96, 53)))
        areas[hle.MTX] = (hle.matrix([[1 / 128.0, 0, 0, 0], [0, 1 / 128.0, 0, 0], [0, 0, 0.5, 0], [0, 0, 0, 1]]) +
                          hle.matrix([[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0.2, 0.6, 0, 1]]))
        hle.cartridge(path, False, "GLIDEN64 UPSCALER", areas=areas)


def run(host, core, rom, tmp, request, fb_emulation, validate=False):
    dump = tempfile.mkdtemp(dir=tmp)
    log = os.path.join(dump, "upscaler.log")
    env = dict(os.environ, LRHOST_SYSTEM_DIR=os.path.join(tmp, "sys"), LRHOST_DUMPDIR=dump,
               LRHOST_DUMP_FROM=str(FIRST - 1), LRHOST_DUMP_TO=str(LAST), LRHOST_DUMP_EVERY="1",
               LRHOST_UPSCALER=request, LRHOST_UPSCALER_LOG=log, GLIDEN64_VULKAN_STRICT="1")
    if validate:
        env.update(VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation",
                   VK_LAYER_ENABLES="VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT",
                   VK_KHRONOS_VALIDATION_DEBUG_ACTION="VK_DBG_LAYER_ACTION_LOG_MSG",
                   VK_KHRONOS_VALIDATION_REPORT_FLAGS="error,warn")
    p = subprocess.run([host, core, rom, str(FRAMES)] + hle.OPTIONS +
                       ["parallel-n64-gliden64-API=vulkan", "parallel-n64-gfxplugin=gliden64",
                        "parallel-n64-gliden64-EnableFBEmulation=" + ("True" if fb_emulation else "False")],
                       env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    if p.returncode != 0:
        sys.stdout.write(p.stdout.decode("utf-8", "replace")[-2000:])
        raise RuntimeError("exit %d" % p.returncode)
    if validate:
        found = {}
        for m in re.findall(r"(VUID-[A-Za-z0-9_-]+|SYNC-HAZARD-[A-Z-]+)", p.stdout.decode("utf-8", "replace")):
            found[m] = found.get(m, 0) + 1
        return dump, log, found
    return dump, log


def have_validation():
    layers = [d for d in os.environ.get("VK_LAYER_PATH", "").split(os.pathsep) if d]
    layers += ["/usr/share/vulkan/explicit_layer.d", "/usr/local/share/vulkan/explicit_layer.d",
               "/etc/vulkan/explicit_layer.d"]
    return any(os.path.isdir(d) and any("khronos_validation" in f.lower() for f in os.listdir(d)) for d in layers)


def frame(dump, n):
    with open("%s/%04d.raw.size" % (dump, n)) as f:
        w, h = map(int, f.read().split())
    with open("%s/%04d.raw" % (dump, n), "rb") as f:
        return w, h, f.read()


def jitter_drawn(dump, log):
    """the pixel on the rectangle's left edge, and the one on its top edge,
    are the rectangle's in the frames jittered left, or up, and no others"""
    frames = upscaler_check.read_log(log)
    quad = bytes(QUAD)
    seen = set()
    for n in range(FIRST, LAST + 1):
        fr = frames.get(n)
        if not fr:
            return "frame %d: no inputs" % n
        w, h, px = frame(dump, n)
        if (w, h) != (640, 480):
            return "frame %d: %dx%d, asked for 640x480" % (n, w, h)
        jx, jy = fr["jitter"]
        at = lambda x, y: px[(y * w + x) * 4:(y * w + x) * 4 + 3]
        if at(180, 240) != quad or at(40, 240) == quad:
            return "frame %d: the rectangle is not where it is drawn" % n
        for axis, j, drawn in (("left", jx, at(80, 240) == quad), ("top", jy, at(180, 139) == quad)):
            if abs(j) < 0.02:
                continue  # on the pixel's centre: either
            if drawn != (j < 0):
                return ("frame %d: jitter %+.3f, the %s edge's pixel is %sdrawn"
                        % (n, j, axis, "" if drawn else "not "))
            seen.add((axis, drawn))
    if len(seen) != 4:
        return "the jitter never went both ways on both axes"
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True)
    ap.add_argument("--vk", required=True)
    a = ap.parse_args()
    core, host = os.path.abspath(a.core), os.path.abspath(a.vk)
    failures = 0

    def report(what, error):
        print("%-44s %s" % (what, "ok" if not error else "FAIL (%s)" % error))
        return 0 if not error else 1

    def checker(dump, log, *more):
        p = subprocess.run([sys.executable, upscaler_check.__file__, dump, log, str(FIRST), str(LAST),
                            "--repeats"] + list(more), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return None if p.returncode == 0 else p.stdout.decode("utf-8", "replace").strip().splitlines()[-1]

    with tempfile.TemporaryDirectory() as tmp:
        still, moving = os.path.join(tmp, "still.z64"), os.path.join(tmp, "moving.z64")
        cartridge(still, False)
        cartridge(moving, True)
        for fb in (False, True):
            mode = "frame buffer emulation %s" % ("on" if fb else "off")
            dump, log = run(host, core, still, tmp, "640,480", fb)
            failures += report("%s: inputs, jitter sequence" % mode, checker(dump, log))
            failures += report("%s: jitter drawn" % mode, jitter_drawn(dump, log))
            dump, log = run(host, core, moving, tmp, "640,480", fb)
            failures += report("%s: motion vectors" % mode, checker(dump, log, "--moving"))

        if have_validation():
            for fb in (False, True):
                dump, log, found = run(host, core, moving, tmp, "640,480", fb, validate=True)
                frames = upscaler_check.read_log(log)
                error = "".join(" %s:%d" % kv for kv in sorted(found.items())).strip() or None
                if not error and not frames.get(LAST):
                    error = "no inputs under the layer"
                failures += report("frame buffer emulation %s: validation" % ("on" if fb else "off"), error)
        else:
            print("validation skipped: the Khronos validation layer is not installed")

        # nothing fits: as without an upscaler
        dump, log = run(host, core, still, tmp, "1000,700,900,650,1100,750", True)
        frames = upscaler_check.read_log(log)
        error = None
        if not frames or any(fr is not None for fr in frames.values()):
            error = "inputs handed over"
        elif frame(dump, LAST)[:2] != (640, 480):
            error = "frames of %dx%d" % frame(dump, LAST)[:2]
        failures += report("a size no factor fits: not upscaled", error)
    print("%d failed" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
