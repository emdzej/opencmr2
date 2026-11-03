/* Textures (see tex.h). The surface's pixel format (the original converts into the mode's 16- or 32-bit
   surface formats) and the mip chains (options bits 0-1 with texture flag 0x10) are not reproduced
   yet; with the default options (all clear) the original takes neither path except the format
   conversion. */
#include "render/tex.h"
#include "exe.h"
#include "image.h"
#include "render/rstate.h"
#include "text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Tex *g_tex[TEX_MAX];
int g_tex_count;
uint32_t g_render_opts;

static const Tex *bound_tex;     /* DAT_0065fa38 */
static uint32_t bound_stage;     /* DAT_00520b28 (initialised data) */
static bool bound_init;
static uint32_t last_blend;      /* DAT_00520b18 */
static uint32_t binds;           /* DAT_0065fa24 */

/* The emulated card's texture formats (DAT_00663b38-0x663b50: DXT1 and DXT5 surfaces available). */
static const bool caps_dxt1 = true, caps_dxt5 = true;

uint32_t Tex_LastBlend(void) { return last_blend; }
void Tex_ForgetBlend(void) { last_blend = 0xffffffffu; }
uint32_t Tex_Binds(void) { return binds; }

void Tex_SetBlend(Tex *t, uint32_t type)
{
    if (t) t->blend = type;
}

static void tss(uint32_t s, uint32_t k, uint32_t v) { D3d_SetTextureStageState(s, k, v); }

/* The colour and alpha operations: arg1, arg2, op for each (D3DTA, D3DTOP). */
static void ops(uint32_t s, uint32_t c1, uint32_t c2, uint32_t cop, uint32_t a1, uint32_t a2)
{
    tss(s, D3DTSS_COLORARG1, c1);
    tss(s, D3DTSS_COLORARG2, c2);
    tss(s, D3DTSS_COLOROP, cop);
    tss(s, D3DTSS_ALPHAARG1, a1);
    tss(s, D3DTSS_ALPHAARG2, a2);
}

