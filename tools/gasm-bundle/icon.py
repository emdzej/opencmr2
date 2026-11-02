#!/usr/bin/env python3
"""Draw OpenCMR2's app icon as a PNG of any size.

The design is the site's own mark (docs/public/favicon.svg): the language screen's dot flag, white dots with a
red cross, on a rounded square of the menus' sage. It is drawn here from the same geometry (a 64 x 64 canvas)
rather than rasterised from the SVG, so the packaging needs nothing but the Python standard library (CI
runners have no SVG renderer or Pillow). Edges are anti-aliased from signed distances, one evaluation per
pixel. No game art is involved.

    tools/gasm-bundle/icon.py <size> <out.png>
"""
import math
import struct
import sys
import zlib

# The favicon's geometry, in its 64 x 64 viewBox.
RECT = (0.0, 0.0, 64.0, 64.0, 12.0)   # x0 y0 x1 y1 corner radius
DOT_R = 5.0
WHITE_DOTS = [(14, 14), (50, 14), (14, 50), (50, 50)]
RED_DOTS = [(32, 14), (14, 32), (32, 32), (50, 32), (32, 50)]


def hexrgb(h):
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (1, 3, 5))


SAGE_RGB = hexrgb('#9cb4ac')
WHITE_RGB = hexrgb('#ffffff')
RED_RGB = hexrgb('#c80000')


def rounded_rect_sd(x, y):
    """Signed distance to the rounded rectangle (negative inside)."""
    x0, y0, x1, y1, r = RECT
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - r, (y1 - y0) / 2 - r
    qx, qy = abs(x - cx) - hx, abs(y - cy) - hy
    out = math.hypot(max(qx, 0.0), max(qy, 0.0))
    return out + min(max(qx, qy), 0.0) - r


def dots_sd(x, y, dots):
    """Signed distance to the nearest of the dots (negative inside)."""
    return min(math.hypot(x - cx, y - cy) for cx, cy in dots) - DOT_R


def coverage(sd, px):
    """Fraction of a pixel (px canvas units wide) covered by the shape at signed distance sd."""
    return max(0.0, min(1.0, 0.5 - sd / px))


def over(dst, rgb, a):
    """Composite a straight-alpha colour over a premultiplied pixel."""
    r, g, b, da = dst
    k = 1.0 - a
    return (rgb[0] * a + r * k, rgb[1] * a + g * k, rgb[2] * a + b * k, a + da * k)


def render(size):
    px = 64.0 / size
    rows = []
    for j in range(size):
        y = (j + 0.5) * px
        line = bytearray()
        for i in range(size):
            x = (i + 0.5) * px
            p = (0.0, 0.0, 0.0, 0.0)
            rd = rounded_rect_sd(x, y)
            if rd < px:
                p = over(p, SAGE_RGB, coverage(rd, px))
                p = over(p, WHITE_RGB, coverage(dots_sd(x, y, WHITE_DOTS), px))
                p = over(p, RED_RGB, coverage(dots_sd(x, y, RED_DOTS), px))
            a = p[3]
            u = 1.0 / a if a > 0 else 0.0
            line += bytes((min(255, round(p[0] * u * 255)), min(255, round(p[1] * u * 255)),
                           min(255, round(p[2] * u * 255)), min(255, round(a * 255))))
        rows.append(line)
    return rows


def write_png(path, w, h, rows):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    raw = b''.join(b'\0' + bytes(r) for r in rows)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    size = int(sys.argv[1])
    if not 8 <= size <= 2048:
        raise SystemExit('size must be 8..2048')
    write_png(sys.argv[2], size, size, render(size))


if __name__ == '__main__':
    main()
