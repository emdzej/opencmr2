/* DEFLATE (RFC 1951) and gzip (RFC 1952) decoding for the game's containers: every data file on the disc is
   gzip (docs/formats/bfl.md), and InstallShield cabinets hold raw-deflate chunks (vfs_cab.c). The original
   links zlib 1.1.3; this is the port's own decoder, checked against Python's zlib over the whole disc
   (tests/bfl_test.c, tests/cab_test.c). */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* Raw deflate stream in src -> dst. Returns the decoded size, or -1 on a malformed stream or if the output
   would exceed dstcap. *used (may be NULL) gets the number of input bytes consumed. */
int64_t inflate_raw(const uint8_t *src, size_t srclen, uint8_t *dst, size_t dstcap, size_t *used);

/* A gzip member: header checked, data inflated into a malloc'd buffer sized by the trailer, CRC-32 and size
   checked. NULL if src is not gzip or is damaged. */
uint8_t *gunzip(const uint8_t *src, size_t srclen, size_t *outlen);
