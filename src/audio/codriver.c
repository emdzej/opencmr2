/* The co-driver (see codriver.h). */
#include "audio/codriver.h"
#include "audio/snd.h"
#include "game/common.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/race.h"
#include "fixmath.h"
#include "exe.h"
#include "game/lights.h"
#include "pending.h"
#include "sim/car.h"
#include "sim/progress.h"
#include "world/stage.h"
#include <stdio.h>
#include <string.h>

static int base;                 /* DAT_0053735c: the first speech sound */
static const uint8_t *cod;       /* DAT_00537358: the stage's call descriptors */
static uint32_t ncod;
static int32_t volume = 0x10000; /* DAT_00537194 */

/* The speech queue (0x5370a0, 20 entries of 12 bytes): a sound, its voice, 1 playing and 2 in use */
typedef struct { int id; int32_t voice; uint8_t flags; } Speech;
static Speech queue[20];

/* The calls per player (0x5371d0, five of 12 bytes): two descriptors, the flags (low byte a countdown
   in steps, 0x100 in use, 0x200 spoken) */
typedef struct { uint32_t desc, desc2, flags; } Call;
static Call calls[4][5];
static int last_point[4];        /* 0x53708c */
static int wrong_at[4];          /* 0x537198 */
static int wrong_way[4];         /* 0x537248 */
static int counted[4][6];        /* 0x5371a4: the countdown's numbers said (five a player, the sixth the next's first) */
static bool go_said;             /* DAT_005371a0 */
static bool ahead;               /* DAT_00537354: calls within six points */
static int said;                 /* the port: the words queued, for the tests */
int Codriver_Said(void) { return said; }

int32_t Codriver_Level(void) { return volume; }
void Codriver_Volume(int32_t speech_percent) { volume = fx_mul((speech_percent << 16) / 100, 0x10000); }   /* 0x418560 */

/* 0x416770: the speech names in the order the code pushes them (each a "%s%s" of the sounds'
   directory and \Speech\English\Wavs\NAME.WAV), read from the function's pushes of their addresses */
bool Codriver_LoadSpeech(void)
{
    base = Snd_Count();
    uint32_t size = 0x416f70 - 0x416770;
    const uint8_t *code = exe_data(0x416770, size);
    if (!code) return false;
    int n = 0;
    for (uint32_t i = 0; i + 5 <= size; i++) {
        if (code[i] != 0x68) continue;   /* push imm32 */
        uint32_t a = (uint32_t)code[i + 1] | code[i + 2] << 8 | code[i + 3] << 16 | (uint32_t)code[i + 4] << 24;
        const char *s = a >= 0x516000 && a < 0x52a000 ? exe_str(a) : NULL;
        if (!s || strncmp(s, "\\Speech\\", 8)) continue;
        char p[96];
        snprintf(p, sizeof p, "Sounds%s", s);
        Snd_LoadNamed(p, Bfl_Speech());   /* 0x418760 */
        n++;
        i += 4;
    }
    return n > 0;
}

/* 0x416670 */
void Codriver_Reset(void)
{
    for (int p = 0; p < 4; p++) {
        wrong_at[p] = 9999, wrong_way[p] = 0, last_point[p] = -1;
        for (int k = 0; k < 5; k++) calls[p][k].flags &= 0xfffffc00, calls[p][k].desc = calls[p][k].desc2 = 0;
        for (int k = 0; k < 6; k++) counted[p][k] = 0;
    }
    go_said = false;
    for (int i = 0; i < 20; i++) queue[i].flags &= 0xfc, queue[i].voice = -1, queue[i].id = -1;
}

bool Codriver_LoadCod(const char *stage)
{
    Codriver_Reset();
    char name[32];
    uint32_t size = 0;
    snprintf(name, sizeof name, "%s.cod", stage);
    cod = Bfl_FindFile(&g_stage_bfl, name, NULL, &size, false);
    ncod = cod ? size / 4 : 0;
    return cod != NULL;
}

static uint32_t cod_at(unsigned i) { return i < ncod ? (uint32_t)cod[4 * i] | cod[4 * i + 1] << 8 | cod[4 * i + 2] << 16 | (uint32_t)cod[4 * i + 3] << 24 : 0; }
static bool rolled(int car) { return CI32(Car_Get(car), 0xc10) != 0; }   /* 0x417760 */

/* 0x417660: a sound into the first free place */
static void say(int id)
{
    said++;
    for (int i = 0; i < 20; i++)
        if (!(queue[i].flags & 2)) {
            queue[i].flags = (uint8_t)((queue[i].flags | 2) & 0xfe);
            queue[i].id = id;
            queue[i].voice = -1;
            return;
        }
}

