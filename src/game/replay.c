/* The replays (see replay.h). The control block's fields are kept in the image at the original's
   offsets (so a saved file is the original's layout); its pointers (+0x24, +0x30, +0x3c, +0x40,
   +0x104 and the unread +0x28, +0x2c, +0x34, +0x38) are kept beside it. */
#include "game/replay.h"
#include "game/mud.h"
#include "game/race.h"
#include "game/gstate.h"
#include "game/opponents.h"
#include "game/ghost.h"
#include "game/mem.h"
#include "game/input.h"
#include "game/weather.h"
#include "game/car_load.h"
#include "game/profile.h"
#include "game/paths.h"
#include "game/ai.h"
#include "game/sectors.h"
#include "sim/car.h"
#include "audio/crash.h"
#include "game/camera.h"
#include "net/carsync.h"
#include "net/records.h"
#include "world/section.h"
#include "fixmath.h"
#include "sim/damage.h"
#include "sim/drive.h"
#include "sim/knock.h"
#include "sim/progress.h"
#include "world/stage.h"
#include "world/tod.h"
#include "bfl.h"
#include "exe.h"
#include "pending.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { HDR = 0x110, SNAP0 = 0x114c, SNAP1 = 0x5c, CHG0 = 0x110c, NCHG0 = 0x1148, CHG1 = 0x1c, NCHG1 = 0x58 };

struct Replay {
    uint8_t *b;          /* the image */
    uint32_t size;
    bool owned;          /* !0x588d18: freed with the replays (not an archive's member) */
    uint8_t *snap;       /* +0x24 (kind 0) or +0x30 */
    uint8_t *frames;     /* +0x3c (kinds 0, 1) or +0x40 */
    uint8_t *counts;     /* +0x104: i16 per segment */
};

static Replay *made[8], *loaded[8];   /* 0x588e80, 0x588ea0 */
static int nmade, nloaded;            /* 0x588d3c, 0x588d14 */
static Replay *race[8];               /* 0x537f3c */

