/* Bink 1 (see bink.h). */
#include "bink.h"
#include "exe.h"
#include "vfs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the DLL's tables ---- */

enum { DLL_SIZE = 272384, DLL_CRC32 = 0x1b8bab1b };

/* addresses in binkw32.dll (image base 0x10000000) */
enum {
    T_TREE_LENS = 0x10037100,   /* 16 trees x 16 code lengths (1-7 bits) */
    T_TREE_CODES = 0x10037208,  /* their codes, read from the low bit up */
    T_PATTERNS = 0x10036218,    /* 16 x 64: the run blocks' orders */
    T_SCAN = 0x1003d7dc,        /* 64: the coefficients' order */
    T_INTRA_Q = 0x1003220c,     /* 16 x 64 int32, 16.16, in raster order */
    T_INTER_Q = 0x1003420c,
    T_CRIT = 0x1003d578,        /* 24 uint32: the audio's critical band edges (Hz) */
    T_RLE = 0x1003d704,         /* 16 bytes: the audio's run lengths, in eights */
};

static uint8_t tree_lens[16][16], tree_codes[16][16], patterns[16][64], scan[64], rle_len[16];
static int32_t intra_q[16][64], inter_q[16][64];
static uint32_t crit[24];
static struct { uint8_t sym, len; } vlc[16][128];   /* every 7-bit window: the code it starts with */
static bool tables;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* a virtual address of the DLL in its file (the DLL's size is checked, so the tables are there) */
static const uint8_t *at_va(const uint8_t *d, uint32_t va)
{
    uint32_t pe = rd32(d + 0x3c), nsec = rd16(d + pe + 6), opt = rd16(d + pe + 20), base = rd32(d + pe + 24 + 28);
    for (uint32_t i = 0; i < nsec; i++) {
        const uint8_t *s = d + pe + 24 + opt + i * 40;
        uint32_t v = base + rd32(s + 12), r = rd32(s + 16);
        if (va >= v && va < v + r) return d + rd32(s + 20) + (va - v);
    }
    return d;
}

bool Bink_LoadTables(char *err, size_t cap)
{
    if (tables) return true;
    size_t n = 0;
    uint8_t *d = vfs_read_all(BINK_DLL, &n);
    if (!d || n != DLL_SIZE || crc32(d, n) != DLL_CRC32) {
        if (err) snprintf(err, cap, "%s: %s", BINK_DLL, d ? "unsupported version" : "not found");
        free(d);
        return false;
    }
#define AT(va) at_va(d, va)
    memcpy(tree_lens, AT(T_TREE_LENS), sizeof tree_lens);
    memcpy(tree_codes, AT(T_TREE_CODES), sizeof tree_codes);
    memcpy(patterns, AT(T_PATTERNS), sizeof patterns);
    memcpy(scan, AT(T_SCAN), sizeof scan);
    memcpy(rle_len, AT(T_RLE), sizeof rle_len);
    const uint8_t *qi = AT(T_INTRA_Q), *qe = AT(T_INTER_Q), *cr = AT(T_CRIT);
    for (int t = 0; t < 16; t++)
        for (int i = 0; i < 64; i++) intra_q[t][i] = (int32_t)rd32(qi + 4 * (64 * t + i)), inter_q[t][i] = (int32_t)rd32(qe + 4 * (64 * t + i));
    for (int i = 0; i < 24; i++) crit[i] = rd32(cr + 4 * i);
#undef AT
    free(d);
    for (int t = 0; t < 16; t++)
        for (int s = 0; s < 16; s++)
            for (int f = 0; f < 1 << (7 - tree_lens[t][s]); f++) {
                int k = tree_codes[t][s] | f << tree_lens[t][s];
                vlc[t][k].sym = (uint8_t)s, vlc[t][k].len = tree_lens[t][s];
            }
    tables = true;
    return true;
}

/* ---- bits, from the low bit of each byte up ---- */

typedef struct {
    const uint8_t *p;
    size_t pos, end;   /* in bits */
} Bits;

static uint32_t peek(const Bits *g, int n)
{
    size_t b = g->pos >> 3;
    uint64_t w = 0;
    for (int i = 0; i < 8; i++) w |= (uint64_t)g->p[b + (size_t)i] << (8 * i);   /* the buffers are padded */
    return (uint32_t)((w >> (g->pos & 7)) & ((1ull << n) - 1));
}
static uint32_t get(Bits *g, int n)
{
    if (!n) return 0;
    uint32_t v = peek(g, n);
    g->pos += (size_t)n;
    return v;
}
static int get1(Bits *g) { return (int)get(g, 1); }

/* ---- the video ---- */

enum { B_TYPES, B_SUBTYPES, B_COLORS, B_PATTERN, B_XOFF, B_YOFF, B_INTRA_DC, B_INTER_DC, B_RUN, B_N };
enum { SKIP, SCALED, MOTION, RUN, RESIDUE, INTRA, FILL, INTER, PATTERN, RAW };

typedef struct { int vlc; uint8_t syms[16]; } Tree;

typedef struct {
    int len;              /* the bits of a chunk's count */
    Tree tree;
    uint8_t *data, *end;  /* the decoded values (bytes, or int16 for the DCs) */
    uint8_t *dec, *ptr;   /* the next to decode, the next to use */
} Bundle;

typedef struct {
    int rate, channels, frame_len, overlap, block, nbands, bands[26];
    float root, quant[96], prev[256], coeffs[4096];
    bool first;
} Audio;

struct Bink {
    VfsFile *f;
    uint32_t frames, frame, w, h, fps_num, fps_den, flags, ntracks, track;
    uint32_t *index;     /* frames + 1 offsets, bit 0 the key frames */
    uint8_t *pkt;
    size_t pkt_cap;
    uint8_t *plane[2][3];   /* the current and last frame */
    int cur;
    Bundle bundle[B_N];
    Tree col_high[16];
    int col_last;
    Audio au;
    bool has_audio;
    float *queue;
    int q_len, q_cap;   /* in samples */
};

static int get_huff(Bits *g, const Tree *t)
{
    uint32_t k = peek(g, 7);
    g->pos += vlc[t->vlc][k].len;
    return t->syms[vlc[t->vlc][k].sym];
}

static void merge(Bits *g, uint8_t *dst, const uint8_t *src, int size)
{
    const uint8_t *src2 = src + size;
    int size2 = size;
    do {
        if (!get1(g)) *dst++ = *src++, size--;
        else *dst++ = *src2++, size2--;
    } while (size && size2);
    while (size--) *dst++ = *src++;
    while (size2--) *dst++ = *src2++;
}

/* a tree: its code set and the symbols in code order, given outright or as merges of halves */
static void read_tree(Bits *g, Tree *t)
{
    uint8_t tmp1[16] = {0}, tmp2[16], *in = tmp1, *out = tmp2;
    t->vlc = (int)get(g, 4);
    if (!t->vlc) {
        for (int i = 0; i < 16; i++) t->syms[i] = (uint8_t)i;
        return;
    }
    if (get1(g)) {
        int len = (int)get(g, 3);
        for (int i = 0; i <= len; i++) t->syms[i] = (uint8_t)get(g, 4), tmp1[t->syms[i]] = 1;
        for (int i = 0; i < 16 && len < 15; i++)
            if (!tmp1[i]) t->syms[++len] = (uint8_t)i;
    } else {
        int len = (int)get(g, 2);
        for (int i = 0; i < 16; i++) in[i] = (uint8_t)i;
        for (int i = 0; i <= len; i++) {
            int size = 1 << i;
            for (int k = 0; k < 16; k += size << 1) merge(g, out + k, in + k, size);
            uint8_t *x = in;
            in = out, out = x;
        }
        memcpy(t->syms, in, 16);
    }
}

static int ilog2(uint32_t v) { int n = 0; while (v >>= 1) n++; return n; }

static void init_lengths(Bink *b, int width, int bw)
{
    width = (width + 7) & ~7;
    b->bundle[B_TYPES].len = ilog2((uint32_t)(width >> 3) + 511) + 1;
    b->bundle[B_SUBTYPES].len = ilog2((uint32_t)(width >> 4) + 511) + 1;
    b->bundle[B_COLORS].len = ilog2((uint32_t)bw * 64 + 511) + 1;
    b->bundle[B_INTRA_DC].len = b->bundle[B_INTER_DC].len = b->bundle[B_XOFF].len = b->bundle[B_YOFF].len = ilog2((uint32_t)(width >> 3) + 511) + 1;
    b->bundle[B_PATTERN].len = ilog2((uint32_t)bw * 8 + 511) + 1;
    b->bundle[B_RUN].len = ilog2((uint32_t)bw * 48 + 511) + 1;
}

static void read_bundle(Bits *g, Bink *b, int n)
{
    if (n == B_COLORS) {
        for (int i = 0; i < 16; i++) read_tree(g, &b->col_high[i]);
        b->col_last = 0;
    }
    if (n != B_INTRA_DC && n != B_INTER_DC) read_tree(g, &b->bundle[n].tree);
    b->bundle[n].dec = b->bundle[n].ptr = b->bundle[n].data;
}

/* a chunk's count when every value decoded so far has been used; 0 none (and none again in this plane
   when the count is 0) */
static int chunk(Bits *g, Bundle *u)
{
    if (!u->dec || u->dec > u->ptr) return 0;
    int t = (int)get(g, u->len);
    if (!t) u->dec = NULL;
    if (t && u->dec + t > u->end) { u->dec = NULL; return -1; }
    return t;
}

static int read_runs(Bits *g, Bundle *u)
{
    int t = chunk(g, u);
    if (t <= 0) return t;
    if (get1(g)) {
        memset(u->dec, (int)get(g, 4), (size_t)t);
        u->dec += t;
    } else
        for (uint8_t *e = u->dec + t; u->dec < e;) *u->dec++ = (uint8_t)get_huff(g, &u->tree);
    return 0;
}

static int signed_val(Bits *g, int v) { return v && get1(g) ? -v : v; }

static int read_motion(Bits *g, Bundle *u)
{
    int t = chunk(g, u);
    if (t <= 0) return t;
    if (get1(g)) {
        int v = signed_val(g, (int)get(g, 4));
        memset(u->dec, (uint8_t)(int8_t)v, (size_t)t);
        u->dec += t;
    } else
        for (uint8_t *e = u->dec + t; u->dec < e;) *u->dec++ = (uint8_t)(int8_t)signed_val(g, get_huff(g, &u->tree));
    return 0;
}

static int read_block_types(Bits *g, Bundle *u)
{
    static const int rle[4] = {4, 8, 12, 32};
    int t = chunk(g, u);
    if (t <= 0) return t;
    if (get1(g)) {
        memset(u->dec, (int)get(g, 4), (size_t)t);
        u->dec += t;
        return 0;
    }
    int last = 0;
    for (uint8_t *e = u->dec + t; u->dec < e;) {
        int v = get_huff(g, &u->tree);
        if (v < 12) last = v, *u->dec++ = (uint8_t)v;
        else {
            int run = rle[v - 12];
            if (e - u->dec < run) return -1;
            memset(u->dec, last, (size_t)run);
            u->dec += run;
        }
    }
    return 0;
}

static int read_patterns(Bits *g, Bundle *u)
{
    int t = chunk(g, u);
    if (t <= 0) return t;
    for (uint8_t *e = u->dec + t; u->dec < e;) {
        int v = get_huff(g, &u->tree);
        v |= get_huff(g, &u->tree) << 4;
        *u->dec++ = (uint8_t)v;
    }
    return 0;
}

/* the colours: each a high nibble from the tree the last high nibble picks, then a low one */
static int read_colors(Bits *g, Bink *b)
{
    Bundle *u = &b->bundle[B_COLORS];
    int t = chunk(g, u);
    if (t <= 0) return t;
    if (get1(g)) {
        b->col_last = get_huff(g, &b->col_high[b->col_last]);
        int v = b->col_last << 4 | get_huff(g, &u->tree);
        memset(u->dec, v, (size_t)t);
        u->dec += t;
    } else
        for (uint8_t *e = u->dec + t; u->dec < e;) {
            b->col_last = get_huff(g, &b->col_high[b->col_last]);
            *u->dec++ = (uint8_t)(b->col_last << 4 | get_huff(g, &u->tree));
        }
    return 0;
}

/* the DCs: the first in 11 bits (10 and a sign for the inter ones), then in groups of eight as
   differences of a width given per group */
static int read_dcs(Bits *g, Bundle *u, bool has_sign)
{
    int len = chunk(g, u);
    if (len <= 0) return len;
    int16_t *dst = (int16_t *)u->dec;
    if ((uint8_t *)(dst + len) > u->end) return -1;
    int v = (int)get(g, 11 - has_sign);
    if (v && has_sign) v = signed_val(g, v);
    *dst++ = (int16_t)v;
    len--;
    for (int i = 0; i < len; i += 8) {
        int n2 = len - i < 8 ? len - i : 8, bsize = (int)get(g, 4);
        for (int j = 0; j < n2; j++) {
            if (bsize) v += signed_val(g, (int)get(g, bsize));
            if (v < -32768 || v > 32767) return -1;
            *dst++ = (int16_t)v;
        }
    }
    u->dec = (uint8_t *)dst;
    return 0;
}

static int value(Bink *b, int n)
{
    Bundle *u = &b->bundle[n];
    if (n < B_XOFF || n == B_RUN) return *u->ptr++;
    if (n == B_XOFF || n == B_YOFF) return (int8_t)*u->ptr++;
    int16_t v;
    memcpy(&v, u->ptr, 2);
    u->ptr += 2;
    return v;
}

/* the coefficients, by bit planes from the top: lists of single coefficients (mode 3) and groups of
   four (modes 0-2) still to be found significant; the quantiser index last */
static int read_dct(Bits *g, int32_t block[64], int *count, int idx[64])
{
    int coef[128], mode[128], start = 64, end = 64, n = 0;
    coef[end] = 4, mode[end++] = 0;
    coef[end] = 24, mode[end++] = 0;
    coef[end] = 44, mode[end++] = 0;
    coef[end] = 1, mode[end++] = 3;
    coef[end] = 2, mode[end++] = 3;
    coef[end] = 3, mode[end++] = 3;
    for (int bits = (int)get(g, 4) - 1; bits >= 0; bits--) {
        int pos = start;
        while (pos < end) {
            if (!(mode[pos] | coef[pos]) || !get1(g)) {
                pos++;
                continue;
            }
            int cc = coef[pos], m = mode[pos];
            switch (m) {
            case 0:
            case 2:
                if (m == 0) coef[pos] = cc + 4, mode[pos] = 1;
                else coef[pos] = 0, mode[pos++] = 0;
                for (int i = 0; i < 4; i++, cc++) {
                    if (get1(g)) {
                        coef[--start] = cc, mode[start] = 3;
                    } else {
                        int t = !bits ? 1 - (get1(g) << 1) : signed_val(g, (int)get(g, bits) | 1 << bits);
                        block[scan[cc]] = t;
                        idx[n++] = cc;
                    }
                }
                break;
            case 1:
                mode[pos] = 2;
                for (int i = 0; i < 3; i++) cc += 4, coef[end] = cc, mode[end++] = 2;
                break;
            case 3: {
                int t = !bits ? 1 - (get1(g) << 1) : signed_val(g, (int)get(g, bits) | 1 << bits);
                block[scan[cc]] = t;
                idx[n++] = cc;
                coef[pos] = 0, mode[pos++] = 0;
                break;
            }
            }
        }
    }
    *count = n;
    return (int)get(g, 4);
}

/* the quantiser (raster order) on the DC and the coefficients found */
static void unquant(int32_t block[64], const int32_t q[64], int count, const int idx[64])
{
    block[0] = (int32_t)((uint32_t)block[0] * (uint32_t)q[0]) >> 11;
    for (int i = 0; i < count; i++) {
        int k = scan[idx[i]];
        block[k] = (int32_t)((uint32_t)block[k] * (uint32_t)q[k]) >> 11;
    }
}

/* the residue: bit planes of a magnitude from 1 << (3 bits) down, the coefficients already found
   refined first; at most `masks` steps */
static void read_residue(Bits *g, int16_t block[64], int masks)
{
    int coef[128], mode[128], start = 64, end = 64, nz[64], nnz = 0;
    coef[end] = 4, mode[end++] = 0;
    coef[end] = 24, mode[end++] = 0;
    coef[end] = 44, mode[end++] = 0;
    coef[end] = 0, mode[end++] = 2;
    for (int mask = 1 << get(g, 3); mask; mask >>= 1) {
        for (int i = 0; i < nnz; i++) {
            if (!get1(g)) continue;
            if (block[nz[i]] < 0) block[nz[i]] = (int16_t)(block[nz[i]] - mask);
            else block[nz[i]] = (int16_t)(block[nz[i]] + mask);
            if (--masks < 0) return;
        }
        int pos = start;
        while (pos < end) {
            if (!(coef[pos] | mode[pos]) || !get1(g)) {
                pos++;
                continue;
            }
            int cc = coef[pos], m = mode[pos];
            switch (m) {
            case 0:
            case 2:
                if (m == 0) coef[pos] = cc + 4, mode[pos] = 1;
                else coef[pos] = 0, mode[pos++] = 0;
                for (int i = 0; i < 4; i++, cc++) {
                    if (get1(g)) {
                        coef[--start] = cc, mode[start] = 3;
                    } else {
                        nz[nnz++] = scan[cc];
                        block[scan[cc]] = (int16_t)(get1(g) ? -mask : mask);
                        if (--masks < 0) return;
                    }
                }
                break;
            case 1:
                mode[pos] = 2;
                for (int i = 0; i < 3; i++) cc += 4, coef[end] = cc, mode[end++] = 2;
                break;
            case 3:
                nz[nnz++] = scan[cc];
                block[scan[cc]] = (int16_t)(get1(g) ? -mask : mask);
                coef[pos] = 0, mode[pos++] = 0;
                if (--masks < 0) return;
                break;
            }
        }
    }
}

/* the 8 x 8 inverse DCT in 11-bit fixed point (columns, then rows rounded by 1 / 256) */
#define MUL(x, y) ((int)((unsigned)(x) * (unsigned)(y)) >> 11)
static void idct_1d(int *d, const int *s, int st, int dst_st, bool row)
{
    const int A1 = 2896, A2 = 2217, A3 = 3784, A4 = -5352;
    int a0 = s[0] + s[4 * st], a1 = s[0] - s[4 * st];
    int a2 = s[2 * st] + s[6 * st], a3 = MUL(A1, s[2 * st] - s[6 * st]);
    int a4 = s[5 * st] + s[3 * st], a5 = s[5 * st] - s[3 * st];
    int a6 = s[1 * st] + s[7 * st], a7 = s[1 * st] - s[7 * st];
    int b0 = a4 + a6, b1 = MUL(A3, a5 + a7);
    int b2 = MUL(A4, a5) - b0 + b1, b3 = MUL(A1, a6 - a4) - b2, b4 = MUL(A2, a7) + b3 - b1;
    int o[8] = {a0 + a2 + b0, a1 + a3 - a2 + b2, a1 - a3 + a2 + b3, a0 - a2 - b4,
                a0 - a2 + b4, a1 - a3 + a2 - b3, a1 + a3 - a2 - b2, a0 + a2 - b0};
    for (int i = 0; i < 8; i++) d[i * dst_st] = row ? (o[i] + 0x7f) >> 8 : o[i];
}
static void idct(int out[64], const int32_t in[64])
{
    int tmp[64], src[64];
    for (int i = 0; i < 64; i++) src[i] = in[i];
    for (int i = 0; i < 8; i++) {
        const int *s = src + i;
        if (!(s[8] | s[16] | s[24] | s[32] | s[40] | s[48] | s[56]))
            for (int k = 0; k < 8; k++) tmp[i + 8 * k] = s[0];
        else
            idct_1d(tmp + i, s, 8, 8, false);
    }
    for (int i = 0; i < 8; i++) idct_1d(out + 8 * i, tmp + 8 * i, 1, 1, true);
}
#undef MUL

static void idct_put(uint8_t *dst, int stride, const int32_t blk[64])
{
    int o[64];
    idct(o, blk);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) dst[y * stride + x] = (uint8_t)o[8 * y + x];
}
static void idct_add(uint8_t *dst, int stride, const int32_t blk[64])
{
    int o[64];
    idct(o, blk);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) dst[y * stride + x] = (uint8_t)(dst[y * stride + x] + o[8 * y + x]);
}
static void copy8(uint8_t *dst, const uint8_t *src, int stride)
{
    for (int y = 0; y < 8; y++) memcpy(dst + y * stride, src + y * stride, 8);
}
static void fill(uint8_t *dst, int v, int stride, int n)
{
    for (int y = 0; y < n; y++) memset(dst + y * stride, v, (size_t)n);
}
static void scale_block(const uint8_t src[64], uint8_t *dst, int stride)
{
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            uint8_t v = src[8 * y + x];
            dst[(2 * y) * stride + 2 * x] = dst[(2 * y) * stride + 2 * x + 1] = v;
            dst[(2 * y + 1) * stride + 2 * x] = dst[(2 * y + 1) * stride + 2 * x + 1] = v;
        }
}

