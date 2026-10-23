#!/usr/bin/env python3
"""parallel-RDP shader variants for devices without 8-bit SSBO storage.

The vendored slangmosh.hpp holds upstream's shaders, which need
storageBuffer8BitAccess. This tool appends a NO_STORAGE_8BIT build of the
shaders that use it, leaving every original SPIR-V word and reflection byte
where it was, so devices that have the feature run exactly the old binaries.

  check:       rdp_no_storage_8bit_bank.py --check
               Every shader that needs 8-bit storage has a fallback, no
               fallback declares an 8-bit capability, each passes spirv-val
               (when installed), each uses only bindings its reflection record
               declares, and unmerging gives back a bank of 8-bit binaries
               only. Used by CI.

  regenerate:  rdp_no_storage_8bit_bank.py
               After a shader change. Needs glslangValidator and spirv-opt
               (Debian/Ubuntu: glslang-tools, spirv-tools).

The fallback entries reuse the reflection records of their SMALL_TYPES = 0
counterparts (same bindings, minus any the fallback no longer uses), so no
Granite tool is needed and the record format stays the one this runtime
reads.
"""
import argparse, json, os, re, shutil, struct, subprocess, sys, tempfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SHADERS = os.path.join(ROOT, "mupen64plus-video-paraLLEl", "parallel-rdp", "parallel-rdp", "shaders")
HEADER = os.path.join(SHADERS, "slangmosh.hpp")
DEFINE = "NO_STORAGE_8BIT"
# OpCapability values that need 8-bit storage or arithmetic
CAPS_8BIT = {39: "Int8", 4448: "StorageBuffer8BitAccess", 4449: "UniformAndStorageBuffer8BitAccess",
             4450: "StoragePushConstant8"}
# ResourceLayout version 3 (vulkan/shader.hpp): 8-byte magic, then four
# DescriptorSetLayouts of 80 bytes whose first nine words are binding masks.
REFL_SETS, REFL_SET_SIZE, REFL_MASKS = 4, 80, 9


def array(src, name):
    a = src.index(name + "[] =")
    b = src.index("\n};", a)
    return [int(x, 16) for x in re.findall(r"0x([0-9a-f]+)", src[src.index("{", a):b])]


def parse(src):
    tmpl = src.index("template <typename Program, typename Shader>")
    ctor_open = src.index("{\n", src.index("Shaders(Device &device")) + 2
    ctor_close = src.index("\n\t}\n};", ctor_open) + 1
    return dict(spirv=array(src, "spirv_bank"), refl=array(src, "reflection_bank"),
                head=src[:src.index("static const uint32_t spirv_bank[]")],
                members=src[tmpl:ctor_open], body=src[ctor_open:ctor_close], tail=src[ctor_close:])


def units(body):
    """(shader, text) statements of the ctor body; an if/else is one."""
    lines = body.splitlines(keepends=True)
    out, i = [], 0
    while i < len(lines):
        if lines[i].strip().startswith("if ("):
            j = lines.index("\t\t{\n", i)
            k = lines.index("\t\t}\n", j)
            if k + 1 < len(lines) and lines[k + 1] == "\t\telse\n":
                k = lines.index("\t\t}\n", k + 2)
            text = "".join(lines[i:k + 1])
            i = k + 1
        else:
            assert "layout.unserialize" in lines[i], lines[i]
            text = lines[i] + lines[i + 1]
            i += 2
        out.append((re.search(r"this->(\w+)", text).group(1), text))
    return out


def group(us):
    groups = []
    for name, text in us:
        if groups and groups[-1][0] == name:
            groups[-1][1].append(text)
        else:
            groups.append((name, [text]))
    return groups


def indent(text):
    return "".join("\t" + l if l.strip() else l for l in text.splitlines(keepends=True))


def dedent(text):
    return "".join(l[1:] if l.startswith("\t") else l for l in text.splitlines(keepends=True))


