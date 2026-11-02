/* The front end's work on the way out (0x406820, 0x4cf060, 0x4d0230, 0x4eabc0, 0x4ec1a0, and the
   teardown 0x49c0f0 / 0x4a5be0 / 0x4a5d10 runs). */
#include "game/saves.h"
#include "fe/fe.h"
#include "fe/fe_page.h"
#include "audio/snd.h"
#include "font.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "render/tex.h"
#include "text.h"
#include <string.h>

/* 0x4cf060 with 0x4d0230: the players, mode, difficulty, rally and stage (a super special's two
   indices with bit 24) for the championship's tables (0x8173f0), each player's entry cleared */
void Fe_ChampTables(void)
{
    uint32_t mw = gm32(0x52afb4), sel = gm32(0x52f2ac), n = mw >> 13 & 0xf;
    gw32(0x8173f0, n);
    gw32(0x8173f4, mw >> 3 & 0x7f);
    gw32(0x817400, mw >> 10 & 7);
    gw32(0x8173f8, sel & 0x1f);
    if (sel >> 0x18 & 1) {
        gw32(0x8173fc, sel >> 0xc & 3);
        gw32(0x817404, sel >> 10 & 3);
    } else
        gw32(0x8173fc, sel >> 5 & 0x1f);
    for (uint32_t i = 0, a = 0x817424; i < n; i++, a += 10) {
        gw16(a + 4, 0), gw16(a, 0), gw16(a + 2, 0);
        gw8(a - 4, 0), gw8(a - 3, 0), gw8(a - 2, 0), gw8(a - 1, 0);
    }
    gw32(0x817418, 0);
    gw16(0x817414, 0), gw16(0x817416, 0);
    gw8(0x817410, 0), gw8(0x817411, 0), gw8(0x817412, 0), gw8(0x817413, 0);
}

/* 0x4eabc0: the options word's third byte from the second by the mode: modes 1-3 and 7 all its bits,
   4-6 bits 8, 10 and 11 only, 0 and the demo bit 10 */
void Fe_OptionsMode(void)
{
    uint32_t o = gm32(0x52afc0), m = Gs_Mode(), keep;
    if (gm8(0x52ea52) || m == 0 || m == 4) keep = 0x400;
    else if (m == 5 || m == 6) keep = 0xd00;
    else keep = 0xff00;
    gw32(0x52afc0, (o & keep) << 8 | (o & 0xff00ffffu));
}

/* 0x406820: each player's set-up from the rally's (0x51682c, 7 bytes); with a chosen wet weather the
   tyres (even ones but 6) one up */
void Fe_CarSetups(void)
{
    uint32_t r = gm32(0x52f2ac) & 0x1f;
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t p = 0x52f0e0 + 7 * i;
        for (uint32_t k = 0; k < 7; k++) gw8(p + k, gm8(0x51682c + r * 7 + k));
        if (gm32(0x52f290) && gm32(0x52f0fc) > 2) {
            int8_t c = (int8_t)gm8(p);
            if (c % 2 == 0 && c != 6) gw8(p, (uint8_t)(c + 1));
        }
    }
}

/* 0x4ec1a0: each player's profile one race older (+0x40 bits 7-14, to 0xff) */
void Fe_AgeProfiles(void)
{
    uint32_t n = gm32(0x52afb4) >> 13 & 0xf;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t p = gm32(0x531350 + 0x30 * i) >> 0x12 & 0xf, a = 0x52fa5c + p * 0x650, v = gm32(a);
        if ((v & 0x7f80) < 0x7f80) gw32(a, ((((v & 0xffffff80u) + 0x80) ^ v) & 0x7f80) ^ v);
    }
}

/* the at-exit functions the front end registered since its start (0x49c0f0 back to its mark): its
   archives (0x4d2100), the saves list (0x4f4e80), the sound layer (0x4b7ae0), the text (0x4f4cc0)
   and the credits (0x4f48b0); then every texture's surface and record (0x4a5be0, 0x4a5d10) */
void Fe_Teardown(void)
{
    Bfl_Unmount(Fe_Res());
    Champs_Free();
    Bfl_Unmount(Fe_ResLang());
    Bfl_Unmount(Fe_Common());
    for (int i = 0; i < 5; i++) Bfl_Unmount(Fe_Text(i));
    Voices_StopAll();
    Snd_Truncate(0);
    Text_Free();
    Font_FreeSlots();
    Tex_FreeAll();
}