/* a run block's colours along pattern p: runs of one colour or of single ones */
static int run_block(Bits *g, Bink *b, uint8_t *dst, int stride, bool ublock)
{
    const uint8_t *sc = patterns[get(g, 4)];
    int i = 0;
    do {
        int run = value(b, B_RUN) + 1;
        i += run;
        if (i > 64) return -1;
        if (get1(g)) {
            int v = value(b, B_COLORS);
            for (int j = 0; j < run; j++, sc++) dst[ublock ? *sc : (*sc & 7) + (*sc >> 3) * stride] = (uint8_t)v;
        } else
            for (int j = 0; j < run; j++, sc++) dst[ublock ? *sc : (*sc & 7) + (*sc >> 3) * stride] = (uint8_t)value(b, B_COLORS);
    } while (i < 63);
    if (i == 63) dst[ublock ? *sc : (*sc & 7) + (*sc >> 3) * stride] = (uint8_t)value(b, B_COLORS);
    return 0;
}

static int plane_w(const Bink *b, int p) { return (int)b->w >> (p ? 1 : 0); }
static int plane_h(const Bink *b, int p) { return (int)b->h >> (p ? 1 : 0); }

static bool decode_plane(Bits *g, Bink *b, int p, bool chroma)
{
    int stride = plane_w(b, p), height = plane_h(b, p);
    int bw = chroma ? ((int)b->w + 15) >> 4 : ((int)b->w + 7) >> 3;
    int bh = chroma ? ((int)b->h + 15) >> 4 : ((int)b->h + 7) >> 3;
    uint8_t *cur = b->plane[b->cur][p], *last = b->plane[b->cur ^ 1][p];
    const uint8_t *ref_lo = last, *ref_hi = last + (size_t)stride * (size_t)(height - 8) + (size_t)stride - 8;   /* the last block's corner */
    init_lengths(b, stride > 8 ? stride : 8, bw);
    for (int i = 0; i < B_N; i++) read_bundle(g, b, i);
    for (int by = 0; by < bh; by++) {
        if (read_block_types(g, &b->bundle[B_TYPES]) < 0 || read_block_types(g, &b->bundle[B_SUBTYPES]) < 0 ||
            read_colors(g, b) < 0 || read_patterns(g, &b->bundle[B_PATTERN]) < 0 ||
            read_motion(g, &b->bundle[B_XOFF]) < 0 || read_motion(g, &b->bundle[B_YOFF]) < 0 ||
            read_dcs(g, &b->bundle[B_INTRA_DC], false) < 0 || read_dcs(g, &b->bundle[B_INTER_DC], true) < 0 ||
            read_runs(g, &b->bundle[B_RUN]) < 0)
            return false;
        uint8_t *dst = cur + 8 * (size_t)by * (size_t)stride, *prev = last + 8 * (size_t)by * (size_t)stride;
        for (int bx = 0; bx < bw; bx++, dst += 8, prev += 8) {
            int blk = value(b, B_TYPES);
            /* a 16 x 16 block on an odd row is the rest of the one above */
            if ((by & 1) && blk == SCALED) {
                bx++, dst += 8, prev += 8;
                continue;
            }
            int32_t dct[64];
            uint8_t ub[64];
            int16_t res[64];
            int cnt, idx[64], q;
            const uint8_t *ref;
            switch (blk) {
            case SKIP: copy8(dst, prev, stride); break;
            case SCALED:
                blk = value(b, B_SUBTYPES);
                switch (blk) {
                case RUN:
                    if (run_block(g, b, ub, 8, true) < 0) return false;
                    break;
                case INTRA:
                    memset(dct, 0, sizeof dct);
                    dct[0] = value(b, B_INTRA_DC);
                    q = read_dct(g, dct, &cnt, idx);
                    unquant(dct, intra_q[q], cnt, idx);
                    idct_put(ub, 8, dct);
                    break;
                case FILL: fill(dst, value(b, B_COLORS), stride, 16); break;
                case PATTERN: {
                    int col[2] = {value(b, B_COLORS), value(b, B_COLORS)};
                    for (int j = 0; j < 8; j++) {
                        int v = value(b, B_PATTERN);
                        for (int i = 0; i < 8; i++, v >>= 1) ub[i + 8 * j] = (uint8_t)col[v & 1];
                    }
                    break;
                }
                case RAW:
                    for (int j = 0; j < 64; j++) ub[j] = (uint8_t)value(b, B_COLORS);
                    break;
                default: return false;
                }
                if (blk != FILL) scale_block(ub, dst, stride);
                bx++, dst += 8, prev += 8;
                break;
            case MOTION:
            case RESIDUE:
            case INTER: {
                int xo = value(b, B_XOFF), yo = value(b, B_YOFF);
                ref = prev + xo + (ptrdiff_t)yo * stride;
                if (ref < ref_lo || ref > ref_hi) return false;
                copy8(dst, ref, stride);
                if (blk == RESIDUE) {
                    memset(res, 0, sizeof res);
                    read_residue(g, res, (int)get(g, 7));
                    for (int y = 0; y < 8; y++)
                        for (int x = 0; x < 8; x++) dst[y * stride + x] = (uint8_t)(dst[y * stride + x] + res[8 * y + x]);
                } else if (blk == INTER) {
                    memset(dct, 0, sizeof dct);
                    dct[0] = value(b, B_INTER_DC);
                    q = read_dct(g, dct, &cnt, idx);
                    unquant(dct, inter_q[q], cnt, idx);
                    idct_add(dst, stride, dct);
                }
                break;
            }
            case RUN:
                if (run_block(g, b, dst, stride, false) < 0) return false;
                break;
            case INTRA:
                memset(dct, 0, sizeof dct);
                dct[0] = value(b, B_INTRA_DC);
                q = read_dct(g, dct, &cnt, idx);
                unquant(dct, intra_q[q], cnt, idx);
                idct_put(dst, stride, dct);
                break;
            case FILL: fill(dst, value(b, B_COLORS), stride, 8); break;
            case PATTERN: {
                int col[2] = {value(b, B_COLORS), value(b, B_COLORS)};
                for (int i = 0; i < 8; i++) {
                    int v = value(b, B_PATTERN);
                    for (int j = 0; j < 8; j++, v >>= 1) dst[i * stride + j] = (uint8_t)col[v & 1];
                }
                break;
            }
            case RAW:
                for (int i = 0; i < 8; i++)
                    for (int j = 0; j < 8; j++) dst[i * stride + j] = (uint8_t)value(b, B_COLORS);
                break;
            default: return false;
            }
        }
    }
    if (g->pos & 31) g->pos += 32 - (g->pos & 31);   /* the next plane at a 32-bit boundary */
    return true;
}

