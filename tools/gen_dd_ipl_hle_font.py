#!/usr/bin/env python3
# Generates mupen64plus-core/src/device/dd/dd_ipl_hle_font_data.c, the
# glyphs of the built-in 64DD IPL's ROM font:
#
#   tools/gen_dd_ipl_hle_font.py <shinonome dir> <output .c>
#
# <shinonome dir> holds the public-domain Shinonome fonts in Plan 9 format,
# as plan9port ships them in font/shinonome (k16b.font, 8x16ab.font and
# their subfont files). The JIS glyphs go into the slots the drive's ROM
# font uses: JIS X 0208 order over lead bytes 0x81-0x84 (where that font
# has no 0x81da and keeps blank slots at 0x82f2, 0x83d7 and 0x84bf), blank
# vendor rows 0x85-0x87, then the level-1 kanji from slot 0x30a.
import os, struct, sys
if len(sys.argv) != 3:
    sys.exit('usage: gen_dd_ipl_hle_font.py <shinonome dir> <output .c>')
SRC, OUT = sys.argv[1], sys.argv[2]

# ---- Plan 9 font and subfont reader
def _fields(b, pos, n):
    out = []
    for i in range(n):
        out.append(b[pos+12*i:pos+12*i+12].decode().strip())
    return out, pos + 12*n