static uint32_t f2u(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

/* Tex_SetStageBlend 0x4a3e90. Types: 0xff and 0 modulate texture by diffuse with alpha modulated and
   alpha blending (0 also sets the mip filter from the options and MAXMIPLEVEL 0); 1 additive (one, one)
   without alpha; 2 alpha-additive (source alpha, one); 3 multiplying the frame buffer (zero, inverse
   source colour); 4 modulate with the diffuse alpha, the second coordinate set; 5 the current colour
   with the texture alpha, alpha-additive; 6 the texture alone; 7 add-signed of the inverse texture
   over the destination colour (destination colour, source colour), second set; 8 bump environment
   mapping with luminance; 9 modulate by the current colour, linear filtering, additive, second set; 10
   as 0xff with the texture's alpha replicated into the colour; 0xb the reflection map (camera-space
   reflection vector of set 1, three-component texture transform). Other values only record the type. */
void Tex_SetStageBlend(uint32_t s, const Tex *t)
{
    if (!t) {
        last_blend = 0xffffffffu;
        ops(s, D3DTA_TEXTURE, D3DTA_DIFFUSE, D3DTOP_DISABLE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
        tss(s, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        return;
    }
    int32_t b = (int32_t)t->blend;
    uint32_t tci = 0;
    if (b > 0xff) goto record;
    switch (b) {
    case 0xff:
    case 10:
        ops(s, b == 10 ? D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE : D3DTA_TEXTURE, D3DTA_DIFFUSE, D3DTOP_MODULATE, D3DTA_TEXTURE,
            D3DTA_DIFFUSE);
        tss(s, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        Render_SetBlend(5, 6);
        break;
    case 0:
        ops(s, D3DTA_TEXTURE, D3DTA_DIFFUSE, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
        tss(s, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        tss(s, D3DTSS_MIPFILTER, g_render_opts & 1 ? 2 : g_render_opts & 2 ? 3 : 1);
        tss(s, D3DTSS_MAXMIPLEVEL, 0);
        Render_SetBlend(5, 6);
        break;
    case 1:
        ops(s, D3DTA_TEXTURE, D3DTA_DIFFUSE, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
        tss(s, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        Render_SetBlend(2, 2);
        break;
    case 2:
        ops(s, D3DTA_TEXTURE, D3DTA_DIFFUSE, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
        tss(s, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        Render_SetBlend(5, 2);
        break;
    case 3:
        ops(s, D3DTA_TEXTURE, D3DTA_DIFFUSE, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
        tss(s, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        Render_SetBlend(1, 4);
        break;
    case 4:
        ops(s, D3DTA_TEXTURE, D3DTA_DIFFUSE, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
        tss(s, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        tci = 1;
        break;
    case 5:
        ops(s, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTOP_SELECTARG2, D3DTA_TEXTURE, D3DTA_CURRENT);
        tss(s, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        Render_SetBlend(5, 2);
        break;
    case 6:
        ops(s, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT);
        tss(s, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        break;
    case 7:
        ops(s, D3DTA_TEXTURE | D3DTA_COMPLEMENT, D3DTA_CURRENT, 8 /* ADDSIGNED */, D3DTA_TEXTURE, D3DTA_CURRENT);
        tss(s, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        Render_SetBlend(9, 3);
        tci = 1;
        break;
    case 8: {
        uint32_t m = exe_u32(0x520b24);   /* the bump matrix's diagonal */
        tss(s, D3DTSS_COLOROP, D3DTOP_BUMPENVMAPLUMINANCE);
        tss(s, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        tss(s, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        tss(s, D3DTSS_BUMPENVMAT00, m);
        tss(s, D3DTSS_BUMPENVMAT01, 0);
        tss(s, D3DTSS_BUMPENVMAT10, 0);
        tss(s, D3DTSS_BUMPENVMAT11, m);
        tss(s, D3DTSS_BUMPENVLSCALE, f2u(1.0f));
        tss(s, D3DTSS_BUMPENVLOFFSET, 0);
        tss(s, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        tss(s, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
        tss(s, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        break;
    }
    case 9:
        ops(s, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT);
        tss(s, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        tss(s, D3DTSS_MAGFILTER, 2);
        tss(s, D3DTSS_MINFILTER, 2);
        Render_SetBlend(2, 2);
        tci = 1;
        break;
    case 0xb:
        ops(s, D3DTA_TEXTURE, D3DTA_DIFFUSE, D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_DIFFUSE);
        tss(s, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        tss(s, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR | 1);
        tss(s, D3DTSS_TEXTURETRANSFORMFLAGS, 3);
        goto record;
    default:
        goto record;
    }
    Render_TexCoordIndex(s, tci);
record:
    last_blend = t->blend;
}

/* Tex_Bind 0x4a4850: skipped when both the texture and the stage equal the last bind (one pair, not one
   per stage). */
void Tex_Bind(uint32_t stage, Tex *t)
{
    if (!bound_init) {
        bound_stage = exe_u32(0x520b28);
        bound_init = true;
    }
    if (t == bound_tex && stage == bound_stage) return;
    bound_tex = t;
    bound_stage = stage;
    D3d_SetTexture(stage, t ? t->surface : NULL);
    Tex_SetStageBlend(stage, t);
    binds++;
}

/* The first free slot (records are zero-filled: GlobalAlloc with GMEM_ZEROINIT). */
static Tex *tex_alloc(void)
{
    for (int i = 0; i < TEX_MAX; i++)
        if (!g_tex[i]) {
            Tex *t = calloc(1, sizeof *t);
            if (!t) return NULL;
            g_tex[i] = t;
            t->index = (uint16_t)i;
            Tex_SetBlend(t, 0);
            g_tex_count++;
            return t;
        }
    return NULL;
}

/* The name tags: two letters before a four-letter extension and the like (name[len - back]). */
static bool tag(const char *name, size_t back, uint32_t va, size_t n)
{
    size_t l = strlen(name);
    return l >= back && !strncmp(name + l - back, exe_str(va), n);
}

/* 0x4a5880: a bump map from the decoded image: at each texel the height its red (0x4a5730), du and dv
   its differences to the texel right of it and below it (wrapping round) as they are, unsigned
   (written to the surface's signed bytes, so a difference over 127 turns negative), the luminance its
   alpha (0x4a57e0). The surface takes the card's bump format: the port's has the 8-bit one (du, dv,
   luminance, 0; the original's 16-bit branch packs them into its masks). */
static uint8_t *bump_map(const uint8_t *px, int w, int h)
{
    uint8_t *b = malloc((size_t)w * (size_t)h * 4);
    if (!b) return NULL;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int c = px[((size_t)y * w + x) * 4], r = px[((size_t)y * w + (x < w - 1 ? x + 1 : 0)) * 4],
                d = px[((size_t)(y < h - 1 ? y + 1 : 0) * w + x) * 4];
            uint8_t *o = b + ((size_t)y * w + x) * 4;
            o[0] = (uint8_t)abs(c - r);
            o[1] = (uint8_t)abs(c - d);
            o[2] = px[((size_t)y * w + x) * 4 + 3];
            o[3] = 0;
        }
    return b;
}

/* the surface for the decoded pixels: with flag 0x20 and option 0x10 the bump map made from them */
static void make_surface(Tex *t, uint8_t *px, int w, int h)
{
    if (!px) return;
    bool bump = (t->flags & 0x20) && (g_render_opts & 0x10);
    if (bump) {
        uint8_t *b = bump_map(px, w, h);
        free(px);
        if (!(px = b)) return;
    }
    t->surface = D3dTex_Create(w, h, px);
    if (t->surface) t->surface->bump = bump;
    t->width = (int16_t)w;
    t->height = (int16_t)h;
}

/* Tex_LoadTga 0x4a6710 (the record's part): 32 bits set flag 1; with option 0x10 a "BU" name is a bump
   map (flag 0x20, no alpha); the DXT flags are cleared. */
static Tex *load_tga(const uint8_t *data, size_t size, Tex *t)
{
    TgaInfo ti;
    if (!Tga_Header(data, size, &ti)) return NULL;   /* the record stays allocated, as in the original */
    if (ti.bpp == 4) t->flags |= 1;
    int w, h;
    uint8_t *px = Tga_Decode(data, size, &w, &h);
    if ((g_render_opts & 0x10) && tag(t->name, 6, 0x520b68, 2)) t->flags = (t->flags & ~1u) | 0x20;
    t->flags &= ~0x6000u;
    make_surface(t, px, w, h);
    return t;
}

/* DAT_00520b14: the cars' "BODF" textures keep their DXT flags (set by Car_Load, 0x4a3dc0: in a
   time trial, 0x407e50); the exe's initial value until then */
static int keep_bodf = -1;
void Tex_SetKeepBodf(bool on) { keep_bodf = on; }   /* 0x4a3dc0 */

/* Tex_LoadDds 0x4a6e30 (the record's part): DXT1 sets 0x4000, DXT5 0x2000 when the card has the format
   (a missing DXT5 marks alpha instead); "BR", "RU", "BODF" (unless 0x520b14), "DIGIT" and "REVCT" names
   are marked alpha without the DXT flags; with option 0x10 a "BU" name is a bump map. */
static Tex *load_dds(const uint8_t *data, size_t size, Tex *t)
{
    uint32_t fourcc = size >= 0x58 ? (uint32_t)(data[84] | data[85] << 8 | data[86] << 16 | (uint32_t)data[87] << 24) : 0;
    if (fourcc == 0x31545844u) t->flags |= 0x4000;
    else if (fourcc == 0x35545844u) t->flags |= 0x2000;
    int w, h;
    uint8_t *px = Dds_Decode(data, size, &w, &h);
    if (fourcc == 0x31545844u) t->flags = caps_dxt1 ? t->flags | 0x4000 : t->flags & ~0x4000u;
    else if (fourcc == 0x35545844u) t->flags = caps_dxt5 ? t->flags | 0x2000 : (t->flags & ~0x6000u) | 1;
    if (tag(t->name, 6, 0x520b70, 2)) t->flags = (t->flags & ~0x6000u) | 1;
    if (tag(t->name, 6, 0x520b6c, 2)) t->flags = (t->flags & ~0x6000u) | 1;
    if ((keep_bodf < 0 ? exe_u32(0x520b14) : (uint32_t)keep_bodf) == 0 && tag(t->name, 8, 0x51c680, 4)) t->flags = (t->flags & ~0x6000u) | 1;
    if (tag(t->name, 9, 0x51c9e4, 5)) t->flags = (t->flags & ~0x6000u) | 1;
    if (tag(t->name, 9, 0x51c9ec, 5)) t->flags = (t->flags & ~0x6000u) | 1;
    if ((g_render_opts & 0x10) && tag(t->name, 6, 0x520b68, 2)) t->flags = (t->flags & ~0x6001u) | 0x20;
    make_surface(t, px, w, h);
    return t;
}

static void put_ext(char *name, uint32_t va)
{
    size_t l = strlen(name);
    if (l >= 4) memcpy(name + l - 4, exe_str(va), 4);
}

Tex *Tex_CreateFromMemory(char *name, const uint8_t *data, uint32_t size, uint32_t flags)
{
    Tex *t = tex_alloc();
    if (!t) return NULL;
    bool dds = size >= 4 && !memcmp(data, "DDS ", 4);
    if (dds) put_ext(name, 0x520b3c);   /* ".DDS" */
    snprintf(t->name, sizeof t->name, "%s", name);
    t->flags |= flags | 0x1000;
    return dds ? load_dds(data, size, t) : load_tga(data, size, t);
}

/* 0x4a49c0: <path>.DDS from disk, else <path>.TGA (both with the CD dialog). (The bump loader
   0x4a6370 for a record given flag 0x20 with option 0x10, which differs from 0x4a5880 in taking the
   TGA's first byte, its blue, as the height and the height plus 0x80 past the last column and row, has
   no caller that gives the flag.) */
Tex *Tex_CreateFromFile(char *path, uint32_t flags)
{
    put_ext(path, 0x520b3c);
    uint8_t *d = File_Load(path, 0);
    bool dds = d != NULL;
    if (!d) {
        put_ext(path, 0x520b44);   /* ".TGA" */
        d = File_Load(path, 0);
    }
    uint32_t size = d ? File_LastSize() : 0;
    Tex *t = tex_alloc();
    if (!t) {
        free(d);
        return NULL;
    }
    t->archive = NULL;
    snprintf(t->name, sizeof t->name, "%s", path);
    t->flags |= flags;
    Tex *r = dds ? load_dds(d, size, t) : d ? load_tga(d, size, t) : NULL;
    free(d);   /* not from memory: the loaders free the file */
    return r;
}

int Tex_Clone(const Tex *t)
{
    for (int i = 0; i < TEX_MAX; i++)
        if (!g_tex[i]) {
            Tex *c = malloc(sizeof *c);
            if (!c) return -1;
            *c = *t;   /* the index and the surface included */
            g_tex[i] = c;
            g_tex_count++;
            return i;
        }
    return -1;
}

Tex *Bfl_FindTexture(const BflSlot *archive, char *path, uint8_t *found, bool skip, uint32_t flags)
{
    if (found) *found = 0;
    if (archive && archive->loaded && !skip) {
        uint32_t size = 0;
        const uint8_t *m = Bfl_FindTextureMember(archive, path, &size);
        if (m) {
            if (found) *found = 1;
            Tex *t = Tex_CreateFromMemory(path, m, size, flags);
            if (t) t->archive = archive;   /* the original writes through the result unchecked */
            return t;
        }
    }
    return Tex_CreateFromFile(path, flags);
}

/* FUN_004b9af0: "BR" or "RU" before the extension, or a "W" five letters before its end. */
static bool reflective(const char *name)
{
    return tag(name, 6, 0x520b70, 2) || tag(name, 6, 0x520b6c, 2) || tag(name, 8, 0x52111c, 1);
}

/* FUN_004b9b80 (cars): a texture already loaded under the same name is cloned instead of loaded again,
   unless the name is a reflective one. */
static Tex *reuse(const char *name)
{
    if (reflective(name)) return NULL;
    for (int i = 0; i < g_tex_count && i < TEX_MAX; i++)
        if (g_tex[i] && !strcmp(g_tex[i]->name, name)) {
            int c = Tex_Clone(g_tex[i]);
            return c >= 0 ? g_tex[c] : NULL;
        }
    return NULL;
}

/* Slot groups: name[12], u16 count at +0x0c, entries of 8 bytes from +0x10 with the path index (u16)
   at +4; the next group follows the entries. Each path is upper-cased in place, cut after its first
   backslash and prefixed with dir ("%s\%s.tga"). Flags: kind 10 none, other non-car kinds 0x90; cars
   0x40 for a reflective name, else 0x100, except that a "BU", "B2" or "B3" name leaves the variable as
   it was: the previous texture's flags, or for the first the group count (the original reuses its
   argument). */
void C3d_Textures(const uint8_t *g, uint8_t *paths, uint32_t ngroups, uint32_t kind, const BflSlot *archive,
                  const char *dir)
{
    uint32_t flags = ngroups;
    for (uint32_t n = ngroups; n; n--) {
        uint16_t count = (uint16_t)(g[0x0c] | g[0x0d] << 8);
        uint32_t i = 0;
        for (; i < count; i++) {
            uint16_t pi = (uint16_t)(g[0x14 + 8 * i] | g[0x15 + 8 * i] << 8);
            char *p = (char *)paths + (size_t)pi * 0x104;
            p[0x103] = 0;
            Text_ToFontCase(p);
            const char *after = strchr(p, '\\');
            char name[260];
            snprintf(name, sizeof name, exe_str(0x5168dc), dir, after ? after + 1 : p);
            if (kind == 6) {
                if (reuse(name)) continue;
                if (!tag(name, 6, 0x520b68, 2) && !tag(name, 6, 0x521118, 2) && !tag(name, 6, 0x521114, 2))
                    flags = reflective(name) ? 0x40 : 0x100;
            } else {
                flags = kind != 10 ? 0x90 : 0;
            }
            Bfl_FindTexture(archive, name, NULL, false, flags);
        }
        g += 0x10 + (size_t)i * 8;
    }
}

enum { COPIES = 64 };
static D3dTexture *copies[COPIES];   /* the records' surfaces at 0x65af34 + 0x130 n (counted in
                                        DAT_0065fa30, which nothing reads) */

void Tex_KeyAlpha(Tex *t, uint8_t key1, uint8_t val1, uint8_t key2, uint8_t val2, uint8_t key3, uint8_t val3, int n)
{
    if (!t || !t->surface || n < 0 || n >= COPIES) return;
    D3dTexture *s = t->surface;
    size_t bytes = (size_t)s->width * (size_t)s->height * 4;
    if (!copies[n]) {
        /* Tex_CreateSurface 0x4a7410 (format 9) at the record's size, then the blit */
        uint8_t *px = malloc(bytes);
        if (!px) return;
        memcpy(px, s->rgba, bytes);
        copies[n] = D3dTex_Create(s->width, s->height, px);
    }
    for (size_t i = 0; i < bytes; i += 4) {
        uint8_t *a = s->rgba + i + 3;
        if (*a == key1) *a = val1;
        if (*a == key2) *a = val2;
        if (*a == key3) *a = val3;
    }
    s->dirty = true;
}

void Tex_RestoreCopy(Tex *t, int n)
{
    if (!t || !t->surface || n < 0 || n >= COPIES || !copies[n]) return;
    D3dTexture *d = t->surface, *c = copies[n];
    for (int y = 0; y < d->height; y++)
        for (int x = 0; x < d->width; x++)
            memcpy(d->rgba + ((size_t)y * d->width + x) * 4,
                   c->rgba + ((size_t)(y * c->height / d->height) * c->width + (size_t)(x * c->width / d->width)) * 4, 4);
    d->dirty = true;
}

void Tex_FreeCopies(void)
{
    for (int i = 0; i < COPIES; i++) {
        D3dTex_Free(copies[i]);
        copies[i] = NULL;
    }
}

/* 0x4a5da0: the record made again from its archive under its (changed) name: its surface let go (a
   clone's shared one kept for the others), the member found and loaded by its magic; nothing more
   when the archive does not have it */
void Tex_Reload(Tex *t)
{
    for (int i = 0; i < TEX_MAX; i++) {
        if (g_tex[i] != t || !t) continue;
        if (t->surface) {
            bool shared = false;
            for (int j = 0; j < TEX_MAX && !shared; j++) shared = j != i && g_tex[j] && g_tex[j]->surface == t->surface;
            if (!shared) D3dTex_Free(t->surface);
            if (bound_tex == t) bound_tex = NULL;
            t->surface = NULL;
        }
        uint32_t size = 0;
        const uint8_t *m = t->archive && t->archive->loaded ? Bfl_FindTextureMember(t->archive, t->name, &size) : NULL;
        if (!m) return;   /* (the original formats a "couldn't find texture" message) */
        if (size >= 4 && !memcmp(m, "DDS ", 4)) load_dds(m, size, t);
        else load_tga(m, size, t);
    }
}

/* 0x4a4c40: after a new device, every record that is no copy made again from its archive under its
   name, .DDS first then .TGA (0x520b3c, 0x520b44), so the front end's take the resolution's archive.
   (The original then restores the vertex buffers, 0x4a76d0; the port's survive the device.) The port's
   surfaces survive it too: a record whose archive no longer has it keeps its surface, where the
   original's would be lost. */
void Tex_ReloadAll(void)
{
    for (int i = 0; i < g_tex_count && i < TEX_MAX; i++) {
        Tex *t = g_tex[i];
        if (!t || t->index != i || !t->archive || !t->archive->loaded) continue;
        uint32_t size = 0;
        put_ext(t->name, 0x520b3c);
        if (!Bfl_FindTextureMember(t->archive, t->name, &size)) put_ext(t->name, 0x520b44);
        if (Bfl_FindTextureMember(t->archive, t->name, &size)) Tex_Reload(t);
    }
}

void Tex_FreeAll(void)
{
    Tex_FreeCopies();
    for (int i = 0; i < TEX_MAX; i++) {
        Tex *t = g_tex[i];
        if (!t) continue;
        /* clones share the surface: free it once, with the last record that has it */
        bool shared = false;
        for (int j = i + 1; j < TEX_MAX && !shared; j++) shared = g_tex[j] && g_tex[j]->surface == t->surface;
        if (!shared && t->surface) D3dTex_Free(t->surface);
        free(t);
        g_tex[i] = NULL;
    }
    g_tex_count = 0;
    bound_tex = NULL;
}