/* ---- the audio ---- */

static float get_float(Bits *g)
{
    int power = (int)get(g, 5);
    float f = ldexpf((float)get(g, 23), power - 23);
    return get1(g) ? -f : f;
}

static void audio_init(Audio *a, int rate, bool stereo)
{
    int ch = stereo ? 2 : 1;
    int bits = rate < 22050 ? 9 : rate < 44100 ? 10 : 11;
    /* the channels interleaved into one stream at ch times the rate */
    rate *= ch;
    bits += ch == 2 ? 1 : 0;
    a->rate = rate / ch, a->channels = ch;
    a->frame_len = 1 << bits;
    a->overlap = a->frame_len / 16;
    a->block = a->frame_len - a->overlap;
    int half = (rate + 1) / 2;
    a->root = 2.0f / (sqrtf((float)a->frame_len) * 32768.0f);
    for (int i = 0; i < 96; i++) a->quant[i] = expf((float)i * 0.15289164787221953823f) * a->root;
    for (a->nbands = 1; a->nbands < 25; a->nbands++)
        if ((uint32_t)half <= crit[a->nbands - 1]) break;
    a->bands[0] = 2;
    for (int i = 1; i < a->nbands; i++) a->bands[i] = (int)(crit[i - 1] * (uint32_t)a->frame_len / (uint32_t)half) & ~1;
    a->bands[a->nbands] = a->frame_len;
    a->first = true;
}

