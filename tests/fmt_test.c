/* Every texture, model and sound of the CD through the port's decoders, as three summary hashes over the
   lines tools/cmr2fmt.py --summary <cd dir> prints (see its summary()); set OPENCMR2_FMT_IMAGES,
   OPENCMR2_FMT_MODELS, OPENCMR2_FMT_SOUNDS to its hashes to compare. Also the text tables. Skips (exit 0)
   without data. */
#include "bfl.h"
#include "c3d.h"
#include "exe.h"
#include "font.h"
#include "image.h"
#include "sound.h"
#include "text.h"
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

static uint32_t crc_more(uint32_t total, const void *data, size_t n)
{
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    uint32_t c = total ^ 0xffffffffu;
    for (const uint8_t *p = data; n--; p++) c = table[(c ^ *p) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

enum { IMAGES, MODELS, SOUNDS };
static uint32_t total[3];
static int lines[3], ndds, ntga, nbad_img, nmodels, nbad_models, nadp;

static void line(int k, const char *s)
{
    total[k] = crc_more(total[k], s, strlen(s));
    lines[k]++;
}

static bool ends(const char *s, const char *ext)
{
    size_t a = strlen(s), b = strlen(ext);
    return a >= b && !strcasecmp(s + a - b, ext);
}

static void image(const char *name, const uint8_t *d, uint32_t n)
{
    int w = 0, h = 0;
    uint8_t *px = Image_Decode(d, n, &w, &h);
    char l[700];
    snprintf(l, sizeof l, "%s %d %d %08x\n", name, px ? w : 0, px ? h : 0, px ? crc32(px, (size_t)w * h * 4) : 0);
    line(IMAGES, l);
    if (!px) nbad_img++;
    else if (n >= 4 && !memcmp(d, "DDS ", 4)) ndds++;
    else ntga++;
    free(px);
}

/* The resolved triangles' positions: the vertices each polygon's three indices pick in its mesh. */
static void model(const char *name, const uint8_t *d, uint32_t n)
{
    uint8_t *copy = malloc(n);
    memcpy(copy, d, n);
    C3d m;
    char l[700];
    if (!C3d_Load(&m, copy, n, 0)) {
        snprintf(l, sizeof l, "%s bad\n", name);
        nbad_models++;
    } else {
        uint32_t crc = 0;
        for (int i = 0; i < m.nmesh1 + m.nmesh2; i++) {
            const uint8_t *me = C3d_Mesh(&m, i);
            const uint8_t *v = C3d_Ref(&m, me + 0x0c, m.verts), *p = C3d_Ref(&m, me + 0x24, m.polys);
            if (!v || !p) continue;
            uint32_t nv, np;
            memcpy(&nv, me + 0x10, 4);
            memcpy(&np, me + 0x28, 4);
            for (uint32_t k = 0; k < np; k++)
                for (int j = 0; j < 3; j++) {
                    uint16_t x = (uint16_t)(p[k * C3D_POLY + 0x40 + 2 * j] | p[k * C3D_POLY + 0x41 + 2 * j] << 8);
                    const uint8_t *vx = v + (size_t)x * C3D_VERTEX;
                    if (vx + 12 > m.data + m.vx) { crc = 0xbad; continue; }   /* past the vertex array */
                    crc = crc_more(crc, vx, 12);
                }
        }
        snprintf(l, sizeof l, "%s %u %u %u %u %u %u %08x\n", name, m.kind, m.nobj, m.nmesh1 + m.nmesh2,
                 m.npoly, m.nvert, m.ntex, crc);
        nmodels++;
    }
    line(MODELS, l);
    free(copy);
}

static void sound(const char *name, const uint8_t *d, uint32_t n)
{
    char l[700];
    if (ends(name, ".adp")) {
        AdpInfo a;
        if (!Adp_Header(d, n, n, &a)) {
            CHECK(!"adp header");
            return;
        }
        int16_t *pcm = malloc((size_t)a.samples_per_block * a.channels * 2);
        uint32_t crc = 0;
        uint64_t bytes = 0;
        for (uint64_t o = 0; o < a.data_size; o += (uint64_t)a.block_align) {
            uint64_t left = a.data_size - o;
            int f = Adpcm_DecodeBlock(&a, d + a.data_off + o, left < (uint64_t)a.block_align ? (size_t)left : (size_t)a.block_align, pcm);
            crc = crc_more(crc, pcm, (size_t)f * a.channels * 2);
            bytes += (uint64_t)f * a.channels * 2;
        }
        free(pcm);
        snprintf(l, sizeof l, "%s %d %d %d %llu %08x\n", name, a.channels, a.rate, 16, (unsigned long long)bytes, crc);
        line(SOUNDS, l);
        nadp++;
        return;
    }
    SndInfo s;
    if (!Snd_Header(d, n, &s)) return;
    snprintf(l, sizeof l, "%s %d %d %d %u %08x\n", name, s.channels, s.rate, s.bits, s.size, crc32(s.data, s.size));
    line(SOUNDS, l);
}

static const char *cur;
static const uint8_t *payload;
static uint32_t payload_size;
static int nfonts, nbad_fonts;

/* A font: its metrics load and every glyph lies inside its atlas, <name>.tga in the same archive. */
static void font(const char *member, const uint8_t *d, uint32_t n)
{
    Font f;
    bool ok = Font_Parse(&f, d, n);
    if (ok) CHECK(Font_Width(&f, "AV\nWWWW") == Font_Width(&f, "AV") && Font_Width(&f, "^A") == 0);
    BflSlot s = {(uint8_t *)payload, payload_size, 1};
    uint32_t sz = 0;
    const uint8_t *t = Bfl_FindTextureMember(&s, member, &sz);   /* <name>.dds, else <name>.tga */
    int aw = 0, ah = 0;
    uint8_t *px = t ? Image_Decode(t, sz, &aw, &ah) : NULL;
    ok = ok && px;
    free(px);
    for (int c = 0; ok && c < 256; c++) {
        Glyph g;
        /* com1024(C).bfl hel_20pt maps 0xcd to a record of 0xcd bytes (the font tool's uninitialised
           memory); the game would draw that rectangle as it is */
        if (Font_Glyph(&f, (unsigned char)c, &g) && (g.x + g.w > aw || g.y + g.h > ah) && g.x != 0xcdcd) ok = false;
    }
    if (ok) nfonts++;
    else {
        printf("  font %s:%s\n", cur, member);
        nbad_fonts++;
    }
}

static void item(const char *name, const uint8_t *d, uint32_t n)
{
    if (ends(name, ".dds") || ends(name, ".tga")) image(name, d, n);
    else if (n >= 4 && !memcmp(d, "PP_F", 4)) model(name, d, n);
    else if (ends(name, ".wav") || ends(name, ".adp")) sound(name, d, n);
}

static void member(const char *name, uint32_t off, uint32_t size, void *user)
{
    char full[700];
    snprintf(full, sizeof full, "%s:%s", cur, name);
    item(full, payload + off, size);
    if (ends(name, ".pcf")) font(name, payload + off, size);
}

static void check(int k, const char *env, const char *what)
{
    printf("%s: %d lines, summary %08x\n", what, lines[k], total[k]);
    const char *want = getenv(env);
    if (want && *want) CHECK(strtoul(want, NULL, 16) == total[k]);
}

static void text(void)
{
    BflSlot s = {0};
    CHECK(Bfl_Mount(&s, "CountrySpecific\\Europe\\englishtext.bfl"));
    uint32_t sz = 0;
    const uint8_t *f = Bfl_FindFile(&s, "fenglish.txt", NULL, &sz, false);
    CHECK(f != NULL);
    if (f) {
        size_t n = sz;
        Text_Build(1, 0x215, &f, &n);
        CHECK(Text_Get(0)[0] != 0);
        CHECK(strlen(Text_Get(0x214)) > 0);   /* the 533 non-empty lines fill the table */
        CHECK(Text_Get(0x215) == exe_str(0x520b00));   /* out of range: the exe's message */
        Text_Free();
    }
    Bfl_Unmount(&s);
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Game/Bigfiles/common.bfl")) {
        printf("fmt_test: no game data, skipped\n");
        return 0;
    }
    char err[200];
    exe_init(err, sizeof err);
    static const char *const dirs[] = {"CountrySpecific", "FrontEnd", "Game", "SetupRep", "Sounds"};
    for (size_t i = 0; i < sizeof dirs / sizeof *dirs; i++) walk(dirs[i]);
    qsort(paths, (size_t)npaths, sizeof *paths, cmp);
    for (int i = 0; i < npaths; i++) {
        uint8_t *d = File_Load(paths[i], 1);
        if (!d) continue;
        uint32_t n = File_LastSize();
        cur = paths[i];
        payload = d;
        payload_size = n;
        if (!Bfl_Walk(d, n, member, NULL)) item(paths[i], d, n);
        free(d);
    }
    printf("%d DDS + %d TGA decoded, %d rejected; %d models, %d rejected; %d music files\n", ndds, ntga,
           nbad_img, nmodels, nbad_models, nadp);
    CHECK(ndds == 7804 && ntga == 4003 && nbad_img == 0);
    CHECK(nmodels > 0 && nbad_models == 0);
    printf("%d fonts, %d rejected\n", nfonts, nbad_fonts);
    CHECK(nfonts == 72 && nbad_fonts == 0);
    check(IMAGES, "OPENCMR2_FMT_IMAGES", "images");
    check(MODELS, "OPENCMR2_FMT_MODELS", "models");
    check(SOUNDS, "OPENCMR2_FMT_SOUNDS", "sounds");
    text();
    printf("fmt_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