static int32_t h32(const Replay *r, uint32_t o) { const uint8_t *p = r->b + o; return (int32_t)((uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static void w32(Replay *r, uint32_t o, int32_t v) { uint8_t *p = r->b + o; uint32_t u = (uint32_t)v; p[0] = (uint8_t)u, p[1] = (uint8_t)(u >> 8), p[2] = (uint8_t)(u >> 16), p[3] = (uint8_t)(u >> 24); }
static int16_t h16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static void w16(uint8_t *p, int v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static int kind(const Replay *r) { return h32(r, 0x1c); }
static int segs(const Replay *r) { return h16(r->b + 0xfc); }
static int per_seg(const Replay *r) { return h16(r->b + 0xfe); }
static uint32_t snap_size(const Replay *r) { return kind(r) == 0 ? SNAP0 : SNAP1; }
static uint8_t *snap_of(const Replay *r, int seg) { return r->snap + (size_t)seg * snap_size(r); }
static uint8_t *frame_of(const Replay *r, int seg, int k) { return r->frames + ((size_t)per_seg(r) * seg + k) * 4; }
static uint8_t *count_of(const Replay *r, int seg) { return r->counts + 2 * seg; }

/* 0x46d470: the pointers from the counts and the kind */
static void layout(Replay *r)
{
    uint32_t ss = snap_size(r), fs = kind(r) == 2 ? 0x10 : 4;
    r->snap = r->b + HDR;
    r->frames = r->b + HDR + (uint32_t)segs(r) * ss;
    r->counts = r->b + HDR + ((uint32_t)per_seg(r) * fs + ss) * (uint32_t)segs(r);
}

static uint32_t image_size(int s, int f, int k)
{
    return (uint32_t)(s * f * (k == 2 ? 0x10 : 4)) + HDR + (uint32_t)s * 2 + (uint32_t)s * (k == 0 ? SNAP0 : SNAP1);
}

Replay *Replay_New(int s, int f, int k)
{
    if (nmade == 8) return NULL;
    Replay *r = calloc(1, sizeof *r);
    uint32_t n = image_size(s, f, k);
    if (!r || !(r->b = calloc(n, 1))) return free(r), NULL;
    r->size = n, r->owned = true;
    w32(r, 0x1c, k);
    w16(r->b + 0xfc, s), w16(r->b + 0xfe, f);
    layout(r);
    w32(r, 4, 0), w32(r, 0xc, 0), w32(r, 0x14, 0), w32(r, 0x18, 0);
    w16(r->b + 0x100, 0);
    for (int i = 0; i < 8; i++)
        if (!made[i]) { made[i] = r; break; }
    nmade++;
    return r;
}

Replay *Replay_Load(const char *path)
{
    /* (the port: a file the game wrote, the ghost's .grp, is in the user's storage) */
    size_t usz = 0;
    uint8_t *d = plat_load_user_file(path, &usz);
    uint32_t size = (uint32_t)usz;
    if (!d) d = File_Load(path, 1), size = File_LastSize();
    bool owned = d != NULL;
    if (!d) {
        /* the stage archive's member, used in place (the playing's flags are written into it) */
        d = (uint8_t *)Bfl_FindFile(&g_stage_bfl, path, NULL, &size, false);
    }
    if (!d || nloaded == 8) {   /* (the original has no check on the count) */
        if (owned) free(d);
        return NULL;
    }
    int s = h16(d + 0xfc), f = h16(d + 0xfe) * s;
    if (size != (uint32_t)(s * 0x114e + HDR + f * 4) && size != (uint32_t)(s * 0x5e + HDR + f * 4) &&
        size != (uint32_t)(s * 0x114e + HDR + f * 0x10) && size != (uint32_t)(s * 0x5e + HDR + f * 0x10)) {
        if (owned) free(d);
        return NULL;
    }
    Replay *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    r->b = d, r->size = size, r->owned = owned;
    layout(r);
    loaded[nloaded++] = r;
    return r;
}

/* 0x46c6d0: every made replay freed; the loaded ones (but an archive's) in the demo and modes 3 and 7
   (the port frees them in every mode) */
void Replay_FreeAll(void)
{
    for (int i = 0; i < 8; i++) {
        if (made[i]) free(made[i]->b), free(made[i]), made[i] = NULL;
        if (loaded[i]) {
            if (loaded[i]->owned) free(loaded[i]->b);
            free(loaded[i]), loaded[i] = NULL;
        }
    }
    nmade = nloaded = 0;
    memset(race, 0, sizeof race);
}

/* 0x46c450: car's controls (+0x1d0) packed into the frame at e (Replay_PackControls 0x46bdc0): the
   three device flags (+0xb88, +0xb8c, +0xb90) only from a pad of type 3 (0x42b710's pad, its record's
   device), the count up by one */
static void pack(uint8_t *e, int car)
{
    Car *c = Car_Get(car);
    int pad = car < (int)Gs_SelBits14() ? car : -1;   /* 0x53acc8: the players' pads, -1 the CPU cars' */
    bool t3 = pad >= 0 && Input_Record(pad)->device == 3;
    const uint8_t *k = c->s + 0x1d0;
    uint8_t f88 = CI32(c, 0xb88) && t3, f8c = CI32(c, 0xb8c) && t3, f90 = CI32(c, 0xb90) && t3;
    /* byte 3: the flag, the side (set for +0x1d0) and the amount */
    e[3] = (uint8_t)(f88 << 7 | (k[0] ? 0x40 | (k[0] & 0x3f) : k[1] & 0x3f));
    e[0] = (uint8_t)(k[2] << 2 | f90 << 1 | f8c);
    e[1] = (uint8_t)(k[3] << 2 | ((k[4] + 1) & 3));
    uint8_t b2 = (uint8_t)(k[8] << 7 | (k[0xc] & 1) << 6 | (e[2] & 0x3f));
    e[2] = (uint8_t)((b2 & 0xc0) | ((b2 + 1) & 0x3f));
}

/* Replay_SameFrame 0x46cbe0: an empty frame takes the step; else the step's controls packed apart and
   compared with the frame's, all but the count, which must be below 63 */
static bool same_frame(const uint8_t *e, int car)
{
    if (!(e[2] & 0x3f)) return true;
    if ((e[2] & 0x3f) >= 0x3f) return false;
    uint8_t p[4];
    memcpy(p, e, 4);   /* (the original packs over the pointer's bytes; every compared bit is written) */
    pack(p, car);
    return p[0] == e[0] && p[1] == e[1] && p[3] == e[3] && !((p[2] ^ e[2]) & 0xc0);
}

/* 0x46c4b0 (0x46bec0): the frame into car's controls and device flags; true once its count of steps
   has been taken (the step counter `sub` back to 0) */
static bool unpack(const Replay *r, const uint8_t *e, int car, uint8_t *sub)
{
    Car *c = Car_Get(car);
    uint8_t *k = c->s + 0x1d0;
    uint8_t a = e[3] & 0x3f;
    if (!(e[3] & 0x40)) k[0] = 0, k[1] = a;
    else k[0] = a, k[1] = 0;
    k[2] = e[0] >> 2;
    k[3] = e[1] >> 2;
    CW32(c, 0x1d8, e[2] >> 7);
    k[4] = (uint8_t)((e[1] & 3) - 1);
    CW32(c, 0x1dc, (e[2] & 0x40) >> 6);
    CW32(c, 0xb8c, e[0] & 1);
    CW32(c, 0xb90, e[0] >> 1 & 1);
    CW32(c, 0xb88, !(e[3] & 0x80) ? 0 : h32(r, 0) == 0 ? 2 : 1);
    if ((uint8_t)++*sub < (e[2] & 0x3f)) return false;
    *sub = 0;
    return true;
}

/* 0x46bfd0: the car's state block from a snapshot, keeping its own models and parts (+0x71c-+0x754),
   contact points (+0x3c0-+0x3ef) and index (+0xb1a); the ghost (selection bit 28) at 25 steps a
   second, a head to head's CPU car at the stage's rate */
static void car_restore(const uint8_t *s, Car *c)
{
    uint8_t keep[0x754 - 0x71c], pts[0x30], idx = c->s[0xb1a];
    memcpy(keep, c->s + 0x71c, sizeof keep);
    memcpy(pts, c->s + 0x3c0, sizeof pts);
    memcpy(c->s, s, CAR_SIZE);
    memcpy(c->s + 0x71c, keep, sizeof keep);
    memcpy(c->s + 0x3c0, pts, sizeof pts);
    c->s[0xb1a] = idx;
    if (Gs_SelBit28()) CW32(c, 0xa98, 0x41c80000);
    if ((int8_t)idx > 0 && Gs_SelBit27() && !Gs_ModeBit19() && Gs_SelBits14() == 1) {
        float r = Opp_StepRate();   /* 0x456ae0 */
        uint32_t u;
        memcpy(&u, &r, 4);
        CW32(c, 0xa98, u);
    }
}

/* 0x46c240: the snapshot of car: its damage record, state block and progress record, then the
   progress started again (0x4207a0) */
static void snap_save(uint8_t *s, int car)
{
    memcpy(s, Damage_Get(car), 0x4d0);
    memcpy(s + 0x4d0, Car_Get(car)->s, CAR_SIZE);
    Progress_Save(car, s + 0x10f4);
    Progress_Reset(car);
}

/* 0x46c2a0: the snapshot into car: the damage again (Damage_Reset), the state block, with damage on
   the record (but its three pointer arrays, +0x00-+0xb3), the progress record (then started again),
   the weather's records (0x45e610) and the knocked scenery (0x4702a0) */
static void snap_restore(const uint8_t *s, int car)
{
    Car *c = Car_Get(car);
    Damage_Reset(c);
    car_restore(s + 0x4d0, c);
    if (CI32(c, 0xb50)) memcpy(Damage_Get(car) + 0xb4, s + 0xb4, 0x4d0 - 0xb4);
    Progress_Load(car, s + 0x10f4);
    Progress_Reset(car);
    Weather_Restart();
    Knock_Restore();
}

/* 0x46c320: a kind 1 snapshot: the car's +0x7a4, then the car reset on its start (0x42b800) with that
   word kept, launched and racing, its progress record (0x46c220) and the progress started again */
static void snap1_save(uint8_t *s, int car)
{
    Car *c = Car_Get(car);
    memcpy(s, c->s + 0x7a4, 4);
    Cars_Reset(car, 1, true);
    memcpy(c->s + 0x7a4, s, 4);
    if (CI32(c, 0xb48) != 1) Car_Launch(c);
    CW32(c, 0xb9c, 1);
    Progress_Save(car, s + 4);
    Progress_Reset(car);
}

/* 0x46c390: a kind 1 snapshot into car: the car reset on its start with the word put back, launched,
   the progress record, the weather's records (0x45e610) and the knocked scenery (0x4702a0); 0x4083f0
   after them is empty */
static void snap1_restore(const uint8_t *s, int car)
{
    Car *c = Car_Get(car);
    Cars_Reset(car, 1, true);
    memcpy(c->s + 0x7a4, s, 4);
    if (CI32(c, 0xb48) != 1) Car_Launch(c);
    CW32(c, 0xb9c, 1);
    Progress_Load(car, s + 4);
    Progress_Reset(car);
    Weather_Restart();
    Knock_Restore();
}

/* ---- kind 2, the network game's (16-byte frames, one every third step: the car's pose) ---- */
static uint32_t rdw(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void wrw(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }

/* 0x46d8d0: car c's pose into frame e: +0 the height (the frame's y), +4 and +6 x and z from the cell's
   centre (as the network state's), +8 the cell, +0xa and +0xc rows 0 and 2's angles, +0xe bits 0-4 the
   steering's share in 31sts, 5 the brake (+0xb54), 6 a part showing (the far body's or the second
   model's mask, +0x17c), 7 the throttle (+0x79c, as a bit 9 of the word), 3 the snap (+0xc14, then
   cleared), bits 5-7 of the high byte's low three the crash (+0xb46, then cleared) */
static void encode(Car *c, uint8_t *e)
{
    int cell = CI16(c, 0xb00);
    const Section *sec = &g_sec[cell];
    uint32_t w1 = (uint32_t)(uint16_t)Net_Q16(CI32(c, 0x30) - sec->x, Net_Kf(0x511338)) |
                  (uint32_t)(uint16_t)Net_Q16(CI32(c, 0x38) - sec->z, Net_Kf(0x511338)) << 16;
    int32_t r0[3] = {CI32(c, 0), CI32(c, 4), CI32(c, 8)}, r2[3] = {CI32(c, 0x20), CI32(c, 0x24), CI32(c, 0x28)};
    uint32_t w2 = (uint32_t)(uint16_t)cell | (uint32_t)Net_RowAngles(r0) << 16;   /* 0x4bac60 */
    uint32_t w3 = rdw(e + 0xc) & 0xffff0000u;
    w3 = (w3 & ~0xffffu) | Net_RowAngles(r2);                                    /* 0x4baca0 */
    int32_t den = CI16(c, 0xb16) * 0x1680, q = 0;
    if (den) q = (int32_t)(((int64_t)(CI16(c, 0xb10) * 0x1680) << 16) / den);   /* (the original divides by zero) */
    q = fx_mul(q + 0x10000, 0xf8000);
    if (q < 0) q = 0;
    else if (q > 0x1f0000) q = 0x1f0000;
    w3 = (w3 & 0xffe0ffffu) | (uint32_t)((q >> 16) & 0x1f) << 16;
    w3 = CI32(c, 0x79c) ? w3 | 0x2000000u : w3 & 0xfdffffffu;
    w3 = (w3 & 0xfeffffffu) | (uint32_t)(CI32(c, 0xb54) & 1) << 24;
    w3 = (w3 & 0xf7ffffffu) | (uint32_t)(CI32(c, 0xc14) & 1) << 27;
    CW32(c, 0xc14, 0);
    w3 = (w3 & 0xff1fffffu) | (uint32_t)(c->s[0xb46] & 7) << 21;
    c->s[0xb46] = 0;
    bool part = (c->far[0] && c->far[0]->mask) || (c->second && c->second->mask);
    w3 = (w3 & 0xfbffffffu) | (uint32_t)part << 26;
    wrw(e, (uint32_t)CI32(c, 0x34));
    wrw(e + 4, w1);
    wrw(e + 8, w2);
    wrw(e + 0xc, w3);
}

/* 0x46de20: frame e into a matrix (the place from the cell, the rows from their angles, row 1 their
   cross, unit) and the fields: the steering (-1..1), the throttle, the brake, the snap, the crash and
   the part */
static void decode(const uint8_t *e, int32_t m[16], int32_t *steer, int32_t *throttle, int32_t *brake, int32_t *snap,
                   int32_t *crash, int32_t *part)
{
    uint32_t w0 = rdw(e), w1 = rdw(e + 4), w2 = rdw(e + 8), w3 = rdw(e + 0xc);
    const Section *sec = &g_sec[(int16_t)w2];
    float fx = (float)((int16_t)w1 * Net_Kf(0x51137c) * Net_Kf(0x511368));
    float fz = (float)((int16_t)(w1 >> 16) * Net_Kf(0x51137c) * Net_Kf(0x511368));
    m[12] = (int32_t)llrint((double)fx * 65536.0) + sec->x;   /* 0x4bacc0 */
    m[13] = (int32_t)w0;
    m[14] = (int32_t)llrint((double)fz * 65536.0) + sec->z;
    int32_t r0[3], r2[3], r1[3];
    Net_RowFromAngles((uint8_t)(w2 >> 16), (uint8_t)(w2 >> 24), r0);
    Net_RowFromAngles(e[0xc], e[0xd], r2);
    r1[0] = fx_mul(r2[1], r0[2]) - fx_mul(r2[2], r0[1]);
    r1[1] = fx_mul(r2[2], r0[0]) - fx_mul(r2[0], r0[2]);
    r1[2] = fx_mul(r2[0], r0[1]) - fx_mul(r2[1], r0[0]);
    Vec_Unit(r1);
    for (int k = 0; k < 3; k++) m[k] = r0[k], m[4 + k] = r1[k], m[8 + k] = r2[k];   /* 0x4bace0-0x4bad20 */
    *steer = fx_mul((int32_t)((w3 >> 16 & 0x1f) << 16), 0x1083) - 0x10000;
    *throttle = (int32_t)(w3 >> 9 & 0x10000);
    *brake = (int32_t)(w3 >> 24 & 1);
    *snap = (int32_t)(w3 >> 27 & 1);
    *crash = (int32_t)((w3 >> 21 & 7) << 13);
    *part = (int32_t)(w3 >> 26 & 1);
}

/* the header's kind 2 fields: +0x44 the frame from, +0x84 the frame to, +0xc4 the change of velocity,
   +0xd0/+0xd4 the steering, +0xd8/+0xdc the throttle, +0xe0/+0xe4 the crash, +0xe8 the part, +0xec the
   brake, +0xf0/+0xf4 the snap (each from and to), +0xf8 the step of three */
static void get_m(const Replay *r, uint32_t o, int32_t m[16]) { for (uint32_t i = 0; i < 16; i++) m[i] = h32(r, o + 4 * i); }
static void put_m(Replay *r, uint32_t o, const int32_t m[16]) { for (uint32_t i = 0; i < 16; i++) w32(r, o + 4 * i, m[i]); }

static void decode_to(Replay *r, const uint8_t *e, uint32_t mo, uint32_t steer, uint32_t thr, uint32_t snap, uint32_t crash)
{
    int32_t m[16], a, b, c, d, f, g;
    get_m(r, mo, m);
    decode(e, m, &a, &b, &c, &d, &f, &g);
    put_m(r, mo, m);
    w32(r, steer, a), w32(r, thr, b), w32(r, 0xec, c), w32(r, snap, d), w32(r, crash, f), w32(r, 0xe8, g);
}

/* 0x46e340: the change of velocity that takes the car from the frame from to the frame to in three
   steps: (to - from - 3 v) / 6 */
static void velocity_step(Replay *r, const Car *c)
{
    for (uint32_t k = 0; k < 3; k++) {
        int32_t d = h32(r, 0x84 + 0x30 + 4 * k) - h32(r, 0x44 + 0x30 + 4 * k) - fx_mul(CI32(c, 0x408 + 4 * k), 0x30000);
        w32(r, 0xc4 + 4 * k, fx_mul(d, 0x2aaa));
    }
}

static const uint8_t *frame2(const Replay *r, int seg, int k) { return r->frames + ((size_t)per_seg(r) * seg + k) * 0x10; }

/* 0x40be60: the player's profile word the playing's +0xb88 is told from */
static int32_t profile_word(int p) { return gms32(0x532374 + gm16(0x5168f4 + 2 * (uint32_t)(p & 0xffff)) * 0x2f0); }

bool Replay_Start(Replay *r, int car)
{
    if (!r || h32(r, 0xc) || h32(r, 4)) return false;
    w32(r, 0, (uint8_t)car < 2 ? profile_word((uint8_t)car) : 0);
    r->b[0x20] = (uint8_t)car;
    w32(r, 0xc, 1);
    w16(r->b + 0x100, 0);
    w32(r, 0x10, 1);
    w16(count_of(r, 0), 0);
    if (kind(r) != 2) frame_of(r, 0, 0)[2] &= 0xc0;
    if (kind(r) == 0) {
        /* the first change: the process's next state (the go's event has been taken) */
        uint8_t *s = snap_of(r, 0);
        s[CHG0 + 4] = (uint8_t)(Race_ProcWord((uint8_t)car) >> 16);
        w16(s + CHG0, 0), w16(s + CHG0 + 2, 0);
        s[NCHG0] = 1;
        snap_save(s, (uint8_t)car);
    } else if (kind(r) == 1) {
        uint8_t *s = snap_of(r, 0);
        /* (kinds 1 and 2: the network game's from car 0's word) */
        s[CHG1 + 4] = (uint8_t)(Race_ProcWord(Gs_ModeBit19() ? 0 : (uint8_t)car) >> 16);
        w16(s + CHG1, 0), w16(s + CHG1 + 2, 0);
        s[NCHG1] = 1;
        snap1_save(s, (uint8_t)car);
    } else {
        /* kind 2: the change list as kind 1's; the car stepped again by itself (but the network game's
           others, whose poses come from outside) */
        uint8_t *s = snap_of(r, 0);
        s[CHG1 + 4] = (uint8_t)(Race_ProcWord(Gs_ModeBit19() ? 0 : (uint8_t)car) >> 16);
        w16(s + CHG1, 0), w16(s + CHG1 + 2, 0);
        s[NCHG1] = 1;
        if (!Gs_ModeBit19() || (uint8_t)car == 0) CW32(Car_Get((uint8_t)car), 0xc18, 0);
    }
    w32(r, 0x18, 0);
    r->b[0xf8] = 0;
    return true;
}

bool Replay_Stop(Replay *r)
{
    if (!r || !h32(r, 0xc)) return false;
    w32(r, 0xc, 0);
    if (h32(r, 0x10) && kind(r) != 2) {
        int s = h16(r->b + 0x100);
        w16(count_of(r, s), h16(count_of(r, s)) + 1);
        w16(r->b + 0x100, s + 1);
    } else if (kind(r) == 2)
        w16(r->b + 0x100, 1);
    w32(r, 0x10, 0);
    w32(r, 0x18, 0);
    return true;
}

bool Replay_End(Replay *r)
{
    if (!r || !h32(r, 4)) return false;
    w32(r, 4, 0), w32(r, 8, 0), w32(r, 0x14, 0);
    return true;
}

bool Replay_Play(Replay *r, int seg, int pre, int car)
{
    if (!r || h32(r, 0xc) || h32(r, 4) || h16(r->b + 0x100) <= seg) return false;
    r->b[0x20] = (uint8_t)car;
    w32(r, 4, 1);
    w16(r->b + 0x108, seg);
    w32(r, 8, 1);
    w16(r->b + 0x10a, pre ? -pre : 0);
    r->b[0x21] = 0;
    if (kind(r) == 0) snap_restore(snap_of(r, seg), (uint8_t)car);
    else if (kind(r) == 1) snap1_restore(snap_of(r, seg), (uint8_t)car);
    else {
        /* 0x46c410: the damage again, the progress started again, the weather's records, the knocked
           scenery */
        Car *c = Car_Get((uint8_t)car);
        Damage_Reset(c);
        Progress_Reset((uint8_t)car);
        Weather_Restart();
        Knock_Restore();
    }
    Mud_Reset();   /* 0x46ef20 */
    w32(r, 0x14, 0);
    r->b[0x10c] = kind(r) == 0 ? snap_of(r, seg)[CHG0 + 4] : snap_of(r, seg)[0x20];
    /* car 0's run with computer drivers or head to head (not networked): the cars' progress again
       (0x4207f0), the grid kept (0x4584d0(1)), the computer cars on it with their drivers going
       (0x42b800, 0x47bdc0), the grid's points and the sectors from the go (0x458100(0)) */
    if ((uint8_t)car == 0 && (Gs_SelBit26() || (Gs_SelBit27() && !Gs_ModeBit19()))) {
        Progress_ResetAll();
        Cars_Grid(true);
        if (Gs_SelBit26()) {
            Cars_Reset((int)Gs_SelBits14(), Race_Cars() - (int)Gs_SelBits14(), true);
            Ai_Go(false);
        }
        Cars_GridPoints();
        Sectors_Start(false);
    }
    r->b[0xf8] = 0;
    if (kind(r) == 2) {
        /* the first two frames (from and to), the car moved from outside (in the network game only
           car 0's: the others already are), still, its velocity's change, a snap */
        decode_to(r, frame2(r, seg, 0), 0x44, 0xd0, 0xd8, 0xf0, 0xe0);
        decode_to(r, frame2(r, seg, 1), 0x84, 0xd4, 0xdc, 0xf4, 0xe4);
        Car *c = Car_Get((uint8_t)car);
        if (!Gs_ModeBit19() || (uint8_t)car == 0) CW32(c, 0xc18, 1);
        for (uint32_t k = 0; k < 6; k++) CW32(c, 0x408 + 4 * k, 0);
        velocity_step(r, c);
        CW32(c, 0xbf8, 1);
    }
    return true;
}

/* the change list of segment seg: kind 0 in the snapshot's +0x110c (count +0x1148), kind 1 +0x1c (+0x58) */
static uint8_t *changes(const Replay *r, int seg, uint8_t **n)
{
    uint8_t *s = snap_of(r, seg);
    *n = s + (kind(r) == 0 ? NCHG0 : NCHG1);
    return s + (kind(r) == 0 ? CHG0 : CHG1);
}

/* 0x46c8e0 for one replay: the step's controls into the segment's frame (a new one when they differ or
   the count is full; at the segment's end the next segment, at the last the recording stopped), and
   a change of the car's race state noted at the frame and step it happened. A segment started on a
   step records nothing on it. */
static void record(Replay *r)
{
    int car = r->b[0x20];
    int active = h32(r, 0x10);
    int seg = h16(r->b + 0x100);
    if (active) {
        uint8_t *cnt = count_of(r, seg);
        uint8_t *e = frame_of(r, seg, h16(cnt));
        if (!same_frame(e, car)) {
            w16(cnt, h16(cnt) + 1);
            if (h16(cnt) == per_seg(r)) {
                w32(r, 0x10, 0);
                w16(cnt, h16(cnt) + 1);
                w16(r->b + 0x100, seg + 1);
                if (seg + 1 == segs(r)) Replay_Stop(r);
                return;   /* (the segment ended: none started on this step) */
            }
            e = frame_of(r, seg, h16(cnt));
            e[2] &= 0xc0;
        }
        pack(e, car);
        uint8_t st = (uint8_t)Race_ProcWord(Gs_ModeBit19() ? 0 : car);
        uint8_t *n, *ch = changes(r, seg, &n);
        if (st != ch[(*n - 1) * 6 + 4]) {
            uint8_t *c = ch + *n * 6;
            if (c + 6 > r->b + r->size) return;   /* (the original writes on past the list's ten) */
            c[4] = st;
            w16(c, h16(cnt));
            w16(c + 2, e[2] & 0x3f);
            ++*n;
            if (!h16(c + 2)) w16(c, h16(c) - 1), w16(c + 2, e[-2] & 0x3f);
            else w16(c + 2, h16(c + 2) - 1);
        }
        return;
    }
    /* a segment started */
    frame_of(r, seg, 0)[2] &= 0xc0;
    w16(count_of(r, seg), 0);
    w32(r, 0x18, 1);
    w32(r, 0x10, 1);
}

void Replay_RecordStep(void)
{
    for (int i = 0; i < 16; i++) {
        Replay *r = i < 8 ? made[i] : loaded[i - 8];
        if (r && h32(r, 0xc) && kind(r) != 2 && Car_Get(r->b[0x20])->s[0xb43]) record(r);
    }
}

/* 0x46cfa0: a step of the playing: a segment's start (the step taken by it), the snapshot held for the
   steps before (the wheels' spin cleared but on the last), the frame into the controls, the race state
   from the last change at or before the step, and at the segment's end the next, at the last the end */
static void play(Replay *r)
{
    Car *c = Car_Get(r->b[0x20]);
    if (!h32(r, 4) || kind(r) == 2 || !c->s[0xb43]) return;
    int seg = h16(r->b + 0x108);
    if (!h32(r, 8)) {
        w16(r->b + 0x10a, 0);
        r->b[0x21] = 0;
        w32(r, 0x14, 1);
        w32(r, 8, 1);
        return;
    }
    if (h16(r->b + 0x10a) < 0) {
        if (kind(r) == 0) snap_restore(snap_of(r, seg), r->b[0x20]);
        else snap1_restore(snap_of(r, seg), r->b[0x20]);
        if (h16(r->b + 0x10a) != -1) CW32(c, 0x860, 0), CW32(c, 0x864, 0), CW32(c, 0x868, 0), CW32(c, 0x86c, 0);
        w16(r->b + 0x10a, h16(r->b + 0x10a) + 1);
    }
    if (h16(r->b + 0x10a) < 0) return;
    if (h16(count_of(r, seg))) {
        if (unpack(r, frame_of(r, seg, h16(r->b + 0x10a)), r->b[0x20], &r->b[0x21]))
            w16(r->b + 0x10a, h16(r->b + 0x10a) + 1);
        int pos = h16(r->b + 0x10a), sub = r->b[0x21];
        uint8_t *n, *ch = changes(r, seg, &n);
        int k = 0;
        while (k < *n && !(pos < h16(ch + 6 * k) || (pos == h16(ch + 6 * k) && sub < h16(ch + 6 * k + 2)))) k++;
        if (k > 0) r->b[0x10c] = ch[6 * (k - 1) + 4];
    }
    if (h16(r->b + 0x10a) == h16(count_of(r, seg))) {
        w32(r, 8, 0);
        w16(r->b + 0x108, seg + 1);
        if (seg + 1 == h16(r->b + 0x100)) Replay_End(r);
    }
}

/* 0x46d270: each replay's step, but in the demo's state 6 */
void Replay_PlayStep(void)
{
    for (int i = 0; i < 16; i++) {
        Replay *r = i < 8 ? made[i] : loaded[i - 8];
        if (r && !(gm8(0x52ea52) && Gs_Mode() == 6)) play(r);
    }
}

/* 0x46d510: each kind 2 recording's car (in the race) every third step: its pose into the segment's
   next frame, the recording stopped when the segment is full (0x46cc60) */
void Replay_RecordStep2(void)
{
    for (int i = 0; i < 16; i++) {
        Replay *r = i < 8 ? made[i] : loaded[i - 8];
        if (!r || !h32(r, 0xc) || kind(r) != 2) continue;
        Car *c = Car_Get(r->b[0x20]);
        if (!c->s[0xb43]) continue;
        if (r->b[0xf8] == 0) {
            int seg = h16(r->b + 0x100);
            uint8_t *cnt = count_of(r, seg);
            if (h16(cnt) < per_seg(r)) {
                encode(c, (uint8_t *)frame2(r, seg, h16(cnt)));
                w16(cnt, h16(cnt) + 1);
            } else
                Replay_Stop(r);
        }
        if (++r->b[0xf8] > 2) r->b[0xf8] = 0;
    }
}

/* 0x46d610: a kind 2 playing's step: every third step the frame to becomes the frame from (its crash
   sounded, 0x418ba0), the next frame decoded (at the segment's end the playing ended), the change of
   velocity again, a snap noted (+0xbf8); each step the car's frame blended between them by the third
   (none after a snap), the steering and the throttle likewise, the velocity changed, the brake */
static void play2(Replay *r)
{
    Car *c = Car_Get(r->b[0x20]);
    if (!h32(r, 4) || kind(r) != 2 || !c->s[0xb43]) return;
    if (r->b[0xf8] > 2) {
        for (uint32_t i = 0; i < 16; i++) w32(r, 0x44 + 4 * i, h32(r, 0x84 + 4 * i));
        w32(r, 0xd8, h32(r, 0xdc));
        w32(r, 0xd0, h32(r, 0xd4));
        w32(r, 0xe0, h32(r, 0xe4));
        w32(r, 0xf0, h32(r, 0xf4));
        if (h32(r, 0xe0) > 0) Crash_Hit(CI8(c, 0xb1a), h32(r, 0xe0), CI8(c, 0xb1a));
        w16(r->b + 0x10a, h16(r->b + 0x10a) + 1);
        int seg = h16(r->b + 0x108);
        if (h16(r->b + 0x10a) < h16(count_of(r, seg))) decode_to(r, frame2(r, seg, h16(r->b + 0x10a)), 0x84, 0xd4, 0xdc, 0xf4, 0xe4);
        else Replay_End(r);
        velocity_step(r, c);
        r->b[0xf8] = 0;
        if (h32(r, 0xf0)) CW32(c, 0xbf8, 1);
    }
    int32_t k = h32(r, 0xf4) ? 0 : (int32_t)(((int64_t)r->b[0xf8] << 32) / 0x30000);
    int32_t a[16], b[16], o[16];
    get_m(r, 0x44, a);
    get_m(r, 0x84, b);
    for (uint32_t i = 0; i < 16; i++) o[i] = CI32(c, 4 * i);
    Frame_Blend(o, a, b, k, k, k, 1);   /* 0x4224e0 on +0x750 */
    for (uint32_t i = 0; i < 16; i++) CW32(c, 4 * i, o[i]);
    int32_t st = fx_mul(fx_mul(h32(r, 0xd4) - h32(r, 0xd0), k) + h32(r, 0xd0), CI16(c, 0xb16) * 0x1680);
    CW32(c, 0x7a4, 0);
    CW16(c, 0xb10, (uint16_t)(int16_t)X87_MulRound((double)st, Net_Kd(0x511300)));
    int32_t th = fx_mul(h32(r, 0xdc) - h32(r, 0xd8), k) + h32(r, 0xd8);
    CW32(c, 0x79c, fx_mul(CI32(c, 0x788), th));
    for (uint32_t q = 0; q < 3; q++) {
        CW32(c, 0x414 + 4 * q, CI32(c, 0x408 + 4 * q));
        CW32(c, 0x408 + 4 * q, CI32(c, 0x408 + 4 * q) + h32(r, 0xc4 + 4 * q));
    }
    CW32(c, 0xb54, h32(r, 0xec));
    r->b[0xf8]++;
}

/* 0x46d5e0: each kind 2 playing's step, but in the demo's state 6 */
void Replay_PlayStep2(void)
{
    for (int i = 0; i < 16; i++) {
        Replay *r = i < 8 ? made[i] : loaded[i - 8];
        if (r && !(gm8(0x52ea52) && Gs_Mode() == 6)) play2(r);
    }
}

Replay *Replay_Slot(int i) { return i < 0 || i >= 16 ? NULL : i < 8 ? made[i] : loaded[i - 8]; }

void Replay_Layout(const Replay *r, uint32_t *snap, uint32_t *frames, uint32_t *counts)
{
    *snap = (uint32_t)(r->snap - r->b), *frames = (uint32_t)(r->frames - r->b), *counts = (uint32_t)(r->counts - r->b);
}

bool Replay_Playing(const Replay *r) { return r && h32(r, 4); }
int32_t Replay_Word(const Replay *r, uint32_t o) { return r ? h32(r, o) : 0; }
bool Replay_Recording(const Replay *r) { return r && h32(r, 0xc); }
uint8_t Replay_State(const Replay *r) { return r ? r->b[0x10c] : 0; }
/* 0x46d3f0: the image written as a file (File_Save 0x4aa5a0; the port's user storage) */
bool Replay_Save(const Replay *r, const char *path)
{
    if (!r) return false;
    uint32_t n = image_size(segs(r), per_seg(r), (int)kind(r));
    plat_log(path);   /* (puts of the name and a blank line) */
    return plat_save_user_file(path, r->b, n <= r->size ? n : r->size);
}

const uint8_t *Replay_Image(const Replay *r, uint32_t *size) { if (size) *size = r ? r->size : 0; return r ? r->b : NULL; }
int Replay_Frames(const Replay *r, int seg) { return r ? h16(count_of(r, seg)) : 0; }

Replay *Race_Replay(int p) { return p >= 0 && p < 8 ? race[p] : NULL; }
void Race_SetReplay(int p, Replay *r) { if (p >= 0 && p < 8) race[p] = r; }

/* 0x41c1b0's name: <the stage's directory (0x41f8e0: 0x53823c)>\\CPU_Runs\\<rally><stage from 1><side>
   <difficulty>.rpl (0x519294), the rally from 0x5191c8 and the difficulty n, i or e (0x5191ec) by the
   level (mode 4, 0x405dd0, from 1) or the mode's (0x405d90). The run's member is in the stage's
   archive (jap111n.rpl). */
static Replay *cpu_run(uint32_t side)
{
    uint32_t d = Gs_Mode() == 4 ? (gm32(0x52afb4) >> 0x14 & 7) - 1 : Gs_Mode2();
    char path[0x104];
    snprintf(path, sizeof path, exe_str(0x519294), (const char *)gm(0x53823c), exe_str(exe_u32(0x5191c8 + 4 * Tod_Rally())),
             (int)Tod_Stage() + 1, (int)side, exe_str(exe_u32(0x5191ec + 4 * d)));
    return Replay_Load(path);
}

/* 0x41c1b0: car p's CPU run, the side (0x537f90) 0x4584c0's head-to-head start side (0x542cb0) for
   car 0, the other for car 1 */
static bool load_cpu_run(int p)
{
    uint32_t side = p == 0 ? gm32(0x542cb0) != 0 : gm32(0x542cb0) == 0;
    gw32(0x537f90, side);
    race[p] = cpu_run(side);
    return race[p] != NULL;
}

/* 0x41c510: the demo's run: the replays cleared, outside the arcade and the network's modes car 0's
   from the disc's <root>\\Game\\DemoRuns\\<rally><stage, two digits> (0x53874c, Race_Setup's name,
   0x51942c) with H.rpl, or L.rpl with the low-detail clouds; 0x538110 the run on */
void Replay_LoadDemo(void)
{
    memset(race, 0, sizeof race);
    snprintf((char *)gm(0x53874c), 0x104, exe_str(0x51942c), Path_Root(), exe_str(exe_u32(0x51931c + 4 * Tod_Rally())), (int)Tod_Stage() + 1);
    char path[0x104];
    snprintf(path, sizeof path, exe_str((gm32(0x52afd0) >> 0x1b & 3) == 0 ? 0x5192dc : 0x5192d4), (const char *)gm(0x53874c));
    uint32_t m = Gs_Mode();
    if (m == 5 || m == 6 || m == 7 || m == 11 || m == 12) return;
    race[0] = Replay_Load(path);
    gw32(0x538110, 1);
}

/* 0x41c260: the race's replays. With the CPU runs (bit 25, not networked) and bit 17 of the mode
   word: car 0's recording and car 1's run (as 0x41c1b0's, the side not noted) and nothing else. Else
   the CPU runs for the cars that are a computer driver's (0x408500; in mode 4 those without a pad) and
   a recording for each other player: 7,500 frames for one player alone (6,250 with two, in the
   championship or with the CPU runs; with the ghost, bit 28, 7,500 for one). The championship's final
   stage (0x407270) loads champend.rpl as car 0's (kind 1, 6,250 frames), which the go then records
   over. The network game's: kind 2 for car 0 and each other player. */
void Replay_Setup(void)
{
    memset(race, 0, sizeof race);
    if (Gs_ModeBit19()) {
        /* the network game: kind 2 recordings of 7,500 frames, car 0's and each player record's (not in
           the time trial, mode 10) */
        race[0] = Replay_New(1, 0x1d4c, 2);
        for (int i = 0; i < 7; i++) race[1 + i] = NetRec_Used(i) && Gs_Mode() != 10 ? Replay_New(1, 0x1d4c, 2) : NULL;
        return;
    }
    if (Gs_SuperSpecial()) {
        char path[0x104];
        snprintf(path, sizeof path, exe_str(0x5192b8), (const char *)gm(0x53823c));
        race[0] = Replay_Load(path);
        return;
    }
    int np = (int)Gs_SelBits14();
    bool cpu_runs = Gs_Mode() == 4 || Gs_SelBit25();
    bool run[2] = {false, false};
    if (!Gs_SelBit28()) {
        if (Gs_SelBit25() && Gs_ModeBit17()) {
            race[0] = Replay_New(1, 0x186a, 0);
            race[1] = cpu_run(gm32(0x542cb0) == 0);
            return;
        }
        for (int i = 0; i < 2; i++)
            if (((Gs_Mode() == 4 && (int8_t)gm8(0x53acc8 + (uint32_t)i) == -1) || (Gs_SelBit25() && Player_Driver(i) != -1)) &&
                load_cpu_run(i))
                run[i] = true;
    }
    for (int i = 0; i < np; i++) {
        if (run[i]) continue;
        int f = Gs_SelBit28() ? (np == 1 ? 0x1d4c : 0x186a) : (np != 1 || cpu_runs) ? 0x186a : 0x1d4c;
        race[i] = Replay_New(1, f, 0);
    }
    if (Ghost_On()) {
        /* the ghost: the stage's .grp (0x5192b0 on 0x41f920's name) */
        char path[0x104];
        snprintf(path, sizeof path, exe_str(0x5192b0), Ghost_StageName());
        Ghost_Load(path);   /* 0x465f90 */
    }
}