/* the inverse real DFT of the packed spectrum (x[0] the DC, x[1] the Nyquist term, then the real and
   imaginary parts of each bin; the imaginary parts taken negated), halved, in place */
static void inverse_rdft(float *x, int n)
{
    static float re[4096], im[4096];
    int h = n / 2;
    /* the Hermitian spectrum, then a complex inverse FFT of n */
    re[0] = x[0], im[0] = 0, re[h] = x[1], im[h] = 0;
    for (int k = 1; k < h; k++) re[k] = x[2 * k], im[k] = -x[2 * k + 1], re[n - k] = x[2 * k], im[n - k] = x[2 * k + 1];
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j], re[j] = t;
            t = im[i]; im[i] = im[j], im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = 2 * 3.14159265358979323846 / len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < len / 2; k++) {
                float wr = (float)cos(ang * k), wi = (float)sin(ang * k);
                int a = i + k, c = i + k + len / 2;
                float tr = re[c] * wr - im[c] * wi, ti = re[c] * wi + im[c] * wr;
                re[c] = re[a] - tr, im[c] = im[a] - ti;
                re[a] += tr, im[a] += ti;
            }
    }
    for (int i = 0; i < n; i++) x[i] = re[i] * 0.5f;
}

static void queue_put(Bink *b, const float *s, int n)
{
    if (b->q_len + n > b->q_cap) {
        b->q_cap = (b->q_len + n) * 2;
        b->queue = realloc(b->queue, sizeof(float) * (size_t)b->q_cap);
    }
    memcpy(b->queue + b->q_len, s, sizeof(float) * (size_t)n);
    b->q_len += n;
}