/* 0x4176b0: the first sound played at the co-driver's volume, 11,025 Hz; when it ends, the queue on */
static void speak(void)
{
    if (!(queue[0].flags & 2) || rolled(0)) return;
    if (!(queue[0].flags & 1)) {
        queue[0].voice = Voice_Play(queue[0].id, volume, 0x2b11, 0, false, false);
        queue[0].flags |= 1;
    } else if (!Voice_Playing(queue[0].voice)) {
        memmove(&queue[0], &queue[1], 19 * sizeof queue[0]);
        queue[19].flags &= 0xfc, queue[19].voice = queue[19].id = -1;
    }
}

/* 0x416f70 (the countdown's state): three, two, one */
void Codriver_Start(int p)
{
    int c = Race_Countdown() > 499 ? 499 : Race_Countdown();   /* 0x41d780 */
    int left = (500 - c) / 100;
    last_point[p] = Car_CatIndex(Car_Get(p));
    if (Gs_SelBit26()) PENDING_NOTE(0x416f70, "Codriver_Start (the arcade's lights)");
    speak();
    if (0 < p) {
        PENDING_NOTE(0x416f70, "Codriver_Start (the second player)");
        return;
    }
    if (!counted[0][left]) {
        counted[0][left] = 1;
        Lights_Countdown(left + 1);   /* 0x463ce0 */
        if (left == 2) say(base + 0x38);
        else if (left == 1) say(base + 0x39);
        else if (left == 0) say(base + 0x3a);
    }
    last_point[p] = Car_CatIndex(Car_Get(p)) - 1;
}

/* 0x4174e0: a call queued from the stage's code (and the next point's): a corner whose modifier
   (crest, jump, over) or second modifier is split off into a second call */
static void queue_call(int p, uint8_t code, uint8_t next)
{
    if (rolled(p)) return;
    uint32_t d = 0, d2 = 0, m1, sev, dir;
    if (code == 0xff) {
        m1 = sev = dir = (uint32_t)p;   /* (the original's registers: the player's number) */
    } else {
        d = cod_at(code);
        if (next != 0xff) d2 = cod_at(next);
        m1 = d >> 0x11 & 0xf, sev = d & 0xf, dir = d >> 4 & 3;
        uint32_t m2 = d >> 0x15 & 3;
        bool split1 = (m1 == 0xe || m1 == 2 || m1 == 1) && dir != 0, split2 = m2 != 0 && dir != 0;
        if (!d2) {
            if (split1) d -= m1 << 0x11, d2 = m1 << 0x11;
            else if (split2) d -= m2 << 0x15, d2 = m2 << 0x15;
        }
    }
    if (sev == 6 && dir == 0 && m1 == 0) return;
    for (int k = 0; k < 5; k++) {
        Call *c = &calls[p][k];
        if (c->flags & 0x100) continue;
        c->flags = (c->flags & 0xfffffd32) | 0x132;   /* in use, 50 steps, not spoken */
        c->desc = d;
        c->desc2 = d2;
        PENDING_NOTE(0x418580, "Notes_Arrow");
        return;
    }
}

void Codriver_Step(int p)
{
    int fin = Cat_FinishPoint();
    if (!fin) return;
    if (Gs_SelBit24() || Gs_SelBit25()) PENDING_NOTE(0x417090, "Codriver_Step (the arcade)");
    int last = last_point[p], cur = Car_CatIndex(Car_Get(p));
    if (!go_said && p == 0) {
        Lights_Countdown(0);   /* 0x463ce0 */
        say(base + 0xd);   /* "go" */
        go_said = true;
    }
    speak();
    const Car *c = Car_Get(p);
    int32_t t[3], f[3] = {CI32(c, 0x3f0), CI32(c, 0x3f4), CI32(c, 0x3f8)};
    if (cur < last - 12) last_point[p] = cur;
    if (cur < last - 1 && (fin >> 1 <= cur || last <= fin >> 1)) {
        Cat_Tangent(t, cur);
        if (fx_mul(t[2], f[2]) + fx_mul(t[0], f[0]) + fx_mul(t[1], f[1]) < -0x8000) wrong_way[p] = 1, wrong_at[p] = cur;
    }
    if (fin >> 1 < cur && last < fin >> 1) wrong_at[p] = cur, last_point[p] = last = cur;
    if (wrong_way[p]) {
        Cat_Tangent(t, cur);
        if (0x3333 < fx_mul(t[2], f[2]) + fx_mul(t[0], f[0]) + fx_mul(t[1], f[1])) wrong_way[p] = 0;
    }
    if (wrong_at[p] < cur) wrong_way[p] = 0;
    ahead = false;
    for (int i = last_point[p] + 1; i <= last_point[p] + 6; i++)
        if (fin <= i || Cat_Record(i)[0x19] != 0xff) ahead = true;
    if (Race_State(p) == 9 || (calls[p][0].flags & 0x100)) ahead = true;
    if (!rolled(p) && Race_State(p) == 8) {
        if (last_point[p] < cur) {
            for (int i = last + 1; i <= cur; i++) {
                if (Cat_Record(i)[0x19] == 0xff || rolled(p)) continue;
                if (i + 1 >= 2 && Cat_Record(i - 1)[0x19] != 0xff) continue;   /* said with the one before */
                uint8_t next = i + 1 < Cat_Points() ? Cat_Record(i + 1)[0x19] : 0xff;
                queue_call(p, Cat_Record(i)[0x19], next);
            }
            last_point[p] = cur;
        }
    }
    if (cur < wrong_at[p]) wrong_at[p] = cur;
    if (wrong_at[p] + 6 < cur) wrong_at[p] = cur - 6;
    /* the first call's countdown (a type-0x19 call waits while there is nothing ahead) */
    Call *c0 = &calls[p][0];
    if (c0->flags & 0x100) {
        if ((uint8_t)c0->flags != 0x19 || ahead) c0->flags = (c0->flags & ~0xffu) | (uint8_t)((uint8_t)c0->flags - 1);
        if (!(uint8_t)c0->flags) {
            memmove(&calls[p][0], &calls[p][1], 4 * sizeof calls[p][0]);
            calls[p][4].flags &= 0xfffffc00, calls[p][4].desc = calls[p][4].desc2 = 0;
        }
    }
}

