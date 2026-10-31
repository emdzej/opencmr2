/* File loading and archives (see bfl.h). */
#include "bfl.h"
#include "inflate.h"
#include "platform.h"
#include "vfs.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t last_size;   /* DAT_00663fe8 */

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

uint32_t File_LastSize(void) { return last_size; }

/* File_Load 0x4aa220. The original opens the file with zlib's gzopen (which reads a non-gzip file as it
   is), reads 8 bytes, and for "CMPR" + size allocates the size and reads the payload through the gzip
   stream. Otherwise it closes the stream and reads the whole file with ReadFile: a gzip file without the
   CMPR header comes back still compressed. A missing file shows the CD dialog (quiet = 0) and retries; the
   port logs it and returns NULL. */
uint8_t *File_Load(const char *path, int quiet)
{
    size_t n;
    uint8_t *raw = vfs_read_all(path, &n);
    if (!raw) {
        if (!quiet) {
            char m[300];
            snprintf(m, sizeof m, "File_Load: %s not found (the original asks for the CD here)", path);
            plat_log(m);
        }
        return NULL;
    }
    uint8_t *s = raw;
    size_t sn = n;
    uint8_t *unz = NULL;
    if (n >= 2 && raw[0] == 0x1f && raw[1] == 0x8b) {
        unz = gunzip(raw, n, &sn);
        if (!unz) {   /* gzread on a damaged stream: the original reads what it can; the port gives up */
            free(raw);
            return NULL;
        }
        s = unz;
    }
    if (sn >= 8 && !memcmp(s, "CMPR", 4)) {
        uint32_t size = rd32(s + 4);
        uint8_t *out = size == 0xffffffffu ? NULL : calloc(size ? size : 1, 1);
        if (out) {
            memcpy(out, s + 8, sn - 8 < size ? sn - 8 : size);   /* a short file leaves the rest zero */
            last_size = size;
        }
        free(unz);
        free(raw);
        return out;
    }
    free(unz);
    last_size = (uint32_t)n;
    return raw;
}

bool Bfl_LoadInto(const char *path, BflSlot *slot)
{
    slot->data = File_Load(path, 0);
    slot->size = slot->data ? File_LastSize() : 0;
    return slot->data != NULL;
}

bool Bfl_Mount(BflSlot *slot, const char *path)
{
    slot->loaded = 0;
    if (!Bfl_LoadInto(path, slot)) return false;
    slot->loaded = 1;
    return true;
}

void Bfl_Unmount(BflSlot *slot)
{
    free(slot->data);
    slot->data = NULL;
    slot->size = 0;
    slot->loaded = 0;
}

/* Bfl_Find 0x4a9c70. The original lower-cases the caller's string in place; the port lower-cases a copy.
   An entry's name is padded to a multiple of 4 bytes (a length that is already one has no padding). The
   walk trusts the directory; the port stops at the end of the payload as well. */
const uint8_t *Bfl_Find(const BflSlot *slot, const char *name, uint32_t *size)
{
    char want[260];
    size_t i = 0;
    for (; name[i] && i + 1 < sizeof want; i++) want[i] = (char)tolower((unsigned char)name[i]);
    want[i] = 0;
    if (!slot->data || slot->size < 4) return NULL;
    const uint8_t *base = slot->data, *end = base + slot->size - 4;
    uint32_t dir = rd32(end);
    if (dir >= slot->size - 4) return NULL;
    for (const uint8_t *p = base + dir; p + 12 <= end;) {
        if (size) *size = rd32(p);
        uint32_t off = rd32(p + 4), len = rd32(p + 8);
        if (len > (size_t)(end - p) - 12) return NULL;
        char ent[260];
        size_t l = len < sizeof ent - 1 ? len : sizeof ent - 1;
        memcpy(ent, p + 12, l);
        ent[l] = 0;
        if (!strcmp(ent, want)) return off < slot->size ? base + off : NULL;
        p += 12 + ((len & 3) ? len - (len & 3) + 4 : len);
    }
    return NULL;
}

/* Path_Basename 0x4a9da0: after the last '\'. A '/' is a quirk: when the path has one, the copy starts AT
   the last '/' (the slash is kept), so "a/b.tga" gives "/b.tga", which no member is called. The game's
   own paths use '\'. */
void Path_Basename(const char *path, char *dst, size_t cap)
{
    const char *s = strrchr(path, '/');
    if (!s) {
        const char *b = strrchr(path, '\\');
        s = b ? b + 1 : path;
    }
    snprintf(dst, cap, "%s", s);
}

const uint8_t *Bfl_FindFile(const BflSlot *slot, const char *path, uint8_t *found, uint32_t *size, bool skip)
{
    if (found) *found = 0;
    if (slot && slot->loaded && !skip) {
        char base[64];
        Path_Basename(path, base, sizeof base);
        const uint8_t *m = Bfl_Find(slot, base, size);
        if (m) {
            if (found) *found = 1;
            return m;
        }
    }
    if (size) *size = 0;
    return NULL;
}

/* FUN_004a9f50 0x4a9f50: the part after the last '\' with the extension cut at the last '.'. The original
   assumes both are there (a path without them would crash it); the port keeps the whole name then. */
static void tex_base(char *dst, size_t cap, const char *path)
{
    const char *b = strrchr(path, '\\');
    snprintf(dst, cap, "%s", b ? b + 1 : path);
    char *dot = strrchr(dst, '.');
    if (dot) *dot = 0;
}

const uint8_t *Bfl_FindTextureMember(const BflSlot *slot, const char *path, uint32_t *size)
{
    if (!slot || !slot->loaded) return NULL;
    char name[64];
    tex_base(name, sizeof name - 4, path);
    size_t l = strlen(name);
    memcpy(name + l, ".dds", 5);
    const uint8_t *m = Bfl_Find(slot, name, size);
    if (m) return m;
    memcpy(name + l, ".tga", 5);
    return Bfl_Find(slot, name, size);
}

bool Bfl_Walk(const uint8_t *data, uint32_t size, BflEntryFn fn, void *user)
{
    if (size < 4) return false;
    const uint8_t *end = data + size - 4;
    uint32_t dir = rd32(end);
    if (dir >= size - 4) return false;
    const uint8_t *p = data + dir;
    while (p < end) {
        if (end - p < 12) return false;
        uint32_t sz = rd32(p), off = rd32(p + 4), len = rd32(p + 8);
        if (!len || len > 260 || len > (size_t)(end - p) - 12 || off > dir || sz > dir - off) return false;
        char name[261];
        memcpy(name, p + 12, len);
        name[len] = 0;
        fn(name, off, sz, user);
        p += 12 + ((len + 3) & ~3u);
    }
    return p == end;
}
