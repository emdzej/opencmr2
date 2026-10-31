/* InstallShield 5/6 cabinet layer (see vfs_cab.h).

   data1.hdr: the common header ("ISc(", version, volume info, descriptor offset at +0x0c), then the cabinet
   descriptor. In the descriptor: +0x0c file-table offset (from the descriptor), +0x1c directory count,
   +0x28 file count, +0x2c offset of the file descriptors inside the file table. The file table starts with
   one u32 per directory (name offset from the table start); file descriptors are 0x57 bytes:
   +0x00 flags (1 split, 2 obfuscated, 4 compressed, 8 invalid), +0x02 size, +0x0a compressed size,
   +0x12 data offset (all u64), +0x3a name offset (u32), +0x3e directory index (u16), +0x55 volume (u16).

   data<N>.cab: the same common header, then the volume header: +0x1c first file index, +0x20 last file
   index, +0x24 first file's data offset, +0x34 its compressed size (the part in this volume), +0x3c last
   file's offset, +0x4c its compressed size (u64 each). A file that doesn't fit continues at the first
   file offset of the next volume. Compressed data is a sequence of chunks: u16 length + raw deflate,
   each chunk independent. */
#include "vfs_cab.h"
#include "inflate.h"
#include "vfs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { F_SPLIT = 1, F_OBFUSCATED = 2, F_COMPRESSED = 4, F_INVALID = 8, FD_SIZE = 0x57 };

typedef struct {
    char path[160];   /* installed path, '/' separated */
    uint64_t size, csize, off;
    uint16_t flags, volume;
    uint32_t index;
} CabFile;

typedef struct {
    VfsBackend base;  /* the medium underneath */
    CabFile *f;
    int n;
} Cab;

typedef struct {
    bool own;         /* a cabinet file: data is ours; else h is the base's handle */
    void *h;
    uint8_t *data;
    uint64_t size;
} CabHandle;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const uint8_t *p) { return rd32(p) | (uint64_t)rd32(p + 4) << 32; }

/* Whole file from the base mount. */
static uint8_t *base_read(const VfsBackend *b, const char *rel, size_t *size)
{
    uint64_t n;
    void *h = b->open(b->ctx, rel, &n);
    if (!h) return NULL;
    uint8_t *buf = malloc(n ? (size_t)n : 1);
    size_t got = 0;
    while (buf && got < n) {
        int64_t r = b->read_at(b->ctx, h, got, buf + got, (size_t)(n - got));
        if (r <= 0) { free(buf); buf = NULL; break; }
        got += (size_t)r;
    }
    b->close(b->ctx, h);
    if (buf && size) *size = (size_t)n;
    return buf;
}

static int64_t base_read_at(const VfsBackend *b, const char *rel, uint64_t off, void *dst, size_t len)
{
    uint64_t n;
    void *h = b->open(b->ctx, rel, &n);
    if (!h) return -1;
    int64_t r = off < n ? b->read_at(b->ctx, h, off, dst, len > n - off ? (size_t)(n - off) : len) : 0;
    b->close(b->ctx, h);
    return r;
}

/* The compressed bytes of a file, gathered across volumes. */
static uint8_t *gather(const Cab *c, const CabFile *f)
{
    uint8_t *raw = malloc(f->csize ? (size_t)f->csize : 1);
    if (!raw) return NULL;
    uint64_t done = 0, off = f->off, avail = f->csize;
    int vol = f->volume;
    char name[32];
    uint8_t vh[0x54];
    snprintf(name, sizeof name, "data%d.cab", vol);
    if (base_read_at(&c->base, name, 0, vh, sizeof vh) != (int64_t)sizeof vh) goto fail;
    if (rd32(vh + 0x20) == f->index && rd64(vh + 0x4c) && rd64(vh + 0x4c) < f->csize) avail = rd64(vh + 0x4c);
    while (done < f->csize) {
        uint64_t take = avail < f->csize - done ? avail : f->csize - done;
        if (!take || base_read_at(&c->base, name, off, raw + done, (size_t)take) != (int64_t)take) goto fail;
        done += take;
        if (done < f->csize) {
            snprintf(name, sizeof name, "data%d.cab", ++vol);
            if (base_read_at(&c->base, name, 0, vh, sizeof vh) != (int64_t)sizeof vh) goto fail;
            off = rd64(vh + 0x24);
            avail = rd64(vh + 0x34);
        }
    }
    return raw;
fail:
    free(raw);
    return NULL;
}

static uint8_t *extract(const Cab *c, const CabFile *f)
{
    if (f->flags & (F_INVALID | F_OBFUSCATED)) return NULL;
    uint8_t *raw = gather(c, f);
    if (!raw || !(f->flags & F_COMPRESSED)) return raw;
    uint8_t *out = malloc(f->size ? (size_t)f->size : 1);
    size_t p = 0, o = 0;
    while (out && p + 2 <= f->csize) {
        size_t n = rd16(raw + p);
        p += 2;
        if (n > f->csize - p) break;
        int64_t k = inflate_raw(raw + p, n, out + o, (size_t)(f->size - o), NULL);
        if (k < 0) break;
        o += (size_t)k;
        p += n;
    }
    free(raw);
    if (out && o != f->size) {
        free(out);
        out = NULL;
    }
    return out;
}

static const CabFile *find(const Cab *c, const char *rel)
{
    for (int i = 0; i < c->n; i++)   /* first match: later duplicates are stale descriptors */
        if (!strcasecmp(c->f[i].path, rel)) return &c->f[i];
    return NULL;
}

