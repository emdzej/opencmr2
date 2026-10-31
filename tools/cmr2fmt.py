#!/usr/bin/env python3
"""Colin McRae Rally 2.0 data formats: reference decoders (stdlib only) and exports into out/.

  cmr2fmt.py png <file|file:member> [out.png]   TGA or DDS (DXT1/DXT5) to PNG
  cmr2fmt.py wav <file|file:member> [out.wav]   PCM WAV header, or an .adp (MS ADPCM) decoded to PCM
  cmr2fmt.py obj <file|file:member> [out.obj] [texture archive]
                                                PP_F model to OBJ (+ MTL and PNGs; textures from the
                                                model's archive, or e.g. Tracks/Aus/aushit.bfl)
  cmr2fmt.py info <file|file:member>            what it is
  cmr2fmt.py --summary <cd dir> [images|models|sounds]   hashes, as tests/fmt_test.c

Layouts: docs/formats/media.md, docs/formats/c3d.md. Archives are read with bfl.py.
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bfl  # noqa: E402


# ---- loading ------------------------------------------------------------------------------------

def load_payload(path):
    data = open(path, 'rb').read()
    if data[:2] == b'\x1f\x8b':
        import gzip
        if gzip.decompress(data)[:4] != b'CMPR':
            return data          # File_Load returns a gzip file without CMPR as it is
    return bfl.unwrap(data)[0]


def load(spec):
    """'file' or 'file:member' -> bytes (payload or member)."""
    if ':' in spec[2:]:
        i = spec.rindex(':')
        path, member = spec[:i], spec[i + 1:]
        d = load_payload(path)
        for n, o, s in bfl.directory(d) or []:
            if n.lower() == member.lower():
                return d[o:o + s]
        raise SystemExit('%s: no member %s' % (path, member))
    return load_payload(spec)


# ---- images -------------------------------------------------------------------------------------

def tga_decode(f):
    """As Tga_Header 0x4a6670 accepts: type 2, no colour map, origin 0, bottom-up, 24 or 32 (8 alpha)
    bits. Returns (w, h, rgba bytes, rows top first) or None."""
    if len(f) < 18 or f[1] != 0 or f[2] != 2 or f[17] & 0x70:
        return None
    xo, yo, w, h = struct.unpack_from('<4H', f, 8)
    if xo or yo:
        return None
    if f[16] == 24:
        bpp = 3
    elif f[16] == 32 and f[17] & 15 == 8:
        bpp = 4
    else:
        return None
    start = 18 + f[0]
    if not w or not h or len(f) - start < w * h * bpp:
        return None
    out = bytearray(w * h * 4)
    for y in range(h):
        s = start + (h - 1 - y) * w * bpp
        row = f[s:s + w * bpp]
        d = y * w * 4
        out[d + 0:d + w * 4:4] = row[2::bpp]
        out[d + 1:d + w * 4:4] = row[1::bpp]
        out[d + 2:d + w * 4:4] = row[0::bpp]
        out[d + 3:d + w * 4:4] = row[3::bpp] if bpp == 4 else b'\xff' * w
    return w, h, bytes(out)


def _colours(b, o, dxt1):
    c0, c1, idx = struct.unpack_from('<HHI', b, o)
    pal = []
    for e in (c0, c1):
        r, g, bl = e >> 11, e >> 5 & 63, e & 31
        pal.append([r << 3 | r >> 2, g << 2 | g >> 4, bl << 3 | bl >> 2, 255])
    if not dxt1 or c0 > c1:
        pal.append([(2 * pal[0][k] + pal[1][k]) // 3 for k in range(3)] + [255])
        pal.append([(pal[0][k] + 2 * pal[1][k]) // 3 for k in range(3)] + [255])
    else:
        pal.append([(pal[0][k] + pal[1][k]) // 2 for k in range(3)] + [255])
        pal.append([0, 0, 0, 0])
    return [bytes(p) for p in pal], idx


def dds_decode(f):
    """DXT1/DXT5 top level. Returns (w, h, rgba) or None."""
    if len(f) < 0x80 or f[:4] != b'DDS ':
        return None
    h, w = struct.unpack_from('<II', f, 12)
    fourcc = f[84:88]
    if fourcc not in (b'DXT1', b'DXT5') or not w or not h or w > 4096 or h > 4096:
        return None
    dxt1 = fourcc == b'DXT1'
    bs = 8 if dxt1 else 16
    bw, bh = (w + 3) // 4, (h + 3) // 4
    if len(f) - 0x80 < bw * bh * bs:
        return None
    out = bytearray(w * h * 4)
    o = 0x80
    for by in range(bh):
        for bx in range(bw):
            if not dxt1:
                a0, a1 = f[o], f[o + 1]
                v = [a0, a1]
                if a0 > a1:
                    v += [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
                else:
                    v += [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
                abits = int.from_bytes(f[o + 2:o + 8], 'little')
                pal, idx = _colours(f, o + 8, False)
            else:
                pal, idx = _colours(f, o, True)
            for i in range(16):
                x, y = bx * 4 + (i & 3), by * 4 + (i >> 2)
                if x < w and y < h:
                    d = (y * w + x) * 4
                    out[d:d + 4] = pal[idx >> (2 * i) & 3]
                    if not dxt1:
                        out[d + 3] = v[abits >> (3 * i) & 7]
            o += bs
    return w, h, bytes(out)


def image_decode(f):
    return dds_decode(f) if f[:4] == b'DDS ' else tga_decode(f)


def png(w, h, rgba):
    def chunk(t, b):
        return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))
    raw = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


# ---- sound --------------------------------------------------------------------------------------

def snd_header(f):
    """Snd_Load 0x4a1f50: fixed offsets of the 44-byte header. (channels, rate, bits, data)."""
    if len(f) < 0x2c or f[8:12] != b'WAVE':
        return None
    ch, = struct.unpack_from('<H', f, 0x16)
    rate, = struct.unpack_from('<I', f, 0x18)
    bits, = struct.unpack_from('<H', f, 0x22)
    n, = struct.unpack_from('<I', f, 0x28)
    return ch, rate, bits, f[0x2c:0x2c + n]


def adp_header(f):
    """RIFF chunks: fmt (tag 2), fact, data. Returns dict or None."""
    if f[:4] != b'RIFF' or f[8:12] != b'WAVE':
        return None
    p, a = 12, None
    frames = 0
    while p + 8 <= len(f):
        cid, n = f[p:p + 4], struct.unpack_from('<I', f, p + 4)[0]
        c = p + 8
        if cid == b'fmt ':
            tag, ch, rate, _, ba, _, _, spb, nc = struct.unpack_from('<HHIIHHHHH', f, c)
            if tag != 2:
                return None
            coef = [struct.unpack_from('<hh', f, c + 22 + 4 * i) for i in range(nc)]
            a = dict(channels=ch, rate=rate, block_align=ba, spb=spb, coef=coef)
        elif cid == b'fact':
            frames, = struct.unpack_from('<I', f, c)
        elif cid == b'data':
            if a is None:
                return None
            a.update(data=f[c:c + n], frames=frames)
            return a
        p += 8 + n + (n & 1)
    return None


ADAPT = (230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230)


def adpcm_block(a, b):
    """One MS ADPCM block -> list of interleaved samples (as src/sound.c)."""
    ch, coef, spb = a['channels'], a['coef'], a['spb']
    if len(b) < 7 * ch:
        return []
    c1, c2, delta, s1, s2 = [], [], [], [], []
    for c in range(ch):
        pi = b[c] if b[c] < len(coef) else 0
        c1.append(coef[pi][0])
        c2.append(coef[pi][1])
        delta.append(struct.unpack_from('<h', b, ch + 2 * c)[0])
        s1.append(struct.unpack_from('<h', b, 3 * ch + 2 * c)[0])
        s2.append(struct.unpack_from('<h', b, 5 * ch + 2 * c)[0])
    out = s2[:] + s1[:]
    frames, k = 2, 0
    for byte in b[7 * ch:]:
        if frames >= spb:
            break
        for code in (byte >> 4, byte & 15):
            if frames >= spb:
                break
            c = k % ch
            t = s1[c] * c1[c] + s2[c] * c2[c]
            pred = (abs(t) // 256) * (1 if t >= 0 else -1) + (code - 16 if code >= 8 else code) * delta[c]
            pred = max(-32768, min(32767, pred))
            s2[c], s1[c] = s1[c], pred
            out.append(pred)
            delta[c] = max(16, ADAPT[code] * delta[c] // 256)
            k += 1
            if k % ch == 0:
                frames += 1
    return out


def adpcm_decode(a):
    ba, d = a['block_align'], a['data']
    out = []
    for o in range(0, len(d), ba):
        out += adpcm_block(a, d[o:o + ba])
    return struct.pack('<%dh' % len(out), *out)


def wav_file(ch, rate, pcm):
    return (b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt ' +
            struct.pack('<IHHIIHH', 16, 1, ch, rate, rate * ch * 2, ch * 2, 16) +
            b'data' + struct.pack('<I', len(pcm)) + pcm)


# ---- PP_F models --------------------------------------------------------------------------------

class C3d:
    """A PP_F image with its array bases (docs/formats/c3d.md). ref(value, base) resolves a relocated
    field as C3d_Reloc 0x4b98f0 would (-1 -> None)."""

    def __init__(self, d):
        if d[:4] != b'PP_F':
            raise ValueError('not PP_F')
        self.d = d
        (self.kind, self.slots, self.paths, self.n10, self.n14, self.n18, self.n1a, self.n1c, self.n1e,
         self.n20, self.n24, self.n26, self.n28) = struct.unpack_from('<IIIII4HIHHH', d, 4)
        o = 0x30
        self.objects = o; o += self.n18 * 0x18c
        self.meshes = o; o += (self.n1a + self.n1c) * 0x120
        self.mesh2x = o; o += self.n1c * 0xa0
        self.sections = o; o += self.n1e * 0x88
        self.r1c = o; o += self.n28 * 0x1c
        self.polys = o; o += self.n20 * 0x4c
        self.verts = o; o += self.n14 * 0x30
        self.vx = o; o += self.n14 * 4
        self.r14 = o; o += self.n10 * 0x14
        self.r5c = o; o += self.n26 * 0x5c
        if o != self.slots or self.paths + self.n24 * 260 > len(d):
            raise ValueError('PP_F arrays end at %#x, slots at %#x' % (o, self.slots))

    def i32(self, o):
        return struct.unpack_from('<i', self.d, o)[0]

    def ref(self, o, base, end, size):
        """The record a relocated field points to, as an index, or None; checked against the array."""
        v = self.i32(o)
        if v == -1:
            return None
        if v < 0 or base + v + size > end or v % size:
            raise ValueError('field %#x = %#x outside its array' % (o, v))
        return v // size

    def mesh(self, i):
        b = self.meshes + i * 0x120
        name = self.d[b:b + 12].split(b'\0')[0].decode('latin1')
        nv, = struct.unpack_from('<i', self.d, b + 0x10)
        npoly, = struct.unpack_from('<i', self.d, b + 0x28)
        v0 = self.ref(b + 0x0c, self.verts, self.vx, 0x30)
        p0 = self.ref(b + 0x24, self.polys, self.verts, 0x4c)
        return dict(name=name, v0=v0, nv=nv, p0=p0, npoly=npoly, sub=self.d[b + 0x113])

    def vertex(self, i):
        return struct.unpack_from('<3f3fII4f', self.d, self.verts + i * 0x30)

    def poly(self, i):
        b = self.polys + i * 0x4c
        return dict(tex=self.i32(b + 4), tex1=self.i32(b + 8), idx=struct.unpack_from('<3H', self.d, b + 0x40))

    def obj(self, i):
        b = self.objects + i * 0x18c
        end = self.meshes
        return dict(next=self.ref(b, self.objects, end, 0x18c), child=self.ref(b + 4, self.objects, end, 0x18c),
                    parent=self.ref(b + 8, self.objects, end, 0x18c),
                    mesh=self.ref(b + 0x0c, self.meshes, self.mesh2x, 0x120),
                    m=[x / 65536 for x in struct.unpack_from('<16i', self.d, b + 0x58)])

    def textures(self):
        out = []
        for i in range(self.n24):
            p = self.d[self.paths + i * 260:self.paths + (i + 1) * 260].split(b'\0')[0].decode('latin1')
            out.append(p)
        return out


def xform(m, v):
    x, y, z = v
    return (x * m[0] + y * m[4] + z * m[8] + m[12], x * m[1] + y * m[5] + z * m[9] + m[13],
            x * m[2] + y * m[6] + z * m[10] + m[14])


def obj_export(spec, out, texarch=None):
    """OBJ + MTL. Objects' meshes are placed by their first matrix (object to parent, row vectors),
    chained up the parents; meshes no object uses are written as they are (stage meshes)."""
    c = C3d(load(spec))
    os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
    base = os.path.splitext(out)[0]
    texs = c.textures()
    used = {}
    for i in range(c.n18):
        o = c.obj(i)
        if o['mesh'] is not None:
            m, p = o['m'], o['parent']
            chain = [m]
            while p is not None:
                po = c.obj(p)
                chain.append(po['m'])
                p = po['parent']
            used.setdefault(o['mesh'], chain)
    lines = ['mtllib %s.mtl' % os.path.basename(base)]
    vbase = 1
    for mi in range(c.n1a + c.n1c):
        me = c.mesh(mi)
        if me['v0'] is None or me['p0'] is None:
            continue
        lines.append('o %s_%d' % (me['name'] or 'mesh', mi))
        for k in range(me['nv']):
            v = c.vertex(me['v0'] + k)
            p = v[0:3]
            for m in used.get(mi, []):
                p = xform(m, p)
            lines.append('v %.6f %.6f %.6f' % p)
            lines.append('vt %.6f %.6f' % (v[8], 1 - v[9]))
        cur = None
        for k in range(me['npoly']):
            pl = c.poly(me['p0'] + k)
            if pl['tex'] != cur:
                cur = pl['tex']
                lines.append('usemtl t%d' % cur)
            a, b, cc = (vbase + x for x in pl['idx'])
            lines.append('f %d/%d %d/%d %d/%d' % (a, a, b, b, cc, cc))
        vbase += me['nv']
    open(out, 'w').write('\n'.join(lines) + '\n')
    # materials: the texture's file name; the image itself if found next to the model (car .bfl, or
    # the stage archive the model came from)
    arch = texarch
    if arch:
        pass
    elif ':' in spec[2:]:
        arch = spec[:spec.rindex(':')]
    elif spec.lower().endswith('.c3d') and os.path.exists(spec[:-4] + '.bfl'):
        arch = spec[:-4] + '.bfl'
    mtl = []
    for i, p in enumerate(texs + ['']):
        mtl.append('newmtl t%d' % (i if i < len(texs) else -1))
        if i < len(texs) and arch:
            name = p.replace('\\', '/').split('/')[-1].rsplit('.', 1)[0]
            for ext in ('.dds', '.tga'):
                try:
                    img = image_decode(load('%s:%s%s' % (arch, name, ext)))
                except SystemExit:
                    continue
                if img:
                    fn = '%s_t%d.png' % (os.path.basename(base), i)
                    open(os.path.join(os.path.dirname(out) or '.', fn), 'wb').write(png(*img))
                    mtl.append('map_Kd %s' % fn)
                break
    open(base + '.mtl', 'w').write('\n'.join(mtl) + '\n')
    print('%s: %d objects, %d meshes, %d polygons, %d vertices, %d textures' %
          (out, c.n18, c.n1a + c.n1c, c.n20, c.n14, c.n24))


# ---- summary ------------------------------------------------------------------------------------

def data_paths(root):
    paths = []
    for d in bfl.DATA_DIRS:
        for dp, _, fs in os.walk(os.path.join(root, d)):
            for f in fs:
                paths.append(os.path.relpath(os.path.join(dp, f), root).replace(os.sep, '/'))
    paths.sort(key=lambda p: p.lower())
    return paths


def items(root, p):
    """(name, bytes) for a file's members, or the file itself when it has no directory."""
    d = load_payload(os.path.join(root, p))
    ents = bfl.directory(d)
    if ents is None:
        return [(p, d)]
    return [('%s:%s' % (p, n), d[o:o + s]) for n, o, s in ents]


