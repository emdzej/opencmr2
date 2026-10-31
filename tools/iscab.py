#!/usr/bin/env python3
"""InstallShield 5/6 cabinet reader (data1.hdr + data<N>.cab), stdlib only. The reference for
src/vfs_cab.c; layout in docs/formats/index.md#installshield-cabinet.

  iscab.py <dir with data1.hdr>                 list: index, volume, flags, size, path
  iscab.py <dir> <path> <out>                   extract one file (path as listed, '\\' or '/')
  iscab.py <dir> --crc                          CRC-32 of every file (checks the C reader)
"""
import os
import struct
import sys
import zlib

FILE_SPLIT, FILE_OBFUSCATED, FILE_COMPRESSED, FILE_INVALID = 1, 2, 4, 8


def u16(b, o): return struct.unpack_from('<H', b, o)[0]
def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def u64(b, o): return struct.unpack_from('<Q', b, o)[0]


def cstr(b, o):
    return b[o:b.index(b'\0', o)].decode('latin-1')


class Cab:
    def __init__(self, folder, prefix='data'):
        self.folder, self.prefix = folder, prefix
        h = self._read(f'{prefix}1.hdr')
        if h[:4] != b'ISc(':
            raise ValueError('not an InstallShield header')
        ver = u32(h, 4)
        self.major = (ver >> 12) & 0xf if ver >> 24 == 1 else (5 if (ver & 0xffff) == 0 else 6)
        if self.major < 5:
            raise ValueError(f'InstallShield {self.major} cabinets are not supported')
        cd = u32(h, 12)
        ft = cd + u32(h, cd + 0x0c)          # file table
        ndirs, nfiles = u32(h, cd + 0x1c), u32(h, cd + 0x28)
        ft2 = u32(h, cd + 0x2c)
        dirs = [cstr(h, ft + u32(h, ft + 4 * i)) for i in range(ndirs)]
        self.files = []
        for i in range(nfiles):
            p = ft + ft2 + i * 0x57
            flags = u16(h, p)
            size, csize, off = u64(h, p + 2), u64(h, p + 10), u64(h, p + 18)
            name = cstr(h, ft + u32(h, p + 0x3a))
            d = u16(h, p + 0x3e)
            vol = u16(h, p + 0x55)
            path = (dirs[d] + '\\' + name) if dirs[d] else name
            self.files.append(dict(index=i, flags=flags, size=size, csize=csize, off=off, vol=vol, path=path))

    def _read(self, name):
        with open(os.path.join(self.folder, name), 'rb') as f:
            return f.read()

    def _volume(self, n):
        b = self._read(f'{self.prefix}{n}.cab')
        # common header (20 bytes), then the volume header (version 6 layout)
        v = dict(first=u32(b, 0x1c), last=u32(b, 0x20), first_off=u64(b, 0x24), first_csize=u64(b, 0x34),
                 last_off=u64(b, 0x3c), last_csize=u64(b, 0x4c))
        return b, v

    def read(self, fd):
        if fd['flags'] & FILE_INVALID:
            raise ValueError('invalid file')
        vol = fd['vol']
        b, v = self._volume(vol)
        off = fd['off']
        left = fd['csize']
        avail = left
        if fd['index'] == v['last'] and v['last_csize'] and v['last_csize'] != left:
            avail = v['last_csize']
        raw = bytearray()
        while left:
            take = min(avail, left)
            raw += b[off:off + take]
            left -= take
            if left:
                vol += 1
                b, v = self._volume(vol)
                off, avail = v['first_off'], v['first_csize']
        if not fd['flags'] & FILE_COMPRESSED:
            return bytes(raw)
        out = bytearray()
        p = 0
        while p < len(raw):
            n = u16(raw, p)
            p += 2
            d = zlib.decompressobj(-15)
            out += d.decompress(bytes(raw[p:p + n]))
            p += n
        if len(out) != fd['size']:
            raise ValueError(f"{fd['path']}: {len(out)} bytes, expected {fd['size']}")
        return bytes(out)


def main():
    c = Cab(sys.argv[1])
    if len(sys.argv) == 2:
        for f in c.files:
            print(f"{f['index']:4} {f['vol']} {f['flags']:02x} {f['size']:9} {f['path']}")
    elif sys.argv[2] == '--crc':
        for f in c.files:
            if f['flags'] & FILE_INVALID or not f['path']:
                continue
            print(f"{zlib.crc32(c.read(f)):08x} {f['size']:9} {f['path']}")
    else:
        want = sys.argv[2].replace('/', '\\').lower()
        for f in c.files:
            if f['path'].lower() == want:
                with open(sys.argv[3], 'wb') as o:
                    o.write(c.read(f))
                return
        sys.exit(f'{sys.argv[2]}: not in the cabinet')


if __name__ == '__main__':
    main()