/* 0x418000: a call's words, in order: a ninety (or a K), the distance, into/and, long, the corner's
   severity, its direction (swapped by option 2), opens or tightens, the modifiers */
static void words(uint32_t d)
{
    uint32_t sev = d & 0xf, dir = d >> 4 & 3, dist = d >> 6 & 0xf, join = d >> 10 & 3, chg = d >> 12 & 7, m0 = d >> 15 & 3,
             m1 = d >> 17 & 0xf, m2 = d >> 21 & 3, lng = d >> 23 & 1, b24 = d >> 24 & 1, m3 = d >> 25 & 7, m4 = d >> 28 & 3;
    if (sev == 6) {
        if (!dir && !dist && !join && !chg && !m1 && !m2 && !m0 && !m4 && !b24 && !m3) return;
    } else if (sev == 9)
        say(base + 0x37);
    static const int8_t dists[15] = {-1, 0xe, 0xf, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16};
    if (dist < 15 && dists[dist] >= 0) say(base + dists[dist]);
    if (m2 == 1) say(base + 0x1a);
    else if (m2 == 2) say(base + 0x1b);
    if (join == 1) say(base + 10);
    else if (join == 2) say(base + 0x1c);
    if (lng) say(base + 0x1d);
    static const int8_t sevs[9] = {-1, 5, 4, 3, 2, 1, 0, 6, 7};
    if (sev < 9 && sevs[sev] >= 0) say(base + sevs[sev]);
    bool swap = Gs_Option(2);
    if (dir == 1) say(base + (swap ? 9 : 8));
    else if (dir == 2) say(base + (swap ? 8 : 9));
    if (chg == 1) say(base + 0xc);
    else if (chg == 2) say(base + 0xb);
    else if (chg == 3) say(base + 0x1e);
    if (m0 == 1) say(base + 0x17);
    else if (m0 == 2) say(base + 0x1f);
    static const int8_t mods[16] = {-1, 0x19, 0x18, 0x22, 0x23, 0x24, 0x21, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x36, 0x20};
    if (mods[m1] >= 0) say(base + mods[m1]);
    if (m4 == 1) say(base + 0x2c);
    else if (m4 == 2) say(base + 0x2d);
    if (b24) say(base + 0x2e);
    if (m3 >= 1 && m3 <= 7) say(base + 0x2e + (int)m3);
}

/* 0x418580's answer: a call with an arrow on the HUD (a direction, a severity but 0 and 9, the
   crest, jump or over modifiers, the second modifier) */
static bool arrow(uint32_t d)
{
    uint32_t sev = d & 0xf, dir = d >> 4 & 3, m1 = d >> 0x11 & 0xf, m2 = d >> 0x15 & 3;
    return dir != 0 || (sev != 0 && sev != 9) || m1 == 0xe || m1 == 2 || m1 == 1 || m2 != 0;
}

/* 0x4177d0's speech: each call not yet spoken (one player, no split screen); the walk stops at a call
   with no arrow, as the original's does (the calls after it wait until it has gone) */
void Codriver_Notes(int p)
{
    PENDING_NOTE(0x4177d0, "Notes_Draw (the arrows, the wrong way)");
    for (int k = 0; k < 5; k++) {
        Call *c = &calls[p][k];
        if (!(c->flags & 0x100)) continue;
        if (!(c->flags & 0x200)) {
            words(c->desc);
            if (c->desc2) words(c->desc2);
            c->flags |= 0x200;
        }
        if (!arrow(c->desc) && !(c->desc2 && arrow(c->desc2))) return;
    }
}