def walk(root):
    for p in data_paths(root):
        yield from items(root, p)


def file_lines(args):
    """The summary lines of one file, per kind."""
    root, p, kinds = args
    out = {k: [] for k in kinds}
    for name, d in items(root, p):
        low = name.lower()
        if 'images' in kinds and (low.endswith('.dds') or low.endswith('.tga')):
            img = image_decode(d)
            out['images'].append('%s %d %d %08x\n' % ((name,) + ((img[0], img[1], zlib.crc32(img[2])) if img else (0, 0, 0))))
        elif 'models' in kinds and d[:4] == b'PP_F':
            out['models'].append(model_line(name, d))
        elif 'sounds' in kinds and low.endswith('.wav'):
            h = snd_header(d)
            if h:
                out['sounds'].append('%s %d %d %d %d %08x\n' % (name, h[0], h[1], h[2], len(h[3]), zlib.crc32(h[3])))
        elif 'sounds' in kinds and low.endswith('.adp'):
            a = adp_header(d)
            pcm = adpcm_decode(a) if a else b''
            out['sounds'].append('%s %d %d %d %d %08x\n' % (name, a['channels'], a['rate'], 16, len(pcm), zlib.crc32(pcm)))
    return out


def summary(root, kinds):
    """Lines per decoded item, CRC-32 over all lines per kind, as tests/fmt_test.c:
    images: 'name w h crc(rgba)' for every .dds / .tga; models: 'name kind objects meshes polygons
    vertices textures crc(resolved triangle positions)' for every PP_F; sounds: 'name ch rate bits bytes
    crc(pcm)' for every WAV with a header and every .adp (decoded). Files are decoded in parallel."""
    from concurrent.futures import ProcessPoolExecutor
    tot = {k: [0, 0] for k in kinds}
    with ProcessPoolExecutor() as ex:
        for out in ex.map(file_lines, [(root, p, kinds) for p in data_paths(root)]):
            for k in kinds:
                for s in out[k]:
                    tot[k][0] = zlib.crc32(s.encode('latin1'), tot[k][0])
                    tot[k][1] += 1
    for k in kinds:
        print('%s: %d lines, summary %08x' % (k, tot[k][1], tot[k][0]))
    return 0