def unmerge(bank):
    """The 8-bit-only bank, and the fallback ctor text per shader."""
    body, fallback, first = "", {}, None
    for name, text in units(bank["body"]):
        if text.startswith('\t\tif (resolver("%s", "%s") == 0)' % (name, DEFINE)):
            lines = text.splitlines(keepends=True)
            e = lines.index("\t\telse\n")
            body += dedent("".join(lines[2:e - 1]))
            fallback[name] = dedent("".join(lines[e + 2:-1]))
            for off in re.findall(r"spirv_bank \+ (\d+)", fallback[name]):
                first = int(off) if first is None else min(first, int(off))
        else:
            body += text
    spirv = bank["spirv"][:first] if first is not None else bank["spirv"]
    return dict(bank, spirv=spirv, body=body), fallback


def with_fallback(base, spirv, fallback):
    """base with the fallback ctor text wrapped in around its shaders."""
    body = ""
    for name, texts in group(units(base["body"])):
        if name in fallback:
            body += '\t\tif (resolver("%s", "%s") == 0)\n\t\t{\n' % (name, DEFINE)
            body += indent("".join(texts)) + "\t\t}\n\t\telse\n\t\t{\n" + indent(fallback[name]) + "\t\t}\n"
        else:
            body += "".join(texts)
    return dict(base, spirv=spirv, body=body)


def emit(bank):
    s = bank["head"] + "static const uint32_t spirv_bank[] =\n{\n"
    w = bank["spirv"]
    for i in range(0, len(w), 8):
        s += "\t" + " ".join("0x%08xu," % x for x in w[i:i + 8]) + "\n"
    s += "};\n\nstatic const uint8_t reflection_bank[] =\n{\n"
    r = bank["refl"]
    for i in range(0, len(r), 32):
        s += "\t" + " ".join("0x%02x," % x for x in r[i:i + 32]) + "\n"
    return s + "};\n\n" + bank["members"] + bank["body"] + bank["tail"]