def read_image(b, pos):
    """returns (chan, rect, rows(list of bytes per row), bpl, newpos)"""
    if b[pos:pos+11] == b'compressed\n':
        pos += 11
        (chan, minx, miny, maxx, maxy), pos = _fields(b, pos, 5)
        minx, miny, maxx, maxy = map(int, (minx, miny, maxx, maxy))
        depth = {'k1': 1, 'm1': 1}.get(chan, None)
        bpl = ((maxx * depth + 7) // 8) - ((minx * depth) // 8)
        data = bytearray()
        y = miny
        while y < maxy:
            (ymax, nb), pos = _fields(b, pos, 2)
            ymax, nb = int(ymax), int(nb)
            blk = b[pos:pos+nb]; pos += nb
            i = 0
            want = (ymax - y) * bpl
            start = len(data)
            while len(data) - start < want:
                c = blk[i]; i += 1
                if c & 0x80:
                    n = (c & 0x7f) + 1
                    data += blk[i:i+n]; i += n
                else:
                    n = ((c >> 2) & 0x1f) + 3
                    off = (((c & 3) << 8) | blk[i]) + 1; i += 1
                    for k in range(n):
                        data.append(data[len(data) - off])
            y = ymax
        rows = [bytes(data[r*bpl:(r+1)*bpl]) for r in range(maxy - miny)]
        return chan, (minx, miny, maxx, maxy), rows, bpl, pos
    (chan, minx, miny, maxx, maxy), pos = _fields(b, pos, 5)
    minx, miny, maxx, maxy = map(int, (minx, miny, maxx, maxy))
    bpl = (maxx + 7) // 8 - minx // 8
    rows = [b[pos+r*bpl:pos+(r+1)*bpl] for r in range(maxy - miny)]
    return chan, (minx, miny, maxx, maxy), rows, bpl, pos + bpl*(maxy-miny)
def read_subfont(path):
    b = open(path, 'rb').read()
    chan, rect, rows, bpl, pos = read_image(b, 0)
    (n, height, ascent), pos = _fields(b, pos, 3)
    n, height, ascent = int(n), int(height), int(ascent)
    info = []
    for i in range(n + 1):
        x, top, bottom, left, width = struct.unpack('<HBBbB', b[pos:pos+6]); pos += 6
        info.append((x, top, bottom, left, width))
    return dict(chan=chan, rect=rect, rows=rows, n=n, height=height, ascent=ascent, info=info)
def pixel(sf, x, y):
    minx = sf['rect'][0]
    xx = x - (minx & ~7)
    v = (sf['rows'][y][xx >> 3] >> (7 - (xx & 7))) & 1
    return v
def load_font(fontfile):
    d = os.path.dirname(fontfile)
    lines = open(fontfile).read().split('\n')
    height, ascent = map(int, lines[0].split())
    glyphs = {}
    for l in lines[1:]:
        p = l.split()
        if len(p) < 3: continue
        lo, hi, name = int(p[0], 0), int(p[1], 0), p[2]
        off = int(p[3], 0) if len(p) > 3 else 0
        sf = read_subfont(os.path.join(d, name))
        for cp in range(lo, hi + 1):
            i = cp - lo + off
            if i >= sf['n']: continue
            x, top, bottom, left, width = sf['info'][i]
            x1 = sf['info'][i+1][0]
            g = [[0]*(x1-x) for _ in range(height)]
            for yy in range(top, bottom):
                for xx in range(x, x1):
                    g[yy][xx-x] = pixel(sf, xx, yy)
            glyphs[cp] = dict(bits=g, left=left, width=width, w=x1-x, top=top, bottom=bottom)
    return height, ascent, glyphs

# ---- slot layout of the drive's JIS font
def trail(p): return 0x40 + p + (1 if p >= 0x3f else 0)
def defined(hi, lo):
    try:
        bytes([hi, lo]).decode('shift_jis'); return True
    except Exception:
        return False
EXTRA = {(0x82, 0xf2), (0x83, 0xd7), (0x84, 0xbf)}   # slots with no JIS character
ABSENT = {(0x81, 0xda)}
def nonkanji_slots():
    """index -> (hi, lo) for lead bytes 0x81-0x84; 0x85-0x87 follow as blanks"""
    out = []
    for hi in range(0x81, 0x85):
        for p in range(188):
            lo = trail(p)
            if ((defined(hi, lo) and (hi, lo) not in ABSENT) or (hi, lo) in EXTRA):
                out.append((hi, lo))
    return out
KANJI_BASE = 0x30a
def kanji_index(hi, lo):
    p = lo - 0x40 - (1 if lo >= 0x80 else 0)
    return (hi - 0x88) * 0xbc + p + KANJI_BASE

# ---- output
_, _, k16 = load_font(os.path.join(SRC, 'k16b.font'))
nslots = kanji_index(0x98, 0x72) + 1
glyph = [None] * nslots
miss = []
def put(idx, hi, lo):
    ch = bytes([hi, lo]).decode('shift_jis')
    cp = ord(ch)
    g = k16.get(cp)
    if g is None:
        miss.append('%02x%02x U+%04X' % (hi, lo, cp)); return
    rows = []
    for y in range(16):
        bits = 0
        for x in range(16):
            v = g['bits'][y][x] if x < g['w'] else 0
            bits |= v << (15 - x)
        rows.append(bits)
    glyph[idx] = rows
for idx, (hi, lo) in enumerate(nonkanji_slots()):
    if (hi, lo) in EXTRA: continue
    put(idx, hi, lo)
for hi in range(0x88, 0x99):
    for p in range(188):
        lo = trail(p)
        if (hi, lo) > (0x98, 0x72): break
        if defined(hi, lo):
            put(kanji_index(hi, lo), hi, lo)
sys.stderr.write('slots %d, glyphs %d, missing %d %s\n' % (nslots, sum(g is not None for g in glyph), len(miss), miss[:10]))
out = []
out.append('/* Generated by tools/gen_dd_ipl_hle_font.py: do not edit. */\n')
out.append('/* Kanji font of the built-in 64DD IPL: 16x16 glyphs, one bit per pixel,\n'
           ' * sixteen big-endian 16-bit rows each, in the slot order of the drive\'s\n'
           ' * ROM font (JIS X 0208 order; slots without a character are blank).\n'
           ' * The glyphs are the Shinonome 16-dot bold gothic font, which its authors\n'
           ' * released into the public domain. */\n\n')
out.append('#include "dd_ipl_hle_font.h"\n\n')
out.append('const unsigned int dd_ipl_hle_kanji_count = %du;\n\n' % nslots)
out.append('const unsigned char dd_ipl_hle_kanji[%d][32] = {\n' % nslots)
for g in glyph:
    if g is None:
        out.append('    { 0 },\n'); continue
    b = []
    for r in g:
        b += [r >> 8, r & 0xff]
    out.append('    { ' + ','.join('0x%02x' % v for v in b) + ' },\n')
out.append('};\n')
open(OUT, 'w').write(''.join(out))

# Latin: printable ASCII and Latin-1 0xa0-0xff, 8x16, from Shinonome 8x16
_, _, l16 = load_font(os.path.join(SRC, '8x16ab.font'))
codes = list(range(0x20, 0x80)) + list(range(0xa0, 0x100))
lat = []
for c in codes:
    g = l16.get(c)
    rows = []
    for y in range(16):
        v = 0
        if g is not None:
            for x in range(min(8, g['w'])):
                v |= g['bits'][y][x] << (7 - x)
        rows.append(v)
    lat.append(rows)
o = []
o.append('\n/* Latin glyphs for the drive\'s proportional fonts: printable ASCII then\n'
         ' * Latin-1 0xa0-0xff, 8x16, one bit per pixel, leftmost pixel in bit 7,\n'
         ' * from the Shinonome 8x16 bold font (public domain). */\n')
o.append('const unsigned char dd_ipl_hle_latin[192][16] = {\n')
for r in lat:
    o.append('    { ' + ','.join('0x%02x' % v for v in r) + ' },\n')
o.append('};\n')
open(OUT, 'a').write(''.join(o))