def model_line(name, d):
    try:
        c = C3d(d)
        crc = 0
        for mi in range(c.n1a + c.n1c):
            me = c.mesh(mi)
            if me['v0'] is None or me['p0'] is None:
                continue
            for k in range(me['npoly']):
                for x in c.poly(me['p0'] + k)['idx']:
                    crc = zlib.crc32(c.d[c.verts + (me['v0'] + x) * 0x30:][:12], crc)
        for i in range(c.n18):
            c.obj(i)   # checks the object references
        return '%s %d %d %d %d %d %d %08x\n' % (name, c.kind, c.n18, c.n1a + c.n1c, c.n20, c.n14, c.n24, crc)
    except (ValueError, struct.error, IndexError):
        return '%s bad\n' % name


def main():
    a = sys.argv[1:]
    if len(a) >= 2 and a[0] == '--summary':
        return summary(a[1], a[2:] or ['images', 'models', 'sounds'])
    if len(a) < 2:
        print(__doc__)
        return 2
    cmd, spec = a[0], a[1]
    stem = os.path.splitext(os.path.basename(spec.split(':')[-1]))[0]
    if cmd == 'png':
        img = image_decode(load(spec))
        if not img:
            raise SystemExit('not a texture the game reads')
        out = a[2] if len(a) > 2 else os.path.join('out', stem + '.png')
        os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
        open(out, 'wb').write(png(*img))
        print('%s: %dx%d' % (out, img[0], img[1]))
    elif cmd == 'wav':
        d = load(spec)
        out = a[2] if len(a) > 2 else os.path.join('out', stem + '.wav')
        os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
        ad = adp_header(d)
        if ad:
            pcm = adpcm_decode(ad)
            open(out, 'wb').write(wav_file(ad['channels'], ad['rate'], pcm))
        else:
            h = snd_header(d)
            if not h:
                raise SystemExit('not a sound')
            open(out, 'wb').write(wav_file(h[0], h[1], h[3]) if h[2] == 16 else d)
        print(out)
    elif cmd == 'obj':
        obj_export(spec, a[2] if len(a) > 2 else os.path.join('out', stem + '.obj'), a[3] if len(a) > 3 else None)
    elif cmd == 'info':
        d = load(spec)
        if d[:4] == b'PP_F':
            c = C3d(d)
            print('PP_F kind %d: %d objects, %d+%d meshes, %d sections, %d polygons, %d vertices, %d textures' %
                  (c.kind, c.n18, c.n1a, c.n1c, c.n1e, c.n20, c.n14, c.n24))
            for i in range(c.n1a + c.n1c):
                print('  mesh', i, c.mesh(i))
        elif image_decode(d):
            w, h, _ = image_decode(d)
            print('image %dx%d (%s)' % (w, h, 'DDS' if d[:4] == b'DDS ' else 'TGA'))
        elif adp_header(d):
            ad = adp_header(d)
            print('MS ADPCM %d ch %d Hz, block %d, %d frames' % (ad['channels'], ad['rate'], ad['block_align'], ad['frames']))
        elif snd_header(d):
            h = snd_header(d)
            print('WAV %d ch %d Hz %d bit, %d bytes' % (h[0], h[1], h[2], len(h[3])))
        else:
            print('%d bytes, unknown' % len(d))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