/* one block: the two first coefficients, the bands' quantisers, the coefficients in runs of a width
   each, the transform, the overlap with the last block's tail */
static bool audio_block(Bits *g, Bink *b)
{
    Audio *a = &b->au;
    float *c = a->coeffs, q[26];
    c[0] = get_float(g) * a->root;
    c[1] = get_float(g) * a->root;
    for (int i = 0; i < a->nbands; i++) {
        int v = (int)get(g, 8);
        q[i] = a->quant[v < 95 ? v : 95];
    }
    int k = 0;
    float qq = q[0];
    for (int i = 2; i < a->frame_len;) {
        int j = get1(g) ? i + rle_len[get(g, 4)] * 8 : i + 8;
        if (j > a->frame_len) j = a->frame_len;
        int width = (int)get(g, 4);
        if (!width) {
            memset(c + i, 0, sizeof(float) * (size_t)(j - i));
            i = j;
            while (a->bands[k] < i) qq = q[k++];
        } else
            for (; i < j; i++) {
                if (a->bands[k] == i) qq = q[k++];
                int coeff = (int)get(g, width);
                c[i] = coeff ? (get1(g) ? -qq * (float)coeff : qq * (float)coeff) : 0.0f;
            }
        if (g->pos > g->end) return false;
    }
    inverse_rdft(c, a->frame_len);
    if (!a->first)
        for (int i = 0; i < a->overlap; i++) c[i] = (a->prev[i] * (float)(a->overlap - i) + c[i] * (float)i) / (float)a->overlap;
    memcpy(a->prev, c + a->frame_len - a->overlap, sizeof(float) * (size_t)a->overlap);
    a->first = false;
    queue_put(b, c, a->block);
    return true;
}

