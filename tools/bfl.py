#!/usr/bin/env python3
"""Colin McRae Rally 2.0 container reader (reference implementation, stdlib only).

Layers (docs/formats/bfl.md):
  1. optional gzip (RFC 1952) wrapper, magic 1f 8b
  2. optional "CMPR" + u32 payload size header
  3. payload: concatenated member files followed by a directory and a u32 directory offset
     directory entry = u32 size, u32 offset, u32 name length, name (padded to a multiple of 4)
"""
import gzip
import struct
import sys
import os


def unwrap(data):
    """Strip the gzip and CMPR layers. Returns (payload, layers)."""
    layers = []
    if data[:2] == b'\x1f\x8b':
        data = gzip.decompress(data)
        layers.append('gzip')
    if data[:4] == b'CMPR':
        n, = struct.unpack_from('<I', data, 4)
        if n != len(data) - 8:
            raise ValueError('CMPR size %d != %d' % (n, len(data) - 8))
        data = data[8:]
        layers.append('cmpr')
    return data, layers


def directory(payload):
    """Parse the trailing directory. Returns [(name, offset, size)] or None if there is none."""
    if len(payload) < 4:
        return None
    do, = struct.unpack_from('<I', payload, len(payload) - 4)
    if do >= len(payload) - 4:
        return None
    out, p, end = [], do, len(payload) - 4
    while p < end:
        if p + 12 > end:
            return None
        size, off, nl = struct.unpack_from('<III', payload, p)
        if nl == 0 or nl > 260 or off + size > do:
            return None
        name = payload[p + 12:p + 12 + nl].decode('latin1')
        out.append((name, off, size))
        p += 12 + ((nl + 3) & ~3)
    return out if p == end else None


DATA_DIRS = ('CountrySpecific', 'FrontEnd', 'Game', 'SetupRep', 'Sounds')


def summary(root):
    """One line per member (or per file without a directory) of every file under the CD's data
    directories, paths sorted case-insensitively, and the CRC-32 of all lines: what tests/bfl_test.c
    prints. Members are 'path:name'; paths use '/'."""
    import zlib
    paths = []
    for d in DATA_DIRS:
        for dp, _, fs in os.walk(os.path.join(root, d)):
            for f in fs:
                paths.append(os.path.relpath(os.path.join(dp, f), root).replace(os.sep, '/'))
    # the installer cabinet's files the disc doesn't have, as src/vfs_cab.c layers them
    cabfiles = {}
    if os.path.exists(os.path.join(root, 'data1.hdr')):
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import iscab
        cab = iscab.Cab(root)
        have = {p.lower() for p in paths}
        for f in cab.files:
            p = f['path'].replace('\\', '/')
            if f['off'] and p.split('/')[0] in DATA_DIRS and p.lower() not in have and p.lower() not in cabfiles:
                cabfiles[p.lower()] = (p, f)
        paths += [p for p, _ in cabfiles.values()]
    paths.sort(key=lambda p: p.lower())
    total, lines = 0, 0
    for p in paths:
        if p.lower() in cabfiles:
            data = cab.read(cabfiles[p.lower()][1])
        else:
            data = open(os.path.join(root, p), 'rb').read()
        if data[:2] == b'\x1f\x8b' and gzip.decompress(data)[:4] != b'CMPR':
            payload = data          # File_Load returns a gzip file without CMPR as it is
        else:
            payload, _ = unwrap(data)
        ents = directory(payload)
        out = ['%s %08x %d\n' % (p, zlib.crc32(payload), len(payload))]
        if ents is not None:
            out += ['%s:%s %08x %d\n' % (p, n, zlib.crc32(payload[o:o + sz]), sz) for n, o, sz in ents]
        for line in out:
            total = zlib.crc32(line.encode('latin1'), total)
        lines += len(out)
    print('%d files, %d lines, summary %08x' % (len(paths), lines, total))
    return 0


def main():
    if len(sys.argv) < 2:
        print('usage: bfl.py <file> [outdir]  (list, or extract members into outdir)\n'
              '       bfl.py --summary <cd dir>  (hash of every member, as tests/bfl_test.c)')
        return 2
    if sys.argv[1] == '--summary':
        return summary(sys.argv[2])
    data, layers = unwrap(open(sys.argv[1], 'rb').read())
    ents = directory(data)
    print('layers:', '+'.join(layers) or 'none', 'payload:', len(data))
    if ents is None:
        print('no directory (single file)')
        return 0
    for name, off, size in ents:
        print('%10d %10d  %s' % (off, size, name))
    if len(sys.argv) > 2:
        for name, off, size in ents:
            o = os.path.join(sys.argv[2], name.replace('\\', '/').lstrip('/'))
            os.makedirs(os.path.dirname(o) or '.', exist_ok=True)
            open(o, 'wb').write(data[off:off + size])
    return 0


if __name__ == '__main__':
    sys.exit(main())
