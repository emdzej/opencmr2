/* The front end's loads at start-up (see fe_res.h). */
#include "fe/fe_res.h"
#include "fe/fe.h"
#include "fe/menu.h"
#include "audio/snd.h"
#include "exe.h"
#include "font.h"
#include "game/mem.h"
#include "game/paths.h"
#include "pending.h"
#include "render/d3d7.h"
#include "text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FeTextures g_fe;
static int nlang;                /* DAT_0081a754 */
static int snd_base;             /* DAT_00819888 */
static char *credits;            /* DAT_0081a730 */
static FeCredit *credit_lines;   /* DAT_0081a734 */
static int ncredits;             /* DAT_0081a728 */

void Fe_LanguageCount(void)
{
    switch (gm32(0x52ea54)) {
    case 0: nlang = 5; break;
    case 1: nlang = 3; break;
    case 2:
    case 3: nlang = 1; break;
    }
}

int Fe_Languages(void) { return nlang; }

/* the eight rallies' names for the banners (setuprep) and the small flags: Finland, Greece, France,
   Sweden, Australia, Kenya, Italy, the UK (the strings the original copies onto its stack) */
static const uint32_t banner_names[8] = {0x525090, 0x52508c, 0x525078, 0x52506c, 0x525068, 0x52505c, 0x525058, 0x519264};
static const uint32_t flag_names[8] = {0x525094, 0x52509c, 0x525084, 0x52507c, 0x525068, 0x525070, 0x525060, 0x519264};

void Fe_LoadTextures(void)
{
    BflSlot *r = Fe_Res();
    g_fe.arrow_a = Fe_Texture(r, 0x525034, Path_Root());
    g_fe.arrow_d = Fe_Texture(r, 0x525010, Path_Root());
    g_fe.lg_matrix = Fe_Texture(r, 0x524fec, Path_Root());
    g_fe.sm_matrix = Fe_Texture(r, 0x524fc8, Path_Root());
    char p[260];
    for (int i = 0; i < 8; i++) {
        Path_Format(p, sizeof p, 0x524fa0, Path_Root(), exe_str(banner_names[i]));
        g_fe.banner[i] = Bfl_FindTexture(r, p, NULL, false, 0);
        Path_Format(p, sizeof p, 0x524f78, Path_Root(), exe_str(flag_names[i]));
        g_fe.flag[i] = Bfl_FindTexture(r, p, NULL, false, 0);
    }
    g_fe.gold = Fe_Texture(r, 0x524f58, Path_Root());
    g_fe.silver = Fe_Texture(r, 0x524f34, Path_Root());
    g_fe.bronze = Fe_Texture(r, 0x524f10, Path_Root());
    Fe_LoadCars();   /* 0x4d2590 */
}

/* 0x4f3b50: Sounds\menu\move, select, back, error, toggle (the names 9 bytes apart from 0x52544c), from
   the front end's Common.bfl; the first of them is sound 0 */
bool Fe_LoadSounds(void)
{
    bool ok = true;
    snd_base = 0;
    for (uint32_t a = 0x52544c; a < 0x525479; a += 9) {
        char p[260];
        Path_Format(p, sizeof p, 0x51ea44, Path_Root(), exe_str(a));
        if (!Snd_LoadNamed(p, Fe_Common())) ok = false;
    }
    return ok;
}

/* 0x4f3bb0: the effects volume (0x52afb8 bits 7-13, percent) as the menus' volume, the five sounds */
void Fe_MenuSounds(void)
{
    gw32(0x81988c, 0xffffffffu);
    int32_t v = (int32_t)((gm32(0x52afb8) >> 7 & 0x7f) << 16) / 100;
    Menu_SetVolume(v / 4);
    Menu_SetSounds(snd_base, snd_base + 1, snd_base + 2, snd_base + 3, snd_base + 4);
    Menu_SoundsOn(true);
}

/* 0x4f4d20: four slots, general\hel_12pt, hel_15pt, hel_36pt and dot (20 bytes apart from 0x525b9c),
   from Res640.bfl */
bool Fe_LoadFonts(void)
{
    Font_InitSlots(4);
    for (int i = 0; i < 4; i++) {
        const char *n = exe_str(0x525b9c + 0x14u * (uint32_t)i);
        const char *b = strrchr(n, '\\');
        char dir[32] = "";
        if (b && (size_t)(b - n) < sizeof dir) memcpy(dir, n, (size_t)(b - n)), dir[b - n] = 0;
        Font_LoadSlot(dir, b ? b + 1 : n, Fe_Res(), i);
    }
    return true;
}

/* 0x4f4b90: f<language>.txt from each language's text archive, one table of 533 strings each */
bool Fe_LoadText(void)
{
    static const uint32_t europe[5] = {0x525b90, 0x525b88, 0x525b7c, 0x525b70, 0x525b68};
    static const uint32_t america[3] = {0x525b60, 0x525b88, 0x525b7c};
    static const uint32_t japan[1] = {0x525b90}, poland[1] = {0x525b58};
    const uint32_t *names = europe;
    switch (gm32(0x52ea54)) {
    case 1: names = america; break;
    case 2: names = japan; break;
    case 3: names = poland; break;
    }
    const uint8_t *files[5] = {0};
    size_t sizes[5] = {0};
    for (int i = 0; i < nlang; i++) {
        char p[64];
        Path_Format(p, sizeof p, 0x51e948, exe_str(names[i]));
        uint32_t sz = 0;
        files[i] = Bfl_FindFile(Fe_Text(i), p, NULL, &sz, false);
        sizes[i] = sz;
    }
    if (!files[0]) return false;
    Text_Build(nlang, 0x215, files, sizes);
    return true;
}