static void decode_audio(Bink *b, const uint8_t *p, uint32_t n)
{
    if (n < 4) return;
    Bits g = {p, 32, (size_t)n * 8};   /* the decoded size first */
    while (g.pos < g.end) {
        if (!audio_block(&g, b)) break;
        if (g.pos & 31) g.pos += 32 - (g.pos & 31);
    }
}

/* ---- the container ---- */

static bool read_at(Bink *b, uint64_t off, void *dst, size_t n) { return vfs_read_at(b->f, off, dst, n) == (int64_t)n; }

Bink *Bink_Open(const char *path, int track)
{
    if (!tables && !Bink_LoadTables(NULL, 0)) return NULL;
    VfsFile *f = vfs_open(path);
    if (!f) return NULL;
    Bink *b = calloc(1, sizeof *b);
    b->f = f;
    uint8_t h[44];
    if (!read_at(b, 0, h, sizeof h) || memcmp(h, "BIK", 3) || h[3] != 'i') goto fail;
    b->frames = rd32(h + 8), b->w = rd32(h + 20), b->h = rd32(h + 24);
    b->fps_num = rd32(h + 28), b->fps_den = rd32(h + 32), b->flags = rd32(h + 36), b->ntracks = rd32(h + 40);
    if (!b->frames || b->frames > 1000000 || !b->w || b->w > 4096 || !b->h || b->h > 4096 || (b->w | b->h) & 15 || b->ntracks > 256) goto fail;
    uint64_t off = 44 + 4ull * b->ntracks;   /* each track's largest decoded size skipped */
    b->track = (uint32_t)track;
    for (uint32_t t = 0; t < b->ntracks; t++) {
        uint8_t a[4];
        if (!read_at(b, off + 4 * t, a, 4)) goto fail;
        if (t == b->track && !(rd16(a + 2) & 0x1000)) {   /* the DCT kind (revision b) is not used here */
            audio_init(&b->au, rd16(a), (rd16(a + 2) & 0x2000) != 0);
            b->has_audio = true;
        }
    }
    off += 8ull * b->ntracks;   /* the rates and flags, the ids */
    b->index = malloc(4 * ((size_t)b->frames + 1));
    uint8_t *ix = malloc(4 * ((size_t)b->frames + 1));
    if (!read_at(b, off, ix, 4 * ((size_t)b->frames + 1))) { free(ix); goto fail; }
    for (uint32_t i = 0; i <= b->frames; i++) b->index[i] = rd32(ix + 4 * i);
    free(ix);
    for (int k = 0; k < 2; k++)
        for (int p = 0; p < 3; p++) b->plane[k][p] = calloc((size_t)plane_w(b, p) * (size_t)plane_h(b, p), 1);
    size_t blocks = ((b->w + 7) >> 3) * ((b->h + 7) >> 3);
    for (int i = 0; i < B_N; i++) {
        b->bundle[i].data = malloc(blocks * 64);
        b->bundle[i].end = b->bundle[i].data + blocks * 64;
    }
    b->frame = 1;
    return b;
fail:
    Bink_Close(b);
    return NULL;
}

