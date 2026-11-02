/* The replays (see replay.h). The control block's fields are kept in the image at the original's
   offsets (so a saved file is the original's layout); its pointers (+0x24, +0x30, +0x3c, +0x40,
   +0x104 and the unread +0x28, +0x2c, +0x34, +0x38) are kept beside it. */
#include "game/replay.h"
#include "game/race.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/input.h"
#include "game/weather.h"
#include "sim/car.h"
#include "sim/damage.h"
#include "sim/knock.h"
#include "sim/progress.h"
#include "world/stage.h"
#include "bfl.h"
#include "pending.h"
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
    uint8_t *d = File_Load(path, 1);
    uint32_t size = File_LastSize();
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
    if ((int8_t)idx > 0 && Gs_SelBit27() && !Gs_ModeBit19() && Gs_SelBits14() == 1)
        PENDING_NOTE(0x456ae0, "the head to head's step rate");
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
        PENDING_NOTE(0x46c320, "Replay_Start (kind 1: the CPU runs' records)");
    } else {
        PENDING_NOTE(0x46c750, "Replay_Start (kind 2: the network game's poses)");
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
    else PENDING_NOTE(0x46c390, "Replay_Play (kinds 1 and 2: the car reset through 0x42b800)");
    /* 0x46ef20: the clouds' texture again (the clouds are not ported) */
    w32(r, 0x14, 0);
    r->b[0x10c] = kind(r) == 0 ? snap_of(r, seg)[CHG0 + 4] : snap_of(r, seg)[0x20];
    if ((uint8_t)car == 0 && (Gs_SelBit26() || (Gs_SelBit27() && !Gs_ModeBit19())))
        PENDING_NOTE(0x46cce0, "Replay_Play (the other cars' progress, splits and states with car 0's)");
    r->b[0xf8] = 0;
    if (kind(r) == 2) PENDING_NOTE(0x46de20, "Replay_Play (kind 2: the network game's poses)");
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
        else PENDING_NOTE(0x46c390, "the replay's kind 1 restore");
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

bool Replay_Playing(const Replay *r) { return r && h32(r, 4); }
bool Replay_Recording(const Replay *r) { return r && h32(r, 0xc); }
uint8_t Replay_State(const Replay *r) { return r ? r->b[0x10c] : 0; }
const uint8_t *Replay_Image(const Replay *r, uint32_t *size) { if (size) *size = r ? r->size : 0; return r ? r->b : NULL; }
int Replay_Frames(const Replay *r, int seg) { return r ? h16(count_of(r, seg)) : 0; }

Replay *Race_Replay(int p) { return p >= 0 && p < 8 ? race[p] : NULL; }
void Race_SetReplay(int p, Replay *r) { if (p >= 0 && p < 8) race[p] = r; }

/* 0x41c260: the race's replays. One per player, 7,500 frames for one player alone (6,250 with two, in
   the championship or with the CPU runs; with the ghost, bit 28, 7,500 for one). The network game's,
   the super special's run (champend.rpl), the CPU runs (bit 25) and the ghost's .grp are not ported. */
void Replay_Setup(void)
{
    memset(race, 0, sizeof race);
    if (Gs_ModeBit19()) {
        PENDING_NOTE(0x41c260, "Replay_Setup (the network game's, kind 2)");
        return;
    }
    if (Gs_SuperSpecial()) {
        PENDING_NOTE(0x41c260, "Replay_Setup (the super special's champend.rpl, kind 1)");
        return;
    }
    int np = (int)Gs_SelBits14();
    bool cpu_runs = Gs_Mode() == 4 || Gs_SelBit25();
    if (!Gs_SelBit28() && Gs_SelBit25()) PENDING_NOTE(0x41c1b0, "Replay_Setup (the CPU runs' .rpl)");
    for (int i = 0; i < np; i++) {
        int f = Gs_SelBit28() ? (np == 1 ? 0x1d4c : 0x186a) : (np != 1 || cpu_runs) ? 0x186a : 0x1d4c;
        race[i] = Replay_New(1, f, 0);
    }
    if (Gs_SelBit28() && Gs_Bit30()) PENDING_NOTE(0x465f90, "Replay_Setup (the ghost's .grp)");
}