/* 0x4f4910: CountrySpecific\<region>\credits_<language>.txt (loose on the CD): pairs of quoted strings,
   cut in place */
void Fe_LoadCredits(void)
{
    static const uint32_t europe[5] = {0x525b48, 0x525b38, 0x525b28, 0x525b18, 0x525b08};
    static const uint32_t america[3] = {0x525afc, 0x525ae8, 0x525ad4};
    static const uint32_t japan[1] = {0x525ac0}, poland[1] = {0x525ab0};
    const uint32_t *names = europe;
    switch (gm32(0x52ea54)) {
    case 1: names = america; break;
    case 2: names = japan; break;
    case 3: names = poland; break;
    }
    uint32_t lang = gm32(0x52afb4) & 7;
    char p[260];
    Path_Format(p, sizeof p, 0x525aa4, Path_Country(), exe_str(exe_u32(0x516124 + 4 * gm32(0x52ea54))),
                exe_str(names[lang]));
    free(credits);
    free(credit_lines);
    credits = (char *)File_Load(p, 1);
    credit_lines = NULL;
    ncredits = 0;
    if (!credits) return;
    uint32_t size = File_LastSize();
    int quotes = 0;
    for (uint32_t i = 0; i < size; i++) quotes += credits[i] == '"';
    ncredits = quotes / 4;
    credit_lines = calloc((size_t)ncredits + 1, sizeof *credit_lines);
    if (!credit_lines) {
        ncredits = 0;
        return;
    }
    char *s = credits, *end = credits + size;
    for (int i = 0; i < ncredits; i++) {
        char *q = memchr(s, '"', (size_t)(end - s));
        if (!q) break;
        credit_lines[i].a = ++q;
        q = memchr(q, '"', (size_t)(end - q));
        if (!q) break;
        *q = 0;
        s = q + 1;
        q = memchr(s, '"', (size_t)(end - s));
        if (!q) break;
        credit_lines[i].b = ++q;
        q = memchr(q, '"', (size_t)(end - q));
        if (!q) break;
        *q = 0;
        s = q + 1;
    }
}

int Fe_Credits(const FeCredit **lines)
{
    *lines = credit_lines;
    return ncredits;
}

/* 0x4ef150: the twelve scroll panels (0x8191a4, 0x78 bytes apart): +0 the gap, 24 at 640, +0x10 0 */
void Fe_ResetPanels(void)
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    for (uint32_t a = 0x8191a4; a < 0x819744; a += 0x78) {
        gw32(a + 0x10, 0);
        gw32(a, (uint32_t)(w * 0x18 / 0x280));
    }
}

/* 0x4f3f60: frontend\Textures\dot00-dot03 */
void Fe_LoadDots(void)
{
    char p[260];
    for (int i = 0; i < 4; i++) {
        Path_Format(p, sizeof p, 0x525a84, Path_Root(), i);
        g_fe.dot[i] = Bfl_FindTexture(Fe_Res(), p, NULL, false, 0);
    }
}


/* 0x4d2590: each car's front and back pictures (frontend\Textures\cars\<name>F01, B01) and the three
   liveries; the alternate Focus and Lancer cars and the Integrale's share their first car's. The
   names are the strings the original lists on its stack (0x524c64 ...; "" for the shared ones). */
void Fe_LoadCars(void)
{
    static const uint32_t names[22] = {0x524c64, 0x524c60, 0, 0, 0x524c58, 0, 0, 0, 0x524c50, 0x524c4c, 0x516ca4,
                                       0x524c44, 0x524c3c, 0, 0, 0x524c34, 0x524c2c, 0x524c28, 0x524c20, 0x516c80,
                                       0x524c18, 0x524c10};
    char p[260];
    for (int i = 0; i < 22; i++) {
        if (!names[i]) {
            int from = i <= 3 ? 1 : i <= 7 ? 4 : 12;
            g_fe.car_front[i] = g_fe.car_front[from];
            g_fe.car_back[i] = g_fe.car_back[from];
            continue;
        }
        Path_Format(p, sizeof p, 0x524bec, Path_Root(), exe_str(names[i]));
        g_fe.car_front[i] = Bfl_FindTexture(Fe_Res(), p, NULL, false, 0);
        Path_Format(p, sizeof p, 0x524bc8, Path_Root(), exe_str(names[i]));
        g_fe.car_back[i] = Bfl_FindTexture(Fe_Res(), p, NULL, false, 0);
    }
    for (int i = 0; i < 3; i++) {
        Path_Format(p, sizeof p, 0x524ba0, Path_Root(), i + 1);
        g_fe.livery[i] = Bfl_FindTexture(Fe_Res(), p, NULL, false, 0);
    }
}