void Bink_Close(Bink *b)
{
    if (!b) return;
    if (b->f) vfs_close(b->f);
    free(b->index);
    free(b->pkt);
    for (int k = 0; k < 2; k++)
        for (int p = 0; p < 3; p++) free(b->plane[k][p]);
    for (int i = 0; i < B_N; i++) free(b->bundle[i].data);
    free(b->queue);
    free(b);
}

int Bink_Width(const Bink *b) { return (int)b->w; }
int Bink_Height(const Bink *b) { return (int)b->h; }
uint32_t Bink_Frames(const Bink *b) { return b->frames; }
uint32_t Bink_Frame(const Bink *b) { return b->frame; }
void Bink_Rate(const Bink *b, uint32_t *num, uint32_t *den) { *num = b->fps_num, *den = b->fps_den; }
int Bink_AudioRate(const Bink *b) { return b->has_audio ? b->au.rate : 0; }
int Bink_AudioChannels(const Bink *b) { return b->has_audio ? b->au.channels : 0; }

bool Bink_DoFrame(Bink *b)
{
    if (b->frame < 1 || b->frame > b->frames) return false;
    uint32_t at = b->index[b->frame - 1] & ~1u, next = b->index[b->frame] & ~1u;
    if (next <= at) return false;
    size_t n = next - at;
    if (n + 16 > b->pkt_cap) {
        b->pkt_cap = n + 16;
        b->pkt = realloc(b->pkt, b->pkt_cap);
    }
    if (!read_at(b, at, b->pkt, n)) return false;
    memset(b->pkt + n, 0, 16);
    size_t pos = 0;
    for (uint32_t t = 0; t < b->ntracks; t++) {
        if (pos + 4 > n) return false;
        uint32_t len = rd32(b->pkt + pos);
        pos += 4;
        if (len > n - pos) return false;
        if (t == b->track && b->has_audio) decode_audio(b, b->pkt + pos, len);
        pos += len;
    }
    Bits g = {b->pkt + pos, 0, (n - pos) * 8};
    if (b->flags & 0x100000) return false;   /* an alpha plane: not in the game's movies */
    g.pos += 32;   /* revision i: a word before the planes */
    for (int pl = 0; pl < 3; pl++) {
        int p = pl == 0 ? 0 : pl ^ 3;   /* Y, then V, then U */
        if (!decode_plane(&g, b, p, pl != 0)) return false;
        if (g.pos >= g.end) break;
    }
    return true;
}

