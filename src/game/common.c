/* The race's common archives and fonts (see common.h). */
#include "game/common.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "exe.h"
#include "font.h"
#include "text.h"
#include "render/d3d7.h"
#include "pending.h"
#include <stdio.h>
#include <string.h>

static BflSlot com, speech, text, common_arc, surface, firework;
BflSlot *Bfl_Com(void) { return &com; }
BflSlot *Bfl_Speech(void) { return &speech; }
BflSlot *Bfl_Text(void) { return &text; }
BflSlot *Bfl_CommonArc(void) { return &common_arc; }
BflSlot *Bfl_Surface(void) { return &surface; }
BflSlot *Bfl_Firework(void) { return &firework; }

static void mount(BflSlot *s, const char *path)
{
    Bfl_Unmount(s);
    Bfl_Mount(s, path);
}

void Bfl_MountCommon(void)
{
    /* the language (0x52ea54): the speech and text archives' names (0x519fbc, 0x519fe4 by 0x405800,
       one per mode-word low bits 0x4057f0), the region's directory (0x516124) */
    uint32_t lang = gm32(0x52ea54), slot = gm32(0x52afb4) & 7;
    static const uint32_t sp[4] = {0x519fbc, 0x519fd0, 0x519fdc, 0x519fe0}, tx[4] = {0x519fe4, 0x519ff8, 0x51a004, 0x51a008};
    int w, h;
    D3d_ScreenSize(&w, &h);
    uint32_t res = 0x280;
    if (w > 0x3ff) PENDING_NOTE(0x4b7560, "Bfl_MountCommon (Com1024 with the texture memory)");
    /* 0x4a9700: compressed textures in use, not in the port's device */
    char p[160];
    snprintf(p, sizeof p, "Game\\Bigfiles\\Com%u.bfl", res);
    mount(&com, p);
    const char *region = exe_str(exe_u32(0x516124 + 4 * lang));
    if (lang < 4) {
        snprintf(p, sizeof p, "CountrySpecific%s%s.bfl", region, exe_str(exe_u32(sp[lang] + 4 * slot)));
        mount(&speech, p);
        snprintf(p, sizeof p, "CountrySpecific%s%s.bfl", region, exe_str(exe_u32(tx[lang] + 4 * slot)));
        mount(&text, p);
    }
    mount(&common_arc, "Game\\Bigfiles\\Common.bfl");
    mount(&surface, "Sounds\\surface.bfl");
    if (Gs_Mode() == 0 && (gm8(0x52f2ac) & 0x1f) == 8 && (gm32(0x52f2ac) & 0x3e0) == 0x160) mount(&firework, "Game\\Bigfiles\\FireWork.bfl");
}

void Race_LoadFonts(void)
{
    Font_InitSlots(7);
    for (int i = 0; i < 7; i++) {
        const char *n = exe_str(0x51e994 + 0x14u * (uint32_t)i);
        char dir[32];
        const char *b = strrchr(n, '\\');
        size_t l = b ? (size_t)(b - n) : 0;
        if (l >= sizeof dir) l = sizeof dir - 1;
        memcpy(dir, n, l);
        dir[l] = 0;
        Font_LoadSlot(dir, b ? b + 1 : n, &com, i);
    }
}

/* 0x478a20: g<language>.txt from the race's text archive, one table of 260 strings */
bool Race_LoadText(void)
{
    static const uint32_t europe[5] = {0x51e988, 0x51e980, 0x51e974, 0x51e968, 0x51e960};
    static const uint32_t america[3] = {0x51e958, 0x51e980, 0x51e974};
    static const uint32_t japan[1] = {0x51e988}, poland[1] = {0x51e950};
    const uint32_t *names = europe;
    switch (gm32(0x52ea54)) {
    case 1: names = america; break;
    case 2: names = japan; break;
    case 3: names = poland; break;
    }
    char p[64];
    snprintf(p, sizeof p, exe_str(0x51e948), exe_str(names[gm32(0x52afb4) & 7]));
    uint32_t sz = 0;
    const uint8_t *f = Bfl_FindFile(&text, p, NULL, &sz, false);
    if (!f) return false;
    size_t n = sz;
    Text_Build(1, 0x104, &f, &n);
    return true;
}
