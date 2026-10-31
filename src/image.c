/* Texture images (see image.h). */
#include "image.h"
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* Tga_Header 0x4a6670. Accepted: no colour map (byte 1 = 0), image type 2, x and y origin 0, descriptor
   bits 4-6 clear (so bottom-up rows, left to right; the original never reads a top-down TGA), 24 bpp,
   or 32 bpp with 8 alpha bits. The pixels follow the 18-byte header and the image ID. The colour map
   spec (bytes 3-7) is not looked at. */
bool Tga_Header(const uint8_t *f, size_t size, TgaInfo *out)
{
    if (!f || size < 18 || f[1] != 0 || rd16(f + 8) || rd16(f + 10) || (f[17] & 0x70) || f[2] != 2) return false;
    if (f[16] == 24) out->bpp = 3;
    else if (f[16] == 32 && (f[17] & 15) == 8) out->bpp = 4;
    else return false;
    out->width = rd16(f + 12);
    out->height = rd16(f + 14);
    size_t start = 18u + f[0];
    if (start > size || (size - start) / (size_t)out->bpp / (out->width ? out->width : 1) < (size_t)out->height)
        return false;
    out->pixels = f + start;
    return true;
}

/* The pixel fetch of Tga_Pixel 0x4a60d0: pixel (x, y) is stored at row height-1-y, as B, G, R(, A). Its
   brightness and colour-matrix options (the texture-adjust dialog) belong to the upload (step 3). A
   24-bit TGA is opaque. */
uint8_t *Tga_Decode(const uint8_t *file, size_t size, int *w, int *h)
{
    TgaInfo t;
    if (!Tga_Header(file, size, &t) || !t.width || !t.height) return NULL;
    uint8_t *out = malloc((size_t)t.width * t.height * 4);
    if (!out) return NULL;
    for (int y = 0; y < t.height; y++) {
        const uint8_t *s = t.pixels + (size_t)(t.height - 1 - y) * t.width * t.bpp;
        uint8_t *d = out + (size_t)y * t.width * 4;
        for (int x = 0; x < t.width; x++, s += t.bpp, d += 4) {
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            d[3] = t.bpp == 4 ? s[3] : 255;
        }
    }
    *w = t.width;
    *h = t.height;
    return out;
}

/* DXT colour block: two RGB565 end points (bits widened by replication), 2-bit indices. In DXT1 c0 <= c1
   selects the three-colour mode with transparent black; DXT2-5 always use four colours. */
static void dxt_colour(const uint8_t *b, uint8_t c[4][4], bool dxt1)
{
    uint16_t e[2] = {rd16(b), rd16(b + 2)};
    for (int i = 0; i < 2; i++) {
        unsigned r = e[i] >> 11, g = e[i] >> 5 & 63, bl = e[i] & 31;
        c[i][0] = (uint8_t)(r << 3 | r >> 2);
        c[i][1] = (uint8_t)(g << 2 | g >> 4);
        c[i][2] = (uint8_t)(bl << 3 | bl >> 2);
        c[i][3] = 255;
    }
    for (int k = 0; k < 3; k++) {
        if (!dxt1 || e[0] > e[1]) {
            c[2][k] = (uint8_t)((2 * c[0][k] + c[1][k]) / 3);
            c[3][k] = (uint8_t)((c[0][k] + 2 * c[1][k]) / 3);
        } else {
            c[2][k] = (uint8_t)((c[0][k] + c[1][k]) / 2);
            c[3][k] = 0;
        }
    }
    c[2][3] = 255;
    c[3][3] = !dxt1 || e[0] > e[1] ? 255 : 0;
}

/* DXT5 alpha block: two 8-bit end points, 3-bit indices; a0 > a1 interpolates 6 values, else 4 plus 0
   and 255. */
static void dxt5_alpha(const uint8_t *b, uint8_t a[16])
{
    uint8_t v[8] = {b[0], b[1]};
    if (v[0] > v[1])
        for (int i = 1; i < 7; i++) v[i + 1] = (uint8_t)(((7 - i) * v[0] + i * v[1]) / 7);
    else {
        for (int i = 1; i < 5; i++) v[i + 1] = (uint8_t)(((5 - i) * v[0] + i * v[1]) / 5);
        v[6] = 0;
        v[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
    for (int i = 0; i < 16; i++) a[i] = v[bits >> (3 * i) & 7];
}

/* DDS as Tex_LoadDds 0x4a6e30 takes it: the 124-byte surface description after "DDS " is passed to
   CreateSurface as it is, the data at 0x80 copied into the surface. FourCC DXT1 or DXT5 (every file on
   the disc is DXT5, one mip level). A size that is not a power of two from 2 to 2048 only makes the
   original note the name; it loads anyway. */
uint8_t *Dds_Decode(const uint8_t *file, size_t size, int *w, int *h)
{
    if (size < 0x80 || memcmp(file, "DDS ", 4)) return NULL;
    uint32_t height = rd32(file + 12), width = rd32(file + 16), fourcc = rd32(file + 84);
    bool dxt1 = fourcc == 0x31545844u;
    if ((!dxt1 && fourcc != 0x35545844u) || !width || !height || width > 4096 || height > 4096) return NULL;
    uint32_t bw = (width + 3) / 4, bh = (height + 3) / 4, bs = dxt1 ? 8 : 16;
    if ((size - 0x80) / bs / bw < bh) return NULL;
    uint8_t *out = malloc((size_t)width * height * 4);
    if (!out) return NULL;
    const uint8_t *s = file + 0x80;
    for (uint32_t by = 0; by < bh; by++)
        for (uint32_t bx = 0; bx < bw; bx++, s += bs) {
            uint8_t a[16], c[4][4];
            if (!dxt1) dxt5_alpha(s, a);
            const uint8_t *cb = dxt1 ? s : s + 8;
            dxt_colour(cb, c, dxt1);
            uint32_t idx = rd32(cb + 4);
            for (int i = 0; i < 16; i++) {
                uint32_t x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x >= width || y >= height) continue;
                uint8_t *d = out + ((size_t)y * width + x) * 4;
                memcpy(d, c[idx >> (2 * i) & 3], 4);
                if (!dxt1) d[3] = a[i];
            }
        }
    *w = (int)width;
    *h = (int)height;
    return out;
}

uint8_t *Image_Decode(const uint8_t *file, size_t size, int *w, int *h)
{
    if (size >= 4 && rd32(file) == 0x20534444u) return Dds_Decode(file, size, w, h);
    return Tga_Decode(file, size, w, h);
}