void Bink_NextFrame(Bink *b)
{
    b->cur ^= 1;
    b->frame++;
}

const uint8_t *Bink_Plane(const Bink *b, int p, int *stride)
{
    if (stride) *stride = plane_w(b, p);
    return b->plane[b->cur][p];
}

static uint8_t clip(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

void Bink_CopyRGBA(const Bink *b, uint8_t *dst, int pitch)
{
    const uint8_t *Y = b->plane[b->cur][0], *U = b->plane[b->cur][1], *V = b->plane[b->cur][2];
    int cw = (int)b->w / 2;
    for (uint32_t y = 0; y < b->h; y++) {
        uint8_t *o = dst + (size_t)y * (size_t)pitch;
        for (uint32_t x = 0; x < b->w; x++) {
            int c = 298 * (Y[y * b->w + x] - 16), u = U[(y / 2) * (uint32_t)cw + x / 2] - 128, v = V[(y / 2) * (uint32_t)cw + x / 2] - 128;
            o[4 * x] = clip((c + 409 * v + 128) >> 8);
            o[4 * x + 1] = clip((c - 100 * u - 208 * v + 128) >> 8);
            o[4 * x + 2] = clip((c + 516 * u + 128) >> 8);
            o[4 * x + 3] = 0xff;
        }
    }
}

static int take(Bink *b, int max_frames, float *f, int16_t *s)
{
    int ch = b->has_audio ? b->au.channels : 1, n = b->q_len / ch;
    if (n > max_frames) n = max_frames;
    for (int i = 0; i < n * ch; i++) {
        float v = b->queue[i];
        if (f) f[i] = v;
        if (s) {
            float x = v * 32768.0f;
            s[i] = (int16_t)(x > 32767.0f ? 32767 : x < -32768.0f ? -32768 : (int)lrintf(x));
        }
    }
    memmove(b->queue, b->queue + n * ch, sizeof(float) * (size_t)(b->q_len - n * ch));
    b->q_len -= n * ch;
    return n;
}

int Bink_TakeAudio(Bink *b, int16_t *dst, int max_frames) { return take(b, max_frames, NULL, dst); }
int Bink_TakeAudioFloat(Bink *b, float *dst, int max_frames) { return take(b, max_frames, dst, NULL); }
