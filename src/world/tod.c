/* The stage's time of day and weather (see tod.h). */
#include "world/tod.h"
#include "exe.h"
#include "fixmath.h"
#include "game/mem.h"
#include "world/relight.h"
#include "world/stage.h"
#include <string.h>
#include <strings.h>

const Horizon *g_hor_start, *g_hor_end;
int32_t g_hor_low[3], g_hor_high[3];
static Horizon hor_start, hor_end;   /* 0x543d00, 0x543e38 */

bool Tod_Select(const char *stage)
{
    size_t l = strlen(stage);
    if (l < 3) return false;
    for (uint32_t r = 0; r < 9; r++) {
        const char *p = exe_str(exe_u32(0x51931c + 4 * r));
        size_t pl = strlen(p);
        if (!pl || pl + 2 != l || strncasecmp(stage, p, pl)) continue;
        int n = (stage[pl] - '0') * 10 + (stage[pl + 1] - '0');
        if (n < 1 || n > 32) return false;
        gw32(0x52f2ac, (gm32(0x52f2ac) & ~0x3ffu) | r | (uint32_t)(n - 1) << 5);   /* rally, stage */
        return true;
    }
    return false;
}

uint32_t Tod_Rally(void) { return gm32(0x52f2ac) & 0x1f; }
uint32_t Tod_Stage(void) { return gm32(0x52f2ac) >> 5 & 0x1f; }
static uint32_t mode(void) { return gm32(0x52afb4) >> 3 & 0x7f; }   /* 0x405d80 */
static uint32_t mode2(void) { return gm32(0x52afb4) >> 10 & 7; }    /* 0x405d90 */

/* 0x407270: a rally, rally 8, stage 12 (the one stage run at night) */
static bool night_stage(void) { return mode() == 0 && (gm32(0x52f2ac) & 0x1f) == 8 && (gm32(0x52f2ac) & 0x3e0) == 0x160; }

/* bits 24 and 26: 0x516260 by the two indices (and the mode) */
static uint32_t table_time(void)
{
    uint32_t a = gm32(0x52f2ac) >> 10 & 3, b = gm32(0x52f2ac) >> 12 & 3;
    uint32_t i = gm32(0x52f2ac) >> 26 & 1 ? mode2() + (a * 3 + b) * 3 : (a * 3 + b) * 3;
    return gm8(0x516260 + i) * 100u;
}

static uint32_t time_of(int second)
{
    if (gm32(0x52f2ac) >> 24 & 1) return table_time();
    if (night_stage()) return 2000;
    return gm8(0x516188 + (uint32_t)second + (Tod_Stage() + Tod_Rally() * 12) * 2) * 100u;
}

uint32_t Tod_StartTime(void) { return time_of(0); }
uint32_t Tod_EndTime(void) { return time_of(1); }

const int32_t *Tod_Weather(uint32_t stage)
{
    static int32_t w[2];
    uint32_t a = gm32(0x52f2ac) >> 10 & 3, b = gm32(0x52f2ac) >> 12 & 3;
    if (gm32(0x52f2ac) >> 24 & 1) {
        uint32_t va = gm32(0x52f2ac) >> 26 & 1 ? 0x52f36c + ((a * 3 + b) * 3 + mode2()) * 4 : 0x52f36c + (a * 3 + b) * 0xc;
        w[0] = w[1] = gms32(va);
        return w;
    }
    w[0] = gms32(0x52f100 + stage * 8);
    w[1] = gms32(0x52f104 + stage * 8);
    return w;
}

/* 0x4616c0: b + (a - b) t, the 16.16 result's whole part, at most 255 */
static uint8_t lerp8(uint8_t a, uint8_t b, int32_t t)
{
    int32_t v = (b * 0x10000 + fx_mul(a * 0x10000 - b * 0x10000, t)) >> 16;
    return (uint8_t)(v > 0xff ? 0xff : v);
}

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static void wr32(uint8_t *p, int32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)((uint32_t)v >> 24); }

void Hor_Blend(Horizon *out, const Horizon *a, const Horizon *b, int32_t t)
{
    Horizon r;
    memcpy(&r, out, sizeof r);   /* +0x4e, +0x4f keep what out held */
    for (int i = 0x1c; i < 0x48; i++) r.b[i] = lerp8(b->b[i], a->b[i], t);
    for (int i = 0; i < 7; i++) wr32(r.b + 4 * i, rd32(a->b + 4 * i) + fx_mul(rd32(b->b + 4 * i) - rd32(a->b + 4 * i), t));
    for (int i = 0x48; i < 0x4e; i++) r.b[i] = lerp8(b->b[i], a->b[i], t);
    memcpy(out, &r, sizeof r);
}

/* The times are the 12 of 0x51b034 (5:00 to 24:00); a time before the first blends from 24:00 (the
   previous slot wraps to the last, from minute 0). The weather condition picks two of the four file sets
   (0x51b0dc: none, CLO, STO, BLI) and their mix (0x51b0f0). */
