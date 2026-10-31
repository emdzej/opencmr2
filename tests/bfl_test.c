/* Every data file of the CD through File_Load and the archive directory: the gzip and CMPR layers (the
   port's inflate against Python's zlib) and every member, as one summary hash over the lines
   "path crc size" and "path:member crc size". tools/bfl.py --summary <cd dir> prints the same; set
   OPENCMR2_BFL_SUMMARY=<its hash> to compare. Skips (exit 0) without data. */
#include "bfl.h"
#include "exe.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static char **paths;
static int npaths, cap;
static char cur[512];

static void walk(const char *dir);

static void add(const char *name, bool is_dir, void *user)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s", (const char *)user, name);
    if (is_dir) {
        walk(p);
        return;
    }
    if (npaths == cap) paths = realloc(paths, (size_t)(cap = cap ? cap * 2 : 1024) * sizeof *paths);
    paths[npaths++] = strdup(p);
}

static void walk(const char *dir)
{
    char d[512];
    snprintf(d, sizeof d, "%s", dir);
    vfs_list(d, add, d);
}

static int cmp(const void *a, const void *b) { return strcasecmp(*(char *const *)a, *(char *const *)b); }

static uint32_t total;
static int lines;

static void line(const char *s)
{
    /* crc32 over the concatenation, continued: the same as zlib.crc32(line, total) */
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    uint32_t c = total ^ 0xffffffffu;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) c = table[(c ^ *p) & 0xff] ^ (c >> 8);
    total = c ^ 0xffffffffu;
    lines++;
}

static const uint8_t *payload;

static void member(const char *name, uint32_t off, uint32_t size, void *user)
{
    char l[700];
    snprintf(l, sizeof l, "%s:%s %08x %u\n", cur, name, crc32(payload + off, size), size);
    line(l);
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Game/Bigfiles/common.bfl")) {
        printf("bfl_test: no game data, skipped\n");
        return 0;
    }
    static const char *const dirs[] = {"CountrySpecific", "FrontEnd", "Game", "SetupRep", "Sounds"};
    for (size_t i = 0; i < sizeof dirs / sizeof *dirs; i++) walk(dirs[i]);
    qsort(paths, (size_t)npaths, sizeof *paths, cmp);
    int archives = 0;
    for (int i = 0; i < npaths; i++) {
        /* only the medium's files: the cabinet's Game/DemoRuns etc. are not on the disc */
        snprintf(cur, sizeof cur, "%s", paths[i]);
        uint8_t *d = File_Load(paths[i], 1);
        CHECK(d != NULL);
        if (!d) { printf("  %s\n", paths[i]); continue; }
        uint32_t n = File_LastSize();
        char l[700];
        snprintf(l, sizeof l, "%s %08x %u\n", cur, crc32(d, n), n);
        line(l);
        payload = d;
        if (Bfl_Walk(d, n, member, NULL)) archives++;
        free(d);
    }
    printf("%d files, %d lines, summary %08x (%d archives)\n", npaths, lines, total, archives);
    const char *want = getenv("OPENCMR2_BFL_SUMMARY");
    if (want && *want) CHECK(strtoul(want, NULL, 16) == total);

    /* Lookup semantics (docs/formats/bfl.md): basename, any case, .dds before .tga. */
    BflSlot s = {0};
    CHECK(Bfl_Mount(&s, "Game\\BigFiles\\Common.bfl"));
    uint32_t sz = 0;
    uint8_t found = 0;
    CHECK(Bfl_FindFile(&s, "\\NEWIMAGE\\Effects\\RAIN.DDS", &found, &sz, false) && found && sz > 0);
    CHECK(!Bfl_FindFile(&s, "Effects/rain.dds", &found, &sz, false) && sz == 0);   /* the '/' quirk */
    CHECK(Bfl_FindTextureMember(&s, "x:\\any\\rain.tga", &sz) != NULL);
    Bfl_Unmount(&s);
    printf("bfl_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
