#!/usr/bin/env bash
# The reference for tests/bink_test.c: FrontEnd/Videos/cm.bik decoded by ffmpeg (yuv420p, every frame)
# as one CRC-32, for OPENCMR2_BINK_VIDEO. Usage: tools/bink-ref.sh [cd dir]
set -euo pipefail
ffmpeg -v error -i "${1:-cd}/FrontEnd/Videos/cm.bik" -f rawvideo -pix_fmt yuv420p - |
  python3 -c 'import sys, zlib; c = 0
for b in iter(lambda: sys.stdin.buffer.read(1 << 20), b""): c = zlib.crc32(b, c)
print("%08x" % c)'