def entries(bank, body):
    """(shader, conditions, reflection offset, SPIR-V words) per request."""
    for name, text in units(body):
        for m in re.finditer(r"((?:if \([^{]*\{\s*)?)layout\.unserialize\(reflection_bank \+ (\d+), \d+\);\s*"
                             r"this->(\w+)(?:\[\d+\])? = device\.request_\w+\(spirv_bank \+ (\d+), (\d+)", text):
            cond = dict((k, int(v)) for k, v in re.findall(r'"(\w+)"\) == (\d)', m.group(1)) if k != DEFINE)
            off, size = int(m.group(4)), int(m.group(5))
            yield m.group(3), cond, int(m.group(2)), bank["spirv"][off:off + size // 4]


def capabilities(words):
    caps, i = set(), 5
    while i < len(words):
        op, n = words[i] & 0xffff, words[i] >> 16
        if n == 0:
            break
        if op == 17:
            caps.add(words[i + 1])
        i += n
    return caps


def bindings(words):
    """(set, binding) pairs the module decorates."""
    sets, binds, i = {}, {}, 5
    while i < len(words):
        op, n = words[i] & 0xffff, words[i] >> 16
        if op == 71 and n >= 4 and words[i + 2] in (33, 34):  # OpDecorate Binding / DescriptorSet
            (binds if words[i + 2] == 33 else sets)[words[i + 1]] = words[i + 3]
        i += max(n, 1)
    return {(sets[v], binds[v]) for v in binds if v in sets}


def declared(refl, offset):
    out = set()
    for s in range(REFL_SETS):
        masks = struct.unpack_from("<%dI" % REFL_MASKS, bytes(refl[offset + 8 + s * REFL_SET_SIZE:
                                                                 offset + 8 + (s + 1) * REFL_SET_SIZE]))
        for b in range(32):
            if any(m >> b & 1 for m in masks):
                out.add((s, b))
    return out


def spirv_val(words):
    with tempfile.NamedTemporaryFile(suffix=".spv", delete=False) as f:
        f.write(struct.pack("<%dI" % len(words), *words))
    try:
        p = subprocess.run(["spirv-val", "--target-env", "vulkan1.1", f.name], capture_output=True, text=True)
        return (p.stdout + p.stderr).strip().splitlines()[0] if p.returncode else None
    finally:
        os.unlink(f.name)


def needs_8bit(base):
    return [n for n, _, _, w in entries(base, base["body"]) if capabilities(w) & set(CAPS_8BIT)]


def check(src):
    bank = parse(src)
    base, fallback = unmerge(bank)
    errors, count = [], 0
    for name in sorted(set(needs_8bit(base)) - set(fallback)):
        errors.append("%s needs 8-bit storage and has no %s variant" % (name, DEFINE))
    validate = shutil.which("spirv-val")
    for name, text in fallback.items():
        for prog, cond, refl, words in entries(bank, text):
            count += 1
            what = "%s variant of %s %s" % (DEFINE, prog, cond or "")
            bad = sorted(CAPS_8BIT[c] for c in capabilities(words) & set(CAPS_8BIT))
            if bad:
                errors.append("%s declares %s" % (what, ", ".join(bad)))
            extra = sorted(bindings(words) - declared(bank["refl"], refl))
            if extra:
                errors.append("%s uses bindings %s its reflection record lacks" % (what, extra))
            complaint = spirv_val(words) if validate else None
            if complaint:
                errors.append("%s fails spirv-val: %s" % (what, complaint))
    if emit(with_fallback(base, bank["spirv"], fallback)) != src:
        errors.append("unmerging and merging again does not give back slangmosh.hpp")
    if not validate:
        print("spirv-val not found, the fallback binaries were not validated")
    return errors, len(set(needs_8bit(base))), count


def compile_variant(tmp, path, defines):
    src = os.path.join(tmp, path)
    spv, opt = os.path.join(tmp, "out.spv"), os.path.join(tmp, "opt.spv")
    subprocess.run(["glslangValidator", "-V", "--target-env", "vulkan1.1", "-I" + tmp,
                    "-D%s=1" % DEFINE] + ["-D%s=%d" % d for d in defines] + ["-o", spv, src],
                   check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["spirv-opt", "-O", "--strip-debug", "--target-env=vulkan1.1", spv, "-o", opt], check=True)
    with open(opt, "rb") as f:
        data = f.read()
    return list(struct.unpack("<%dI" % (len(data) // 4), data))


def regenerate():
    base, _ = unmerge(parse(open(HEADER).read()))
    refl_of = {}
    for name, cond, refl, _ in entries(base, base["body"]):
        if cond.pop("SMALL_TYPES", 0) == 0:
            refl_of[(name, tuple(sorted(cond.items())))] = refl
    needs = needs_8bit(base)
    spec = json.load(open(os.path.join(SHADERS, "slangmosh.json")))
    spirv, fallback = list(base["spirv"]), {}
    tmp = tempfile.mkdtemp()
    try:
        for f in os.listdir(SHADERS):
            if f.endswith((".comp", ".h", ".frag", ".vert")):
                text = open(os.path.join(SHADERS, f)).read()
                text = re.sub(r"^(#version[^\n]*\n)", r"\1#extension GL_GOOGLE_include_directive : require\n",
                              text, count=1)
                open(os.path.join(tmp, f), "w").write(text)
        for s in spec["shaders"]:
            if s["name"] not in needs:
                continue
            axes = [(v["define"], v["count"]) for v in s.get("variants", []) if v["define"] != "SMALL_TYPES"]
            combos = [[]]
            for define, count in axes:  # the first define varies fastest, as slangmosh orders them
                combos = [c + [(define, v)] for v in range(count) for c in combos]
            text = ""
            for combo in combos:
                words = compile_variant(tmp, s["path"], combo)
                refl = refl_of[(s["name"], tuple(sorted(combo)))]
                req = ("layout.unserialize(reflection_bank + %d, %d);\n"
                       "this->%s = device.request_program(spirv_bank + %d, %d, &layout);\n"
                       % (refl, 348, s["name"], len(spirv), 4 * len(words)))
                spirv += words
                if combo:
                    conds = " &&\n\t\t    ".join('resolver("%s", "%s") == %d' % (s["name"], d, v) for d, v in combo)
                    text += "\t\tif (%s)\n\t\t{\n%s\t\t}\n" % (conds, indent(indent(indent(req))))
                else:
                    text += indent(indent(req))
            fallback[s["name"]] = text
    finally:
        shutil.rmtree(tmp)
    open(HEADER, "w").write(emit(with_fallback(base, spirv, fallback)))
    print("wrote", HEADER)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    if not args.check:
        regenerate()
        return 0
    errors, needs, variants = check(open(HEADER).read())
    print("%d shaders need 8-bit storage, %d %s binaries" % (needs, variants, DEFINE))
    if errors:
        print("\n".join(errors))
        return 1
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