static void *cab_open(void *ctx, const char *rel, uint64_t *size)
{
    Cab *c = ctx;
    CabHandle *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    h->h = c->base.open(c->base.ctx, rel, size);
    if (h->h) return h;
    const CabFile *f = find(c, rel);
    if (f && (h->data = extract(c, f))) {
        h->own = true;
        h->size = *size = f->size;
        return h;
    }
    free(h);
    return NULL;
}

static int64_t cab_read_at(void *ctx, void *file, uint64_t off, void *dst, size_t len)
{
    Cab *c = ctx;
    CabHandle *h = file;
    if (!h->own) return c->base.read_at(c->base.ctx, h->h, off, dst, len);
    if (off >= h->size) return 0;
    if (len > h->size - off) len = (size_t)(h->size - off);
    memcpy(dst, h->data + off, len);
    return (int64_t)len;
}

static void cab_close(void *ctx, void *file)
{
    Cab *c = ctx;
    CabHandle *h = file;
    if (h->own) free(h->data);
    else c->base.close(c->base.ctx, h->h);
    free(h);
}

typedef struct { VfsListFn fn; void *user; char seen[64][64]; int nseen; } ListCtx;

static bool seen(ListCtx *l, const char *name)
{
    for (int i = 0; i < l->nseen; i++)
        if (!strcasecmp(l->seen[i], name)) return true;
    if (l->nseen < 64) snprintf(l->seen[l->nseen++], sizeof l->seen[0], "%s", name);
    return false;
}

static void list_base(const char *name, bool is_dir, void *user)
{
    ListCtx *l = user;
    seen(l, name);
    l->fn(name, is_dir, l->user);
}

/* The medium's entries, then the cabinet's under dir that the medium doesn't have. */
static bool cab_list(void *ctx, const char *dir, VfsListFn fn, void *user)
{
    Cab *c = ctx;
    ListCtx *l = calloc(1, sizeof *l);
    if (!l) return false;
    l->fn = fn;
    l->user = user;
    bool any = c->base.list && c->base.list(c->base.ctx, dir, list_base, l);
    size_t dl = strlen(dir);
    for (int i = 0; i < c->n; i++) {
        const char *p = c->f[i].path;
        if (dl && (strncasecmp(p, dir, dl) || p[dl] != '/')) continue;
        const char *rest = p + (dl ? dl + 1 : 0);
        const char *slash = strchr(rest, '/');
        char e[64];
        size_t el = slash ? (size_t)(slash - rest) : strlen(rest);
        if (!el || el >= sizeof e) continue;
        memcpy(e, rest, el);
        e[el] = 0;
        any = true;
        if (!seen(l, e)) fn(e, slash != NULL, user);
    }
    free(l);
    return any;
}

static void cab_unmount(void *ctx)
{
    Cab *c = ctx;
    if (c->base.unmount) c->base.unmount(c->base.ctx);
    free(c->f);
    free(c);
}

/* Reads the file table of data1.hdr. */
static bool load_table(Cab *c)
{
    size_t n;
    uint8_t *h = base_read(&c->base, "data1.hdr", &n);
    if (!h) return false;
    bool ok = false;
    if (n < 0x200 || memcmp(h, "ISc(", 4)) goto done;
    uint32_t cd = rd32(h + 12);
    if ((uint64_t)cd + 0x30 > n) goto done;
    uint64_t ft = (uint64_t)cd + rd32(h + cd + 0x0c);
    uint32_t ndirs = rd32(h + cd + 0x1c), nfiles = rd32(h + cd + 0x28), ft2 = rd32(h + cd + 0x2c);
    if (ft + 4ull * ndirs > n || ft + ft2 + (uint64_t)nfiles * FD_SIZE > n) goto done;
    c->f = calloc(nfiles ? nfiles : 1, sizeof *c->f);
    if (!c->f) goto done;
    for (uint32_t i = 0; i < nfiles; i++) {
        const uint8_t *p = h + ft + ft2 + (uint64_t)i * FD_SIZE;
        CabFile *f = &c->f[c->n];
        f->flags = rd16(p);
        f->size = rd64(p + 0x02);
        f->csize = rd64(p + 0x0a);
        f->off = rd64(p + 0x12);
        f->volume = rd16(p + 0x55);
        f->index = i;
        uint64_t name = ft + rd32(p + 0x3a);
        uint16_t d = rd16(p + 0x3e);
        if (f->flags & F_INVALID || !f->off || d >= ndirs || name >= n) continue;
        uint64_t dname = ft + rd32(h + ft + 4ull * d);
        if (dname >= n || !memchr(h + name, 0, n - name) || !memchr(h + dname, 0, n - dname)) continue;
        const char *dn = (const char *)h + dname, *fn = (const char *)h + name;
        if (!*fn) continue;
        int k = snprintf(f->path, sizeof f->path, "%s%s%s", dn, *dn ? "/" : "", fn);
        if (k < 0 || k >= (int)sizeof f->path) continue;
        for (char *s = f->path; *s; s++)
            if (*s == '\\') *s = '/';
        c->n++;
    }
    ok = c->n > 0;
done:
    free(h);
    return ok;
}

bool vfs_cab_layer(void)
{
    if (!vfs_exists("data1.hdr") || vfs_exists("CMR2.exe")) return true;
    Cab *c = calloc(1, sizeof *c);
    char desc[512], d[600];
    if (!c || !vfs_detach(&c->base, desc, sizeof desc)) { free(c); return false; }
    if (!load_table(c)) {
        vfs_mount(&c->base, desc);   /* hand the medium back */
        free(c->f);
        free(c);
        return false;
    }
    snprintf(d, sizeof d, "%s + data1.hdr", desc);
    VfsBackend b = {c, cab_open, cab_read_at, cab_close, cab_list, cab_unmount};
    vfs_mount(&b, d);
    return true;
}