const Horizon *Hor_ForTime(uint32_t time, int which, const uint8_t *const files[48])
{
    const int32_t *w = Tod_Weather(Tod_Stage());
    int32_t c = which ? w[1] : w[0];
    int next = -1;
    for (int i = 0; i < 12; i++)
        if ((uint16_t)time <= exe_u16(0x51b034 + 2 * i)) { next = i; break; }
    if (next < 0) return NULL;
    int prev = next - 1;
    int32_t prev_min = 0;
    if (prev < 0) prev = next + 11;
    else {
        uint32_t h = exe_u16(0x51b034 + 2 * prev);
        prev_min = (int32_t)(h * 0x10000 - (h / 100) * 0x280000);
    }
    uint32_t mm = (uint16_t)time % 100;
    if (mm >= 60) return NULL;
    int32_t cur = (int32_t)(((uint16_t)time / 100) * 0x3c0000 + mm * 0x10000) - prev_min;
    uint32_t hn = exe_u16(0x51b034 + 2 * next);
    int32_t span = (int32_t)((hn - (hn / 100) * 0x28) * 0x10000) - prev_min;
    int32_t t = span ? (int32_t)(((int64_t)(cur >> 16) << 32) / span) : 0;
    if (t > 0x10000) t = 0x10000;
    else if (t < 0) t = 0;
    int32_t mix = (int32_t)exe_u32(0x51b0f0 + 4 * (uint32_t)c);
    const uint8_t *sets = exe_data(0x51b0dc + 2 * (uint32_t)c, 2);
    if (!sets) return NULL;
    const uint8_t *ap = files[sets[0] * 12 + prev], *an = files[sets[0] * 12 + next];
    const uint8_t *bp = files[sets[1] * 12 + prev], *bn = files[sets[1] * 12 + next];
    if (!ap || !bp || !an || !bn) return NULL;
    Horizon *out = which ? &hor_end : &hor_start;
    static Horizon x, y;   /* the two weather mixes (on the original's stack) */
    Hor_Blend(&x, (const Horizon *)ap, (const Horizon *)bp, mix);
    Hor_Blend(&y, (const Horizon *)an, (const Horizon *)bn, mix);
    Hor_Blend(out, &x, &y, t);
    return out;
}

static void wr(Horizon *h, int o, int32_t v) { wr32(h->b + o, v); }
static int32_t rd(const Horizon *h, int o) { return rd32(h->b + o); }

/* 0x461b30: a record's fog range (+0x14 near, +0x18 far) by its weather: 15 to 80 on a stage with
   heavy rain (0x52f1f0), else none near and far at the draw distance's (option 0x405ca0, + 2) x 50
   over the type's fog factor (0x51b114; none at 0) */
static void hor_fog(Horizon *h, int32_t type)
{
    wr(h, 0x14, 0), wr(h, 0x18, 0);
    uint32_t v = ((gm32(0x52afd0) >> 0x15 & 0xf) + 2) * 0x320000;
    if (gm32(0x52f1f0 + 4 * Tod_Stage())) {
        wr(h, 0x14, 0xf0000), wr(h, 0x18, 0x500000);
        return;
    }
    int32_t f = gms32(0x51b114 + 4 * (uint32_t)type);
    if (f) wr(h, 0x18, (int32_t)(((int64_t)(int32_t)v << 16) / f));
}

/* 0x461a70: the weather's changes to the two records: a cloudy end (type 1) gets the sky's +0x2f 200
   next to clear or cloudy, else 50; their fog ranges; one without fog takes the other's far value for
   both of its own */
static void hor_weather(Horizon *a, Horizon *b)
{
    if (!a || !b) return;
    const int32_t *w = Tod_Weather(Tod_Stage());
    int32_t s = w[0], e = w[1];
    if (s == 1) a->b[0x2f] = e == 0 || e == 1 ? 200 : 0x32;
    if (e == 1) b->b[0x2f] = s == 0 || s == 1 ? 200 : 0x32;
    hor_fog(a, s);
    hor_fog(b, e);
    if (!rd(a, 0x14) && !rd(a, 0x18) && (rd(b, 0x14) || rd(b, 0x18))) {
        wr(a, 0x18, rd(b, 0x18)), wr(a, 0x14, rd(b, 0x18));
        return;
    }
    if (!rd(b, 0x14) && !rd(b, 0x18) && (rd(a, 0x14) || rd(a, 0x18))) wr(b, 0x18, rd(a, 0x18)), wr(b, 0x14, rd(a, 0x18));
}

/* The file names are the 48 strings at 0x5196dc down in steps of 12 (0500.hor ... 2400BLI.hor). Not
   ported yet: the six fog-colour bytes (the two records' +0x48-+0x4d averaged, 0x4a5ff0-0x4a60b0). */
void Stage_LoadHorizons(void)
{
    const uint8_t *files[48];
    for (int i = 0; i < 48; i++) {
        uint32_t size = 0;
        files[i] = Bfl_FindFile(&g_stage_bfl, exe_str(0x5196dc - 12 * (uint32_t)i), NULL, &size, false);
        if (size < HOR_SIZE) files[i] = NULL;
    }
    uint32_t end = Tod_EndTime(), start = Tod_StartTime();
    g_hor_start = Hor_ForTime(start, 0, files);
    g_hor_end = Hor_ForTime(end, 1, files);
    hor_weather(g_hor_start ? &hor_start : NULL, g_hor_end ? &hor_end : NULL);   /* 0x461a70 */
    if (g_hor_start && g_hor_end) Light_Defaults();
}

void Hor_Apply(void)
{
    for (int i = 0; i < 3; i++) {
        g_hor_low[i] = g_hor_start && g_hor_end ? g_hor_start->b[0x30 + i] << 16 : 0xc80000;
        g_hor_high[i] = g_hor_start && g_hor_end ? g_hor_start->b[0x38 + i] << 16 : 0xff0000;
    }
}

void Tod_StartLights(void)
{
    uint8_t low[4] = {(uint8_t)(g_hor_low[0] >> 16), (uint8_t)(g_hor_low[1] >> 16), (uint8_t)(g_hor_low[2] >> 16), 0xff};
    uint8_t cur[4];
    Light_GetLow(cur);
    if (cur[0] != low[0] || cur[1] != low[1] || cur[2] != low[2]) Light_SetLow(low, Tod_Rally() != 3);
    Light_SetHigh(g_hor_high, Tod_Rally() != 3);
    Light_RelightAll();
}
