/* The network game's records of the other players (0x409b60-0x40b1e0; docs/re/network.md): eight of
   0x80 bytes at 0x531778, each a player's DirectPlay id (+0), flags (+4: the car in bits 0-4, bit 5,
   bit 6, in use bit 7, ready 8, finished 9, the state in bits 18-21, bit 22, bit 23), the car slot
   (+8), a count (+0xc), the last split message (+0x10, 0x1e bytes, new at +0x2e), the stage times
   (+0x2c + 4k), the lap times (+0x48 + 4k), the total (+0x74), the best total (+0x78) and the time
   (+0x7c); the bests (0x531f80, 0x531c94/0x531c98) and their holder's name (0x531cf0); the stage's and
   the rally's places (0x531ec0, 0x531e00: eight of 0x18 bytes) and the place numbers (0x531dd8,
   0x531b78); the race places (0x531bb8: eight of 0x1c bytes); the lobby list (0x5320b0: seven of
   0x10); the start order (0x531cc0). All in the game state at those addresses. */
#include "net/records.h"
#include "net/session.h"
#include "crt.h"
#include "exe.h"
#include "game/gstate.h"
#include "game/hud.h"
#include "game/mem.h"
#include "game/profile.h"
#include "game/race.h"
#include "game/select.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { REC = 0x531778, NREC = 8, EMPTY = 0x52ea5c, BEST_NAME = 0x531cf0 };

static uint32_t rec(int i) { return REC + 0x80u * (uint32_t)i; }
static uint32_t flags(int i) { return gm32(rec(i) + 4); }
static void gstr(uint32_t va, const char *s) { size_t n = strlen(s); memcpy(gm(va), s, n + 1); }
static uint32_t r32(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static int32_t rs32(const void *p) { int32_t v; memcpy(&v, p, 4); return v; }
static uint8_t mode(void) { return (uint8_t)Gs_Mode(); }

/* the record in use with the id, or -1 (the original's loops over the first seven, 0x531af8) */
static int find_used(uint32_t id)
{
    for (int i = 0; i < 7; i++)
        if (flags(i) & 0x80 && gm32(rec(i)) == id) return i;
    return -1;
}

/* 0x409b60: the bests and their holder */
void NetRec_ClearBests(void)
{
    memset(gm(0x531f84), 0, 8 * 4);
    memset(gm(0x531c98), 0, 10 * 4);
    gw32(0x5320a8, 0), gw32(0x531ce0, 0);
    gstr(BEST_NAME, (const char *)gm(EMPTY));
    gw32(0x531ce4, 0);
}

/* 0x409bc0: nobody ready */
void NetRec_ClearReady(void)
{
    for (int i = 0; i < NREC; i++) gw32(rec(i) + 4, flags(i) & ~0x100u);
}

/* 0x409be0: the local player's data (0x10 bytes) */
void NetRec_SendData(const void *data) { Net_SetPlayerData(data, 0x10); }

/* 0x409bf0: a player's data (DirectPlay id, then 0x10 bytes: the id, the flags, the slot, the count)
   into its record, or (add) into the last free one; not the local player's */
void NetRec_Take(uint32_t id, const uint8_t *data, bool add)
{
    if (Net_LocalPlayer() == id) return;
    int i = 6;
    while (i >= 0 && !(flags(i) & 0x80 && gm32(rec(i)) == id)) i--;
    if (i < 0) {
        if (!add) return;
        i = 6;
        while (gm8(rec(i) + 4) & 0x80)
            if (--i < 0) return;
    }
    for (uint32_t k = 0; k < 16; k += 4) gw32(rec(i) + k, r32(data + k));
    gw32(rec(i), id);
}

/* 0x409c80: a player gone */
void NetRec_Remove(uint32_t id)
{
    for (int i = 0; i < 7; i++)
        if (gm32(rec(i)) == id) gw32(rec(i) + 4, flags(i) & ~0x80u);
}

bool NetRec_Used(int i) { return flags(i) >> 7 & 1; }   /* 0x409cb0 */

/* 0x409cd0: a record's player's long name, if it is one of the session's */
const char *NetRec_Name(int i)
{
    uint8_t slot;
    if (!Net_PlayerSlot(gm32(rec(i)), &slot)) return NULL;
    return Net_PlayerLongName(slot);
}

uint32_t NetRec_Car(int i) { return flags(i) & 0x1f; }        /* 0x409d00 */
uint32_t NetRec_Id(int i) { return gm32(rec(i)); }            /* 0x409d20 */
bool NetRec_Bit5(int i) { return flags(i) >> 5 & 1; }         /* 0x409d30 */

/* 0x409d50 (message 0xb): a player's splits, kept if newer than the last (when 0x539cc8 is set); the
   count goes up for each record with the id before the one in use */
void NetRec_Splits(uint32_t id, const uint8_t *splits)
{
    int i = 0;
    for (;; i++) {
        if (i >= 7) return;
        if (gm32(rec(i)) == id) gw32(rec(i) + 0xc, gm32(rec(i) + 0xc) + 1);
        if (flags(i) & 0x80 && gm32(rec(i)) == id && !(flags(i) & 0x400000)) break;
    }
    if (!gm8(0x539cc8)) return;
    uint16_t at;
    memcpy(&at, splits, 2);
    if (gm16(rec(i) + 0x10) < at) {
        memcpy(gm(rec(i) + 0x10), splits, 0x1e);
        gw8(rec(i) + 0x2e, 1);
    }
}

/* 0x409dd0 */
void NetRec_ClearSplits(void)
{
    for (int i = 0; i < 8; i++) gw16(rec(i) + 0x10, 0);
}
bool NetRec_SplitsNew(int i) { return gm8(rec(i) + 0x2e) != 0; }   /* 0x409df0 */
void NetRec_SplitsSeen(int i) { gw8(rec(i) + 0x2e, 0); }           /* 0x409e00 */
uint32_t NetRec_SplitsAt(int i) { return rec(i) + 0x10; }          /* 0x409e20 */

/* 0x409ab0: the records' race state cleared (keep: the ready flags kept), the rally results' rows,
   and the totals (totals) */
void NetRec_Clear(bool keep, bool totals)
{
    for (int i = 0; i < NREC; i++) {
        uint32_t r = rec(i), f = flags(i) & ~0x200u, e = 0x531e00 + 0x18u * (uint32_t)i;
        if (!keep) f &= ~0x100u;
        gw32(r + 4, f);
        gw32(r + 0xc, 0);
        gw32(e, 0xffffffff);
        memset(gm(r + 0x30), 0, 7 * 4);
        memset(gm(r + 0x4c), 0, 10 * 4);
        memset(gm(r + 0x10), 0, 7 * 4);
        gw32(e + 8, 0), gw32(e + 4, 0xffffffff);
        gw16(r + 0x2c, 0);
    }
    if (totals) gw32(0x531ce8, 0);
    uint8_t m = mode();
    if (m == 8 || m == 9 || m == 11) NetRec_ClearBests();
    gw32(0x535b8c, 0);   /* 0x40e890 */
    Sel_ModeFlags();     /* 0x408020 */
}

/* 0x409e30: the finished and ready flags and the split index cleared (and the best totals) */
void NetRec_ResetRace(bool totals, bool bests)
{
    for (int i = 0; i < 7; i++) {
        uint32_t r = rec(i);
        if (!(flags(i) & 0x80)) continue;
        gw32(r + 4, flags(i) & ~0x300u);
        gw16(r + 0x28, gm16(r + 0x28) & 0xfc00);
        if (bests) gw32(r + 0x78, 0xffffffff);
    }
    if (totals) gw32(0x531ce8, 0);
    if (bests) gw32(0x531df8, 0xffffffff);
}

/* 0x409e90 (message 7): ready (bit 22 cleared) */
void NetRec_SetReady(uint32_t id)
{
    int i = find_used(id);
    if (i >= 0) gw32(rec(i) + 4, (flags(i) & ~0x400000u) | 0x100);
}
bool NetRec_Ready(int i) { return flags(i) >> 8 & 1; }   /* 0x409ee0 */

/* 0x409f00 (message 10): finished, with the total and the time; the best total kept */
void NetRec_Finished(uint32_t id, uint32_t total, uint32_t time)
{
    int i = find_used(id);
    if (i < 0) return;
    uint32_t r = rec(i);
    gw32(r + 4, flags(i) | 0x200);
    gw32(r + 0x74, total);
    gw32(r + 0x7c, time);
    if (gm32(r + 0x74) < gm32(r + 0x78) || gm32(r + 0x78) == 0xffffffff) gw32(r + 0x78, gm32(r + 0x74));
}

/* 0x409f80 (message 10's other half): finished */
void NetRec_SetFinished(uint32_t id)
{
    int i = find_used(id);
    if (i >= 0) gw32(rec(i) + 4, flags(i) | 0x200);
}

/* the best's holder: a record's name, or none */
static void best_name(int i)
{
    const char *n = i >= 0 ? NetRec_Name(i) : NULL;
    gstr(BEST_NAME, n ? n : (const char *)gm(EMPTY));
}

/* 0x409fd0 (message 8): a player's stage time k; the best of each, and the holder of the last
   (split 8, or 2 when bit 25 of the selection is set) */
void NetRec_StageTime(uint32_t id, int k, uint32_t t)
{
    int i = find_used(id);
    if (i < 0) return;
    gw32(rec(i) + 0x2c + 4u * (uint32_t)k, t);
    uint32_t b = 0x531f80 + 4u * (uint32_t)k;
    if (!(t < gm32(b) || gm32(b) == 0)) return;
    gw32(b, t);
    int j = NetRec_Find(id);
    if (Gs_SelBit25() ? k == 2 : k == 8) best_name(j);
}

/* 0x40a0e0 (message 9): a player's lap time (mode 12: the best lap and its holder, the second best) */
void NetRec_LapTime(uint32_t id, int k, uint32_t t)
{
    int i = find_used(id);
    if (i < 0) return;
    if (mode() == 12) {
        gw32(rec(i) + 0x4c, t);
        if (t < gm32(0x531c98) || gm32(0x531c98) == 0) {
            gw32(0x531ce0, gm32(0x531c98));
            gw32(0x531c98, t);
            gw32(0x531ce4, 0);
            best_name(NetRec_Find(id));
        } else if (t < gm32(0x531ce0) || gm32(0x531ce0) == 0)
            gw32(0x531ce0, t);
        return;
    }
    uint32_t b = 0x531c94 + 4u * (uint32_t)k, best = gm32(b);
    gw32(rec(i) + 0x48 + 4u * (uint32_t)k, t);
    if (t < best || best == 0) gw32(b, t);
}

/* 0x40a230: the local player's splits 1..n against the bests (the holder the local player's name) */
void NetRec_LocalTimes(int n)
{
    int last = Gs_SelBit25() ? 2 : 8;
    for (int k = 1; k <= n; k++) {
        uint32_t t = (uint32_t)Times_Split(0, k), b = 0x531f80 + 4u * (uint32_t)k;
        if (t < gm32(b) || gm32(b) == 0) {
            gw32(b, t);
            if (k == last) gstr(BEST_NAME, Player_Name(0));
        }
    }
    if (n != last) return;
    uint32_t t = (uint32_t)Times_Split(0, last);
    if (gm32(0x5320a8) <= t && gm32(0x5320a8) != 0) return;
    gw32(0x5320a8, t);
}

/* 0x40a330: the local player's lap time k (mode 12: the best lap, held by the local player) */
void Lap_Best(uint32_t t, int k)
{
    if (mode() != 12) {
        uint32_t b = 0x531c94 + 4u * (uint32_t)k;
        if (t < gm32(b) || gm32(b) == 0) gw32(b, t);
        return;
    }
    if (gm32(0x531c98) <= t && gm32(0x531c98) != 0) {
        gw32(0x531ce4, 0);
        return;
    }
    gw32(0x531ce0, gm32(0x531c98));
    gw32(0x531c98, t);
    gstr(BEST_NAME, Player_Name(0));
    gw32(0x531ce4, 1);
}

uint32_t NetRec_LocalBest(void) { return gm32(0x531ce4); }    /* 0x40a3c0 */
uint32_t NetRec_BestTotal(void) { return gm32(0x5320a8); }    /* 0x40a3d0 */
void Trial_BestLap(uint32_t t) { if (t < gm32(0x5320a8) || gm32(0x5320a8) == 0) gw32(0x5320a8, t); }   /* 0x40a3e0 */
const char *NetRec_BestName(void) { return (const char *)gm(BEST_NAME); }   /* 0x40a400 */
uint32_t NetRec_BestSplit(int k) { return gm32(0x531f80 + 4u * (uint32_t)k); }   /* 0x40a410 */
uint32_t NetRec_BestLap(int k) { return mode() == 12 ? gm32(0x531c98) : gm32(0x531c94 + 4u * (uint32_t)k); }   /* 0x40a420 */
uint32_t NetRec_SecondLap(void) { return gm32(0x531ce0); }    /* 0x40a440 */
bool NetRec_Done(int i) { return flags(i) >> 9 & 1; }         /* 0x40a450 */
bool NetRec_Bit6(int i) { return flags(i) >> 6 & 1; }         /* 0x40a470 */

/* 0x4591e0: a car's place on a circuit (sector, distance) carried over the lap: for a car of side 0
   (0x542cb4 by its slot; -2 the local car), the distance plus the circuit's length (0x542c70) times the
   double at 0x5113b8, at the x87's precision: past the length, the next sector and the rest */
void Net_LapFold(int32_t index, uint32_t sector, uint32_t dist)
{
    uint32_t slot = index == -2 ? 0 : NetRec_Slot(index);
    if (gm32(0x542cb4 + 4 * slot)) return;
    float len = (float)gms32(0x542c70);
    uint64_t bits = (uint64_t)exe_u32(0x5113b8) | (uint64_t)exe_u32(0x5113bc) << 32;
    double k;
    memcpy(&k, &bits, 8);
    long double v = (long double)gms32(0x542c70) * k + (long double)gms32(dist);
    if (v >= (long double)len) {
        gw32(sector, gm32(sector) + 1);
        gw32(dist, (uint32_t)(int64_t)nearbyintl(v - (long double)len));
        return;
    }
    gw32(dist, (uint32_t)(int64_t)nearbyintl(v));
}

void NetRec_SetSlot(int i, uint32_t slot) { gw32(rec(i) + 8, slot); }   /* 0x40aff0 */
uint32_t NetRec_Slot(int i) { return gm32(rec(i) + 8); }               /* 0x40b010 */

/* 0x40a490: the race places' order: the ones in use first, the finished (+0x14) by their time
   (+0x18), the others by the sector (+0xc), the distance (+4), +8, then the index (+0x10) */
static int cmp_places(const void *pa, const void *pb)
{
    const uint8_t *a = pa, *b = pb;
    int32_t a0 = rs32(a), b0 = rs32(b);
    if (a0 != -1 && b0 == -1) return -1;
    if (a0 == -1 && b0 != -1) return 1;
    if (a0 == -1 && b0 == -1) return 0;
    bool fa = a[0x14] != 0, fb = b[0x14] != 0;
    if (fa && !fb) return -1;
    if (!fa && fb) return 1;
    if (fa && fb) {
        if (r32(a + 0x18) < r32(b + 0x18)) return -1;
        if (r32(b + 0x18) < r32(a + 0x18)) return 1;
    } else {
        if (rs32(b + 0xc) < rs32(a + 0xc)) return -1;
        if (rs32(a + 0xc) < rs32(b + 0xc)) return 1;
        if (rs32(b + 4) < rs32(a + 4)) return -1;
        if (rs32(a + 4) < rs32(b + 4)) return 1;
        if (rs32(b + 8) < rs32(a + 8)) return -1;
        if (rs32(a + 8) < rs32(b + 8)) return 1;
    }
    return r32(b + 0x10) < r32(a + 0x10) ? -1 : 1;
}

/* 0x40a580: the race places: each record from its last splits (the sector, the distance in tenths of
   a metre (bits 10-15 times 0x19645 in 16.16, at most 99), the signed lap part), then the local
   player's (index -2) with its sector, distance and lap; sorted */
void NetRec_Places(int32_t sector, int32_t dist, int32_t lap)
{
    int n = 0;
    for (; n < 7; n++) {
        uint32_t e = 0x531bb8 + 0x1cu * (uint32_t)n, r = rec(n);
        if (!(flags(n) & 0x80)) {
            gw32(e, 0xffffffff), gw32(e + 4, 0xffffffff);
            continue;
        }
        gw32(e, (uint32_t)n);
        uint16_t s = gm16(r + 0x26);
        gw32(e + 4, gm16(r + 0x28) & 0x3ff);
        int64_t m = (int64_t)(int32_t)((uint32_t)(s >> 10) << 16) * 0x19645;
        int32_t d = (int32_t)(m >> 16) >> 16;
        if (d > 99) d = 99;
        gw32(e + 8, (uint32_t)d);
        uint16_t l = gm16(r + 0x2a);
        int32_t lp = (l & 0x7800) >> 11;
        if (l & 0x8000) lp = -lp;
        gw32(e + 0xc, (uint32_t)lp);
        gw32(e + 0x10, gm32(r));
        gw8(e + 0x14, (uint8_t)(flags(n) >> 9 & 1));
        gw32(e + 0x18, gm32(r + 0x74));
        if (Gs_SelBit25()) Net_LapFold((int32_t)gm32(e), e + 0xc, e + 4);
    }
    uint32_t e = 0x531bb8 + 0x1cu * (uint32_t)n;
    gw32(e, 0xfffffffe);
    gw32(e + 4, (uint32_t)sector), gw32(e + 8, (uint32_t)dist), gw32(e + 0xc, (uint32_t)lap);
    gw32(e + 0x10, Net_LocalPlayer());
    gw8(e + 0x14, (Race_ProcWord(0) & 0xff) >= 9);
    gw32(e + 0x18, (uint32_t)Times_Finish(0));
    if (Gs_SelBit25()) Net_LapFold((int32_t)gm32(e), e + 0xc, e + 4);
    crt_qsort(gm(0x531bb8), 8, 0x1c, cmp_places);
}

uint32_t NetRec_PlaceIndex(int p) { return gm32(0x531bb8 + 0x1cu * (uint32_t)p); }   /* 0x40a700 */
/* 0x40a720, 0x40a760: the place's sector and distance for a player id */
uint32_t NetRec_PlaceSector(uint32_t id)
{
    for (uint32_t p = 0; p < 8; p++)
        if (gm32(0x531bc8 + 0x1c * p) == id) return gm32(0x531bbc + 0x1c * p);
    return 0;
}
uint32_t NetRec_PlaceDist(uint32_t id)
{
    for (uint32_t p = 0; p < 8; p++)
        if (gm32(0x531bc8 + 0x1c * p) == id) return gm32(0x531bc0 + 0x1c * p);
    return 0;
}

/* 0x40a7a0: the record with the id (in use or not), or -1 */
int NetRec_Find(uint32_t id)
{
    for (int i = 0; i < 7; i++)
        if (gm32(rec(i)) == id) return i;
    return -1;
}

/* 0x40a7d0: the results' order: by the time (+4), then +8; the empty last */
static int cmp_times(const void *pa, const void *pb)
{
    const uint8_t *a = pa, *b = pb;
    if (rs32(a) == -1 || r32(a + 4) == 0xffffffff) return 1;
    if (rs32(b) != -1 && r32(b + 4) != 0xffffffff) {
        if (r32(b + 4) < r32(a + 4)) return 1;
        if (r32(b + 4) <= r32(a + 4)) return r32(b + 8) < r32(a + 8) ? 1 : -1;
    }
    return -1;
}

/* the places numbered (equal times share one) */
static void number(uint32_t rows, uint32_t places)
{
    int place = 1;
    gw32(places, 1);
    for (uint32_t p = 1; p < 8; p++) {
        uint32_t c = rows + 0x18 * p;
        if (gm32(c) == 0xffffffff) return;
        if (gm32(c + 4) != gm32(c - 0x18 + 4)) place++;
        gw32(places + 4 * p, (uint32_t)place);
    }
}

/* 0x40a820: the stage's results (rows of 0x18 bytes: the record, the time, the id, the name at +0xc,
   the car at +0x10): each player's time (+0x74) and the local player's, sorted, and the places. The
   name is copied whole at +0xc, over the car and on into the next row, as the original does (the car
   written before it for the others, after it for the local player). */
void NetRec_StageResults(uint32_t t)
{
    gw32(0x531cec, 1);
    int i = 0;
    for (; i < 7; i++) {
        uint32_t e = 0x531ec0 + 0x18u * (uint32_t)i;
        const char *name = NULL;
        if (flags(i) & 0x80) {
            gw32(e, (uint32_t)i), gw32(e + 4, gm32(rec(i) + 0x74)), gw32(e + 8, gm32(rec(i)));
            gw32(e + 0x10, NetRec_Car(i));
            name = NetRec_Name(i);
        }
        if (!name) {
            gw32(e, 0xffffffff);
            continue;
        }
        gstr(e + 0xc, name);
        gw32(0x531cec, gm32(0x531cec) + 1);
    }
    uint32_t e = 0x531ec0 + 0x18u * (uint32_t)i;
    gw32(e, 0xfffffffe), gw32(e + 4, t), gw32(e + 8, Net_LocalPlayer());
    gstr(e + 0xc, Player_Name(0));
    gw32(e + 0x10, Player_Car(0) & 0xff);
    crt_qsort(gm(0x531ec0), 8, 0x18, cmp_times);
    number(0x531ec0, 0x531dd8);
}

/* 0x40a980: the rally's results: the stage's time added to the local total (0x531ce8), each player's
   total (+0x7c... the record's +0x7c, its id, the car) and the local player's, sorted, the places, and
   the points (+0x14: 3 for a win, 0 otherwise); the names as 0x40a820's */
void NetRec_RallyResults(uint32_t t)
{
    gw32(0x531ce8, gm32(0x531ce8) + t);
    int i = 0;
    for (; i < 7; i++) {
        uint32_t e = 0x531e00 + 0x18u * (uint32_t)i;
        const char *name = flags(i) & 0x80 ? NetRec_Name(i) : NULL;
        if (!name) {
            gw32(e, 0xffffffff);
            continue;
        }
        gstr(e + 0xc, name);
        gw32(e, (uint32_t)i), gw32(e + 4, gm32(rec(i) + 0x7c)), gw32(e + 8, gm32(rec(i)));
        gw32(e + 0x10, NetRec_Car(i));
    }
    uint32_t e = 0x531e00 + 0x18u * (uint32_t)i;
    gw32(e, 0xfffffffe), gw32(e + 4, gm32(0x531ce8)), gw32(e + 8, Net_LocalPlayer());
    gstr(e + 0xc, Player_Name(0));
    gw32(e + 0x10, Player_Car(0) & 0xff);
    crt_qsort(gm(0x531e00), 8, 0x18, cmp_times);
    /* the places (0x531b78), stopping at the first empty row, then each row's points */
    int place = 1;
    gw32(0x531b78, 1);
    for (uint32_t p = 1; p < 8; p++) {
        uint32_t c = 0x531e00 + 0x18 * p;
        if (gm32(c) == 0xffffffff) break;
        if (gm32(c + 4) != gm32(c - 0x18 + 4)) place++;
        gw32(0x531b78 + 4 * p, (uint32_t)place);
    }
    for (uint32_t p = 0; p < 8; p++) gw32(0x531e14 + 0x18 * p, gm32(0x531b78 + 4 * p) == 1 ? 3 : 0);
}

uint32_t NetRec_Results(void) { return gm32(0x531cec); }   /* 0x40ab10 */
/* 0x40ab20-0x40abe0: a results row's place, record, time, name and car (rally: the rally's) */
uint32_t NetRec_ResultPlace(int p, bool rally) { return rally ? gm32(0x531b78 + 4u * (uint32_t)p) : gm32(0x531dd8 + 4u * (uint32_t)p); }
uint32_t NetRec_ResultIndex(int p, bool rally) { return gm32((rally ? 0x531e00 : 0x531ec0) + 0x18u * (uint32_t)p); }
uint32_t NetRec_ResultTime(int p, bool rally) { return gm32((rally ? 0x531e04 : 0x531ec4) + 0x18u * (uint32_t)p); }
const char *NetRec_ResultName(int p, bool rally) { return (const char *)gm((rally ? 0x531e0c : 0x531ecc) + 0x18u * (uint32_t)p); }
uint32_t NetRec_ResultCar(int p, bool rally) { return gm32((rally ? 0x531e10 : 0x531ed0) + 0x18u * (uint32_t)p); }
uint32_t NetRec_ResultPoints(int p) { return gm32(0x531e14 + 0x18u * (uint32_t)p); }   /* 0x40ac10 */
uint32_t NetRec_Total(void) { return gm32(0x531ce8); }    /* 0x40ac30 */

/* 0x40ac40: message 6, the local player's state */
void Net_SendState(uint8_t v)
{
    uint8_t m[2] = {6, v};
    Net_Send(0, true, m, 2);
}

/* 0x40ac70 (message 6): a player's state (bits 18-21) */
void NetRec_SetState(uint32_t id, uint32_t v)
{
    if (Net_LocalPlayer() == id) return;
    int i = find_used(id);
    if (i >= 0) gw32(rec(i) + 4, (v & 0xf) << 18 | (flags(i) & 0xffc3ffffu));
}

/* 0x40acd0: the lobby's order: by the best total (+8; none last), then +0xc */
static int cmp_lobby(const void *pa, const void *pb)
{
    const uint8_t *a = pa, *b = pb;
    uint32_t x = r32(a + 8), y = r32(b + 8);
    if (x == 0xffffffff && y != 0xffffffff) return 1;
    if (y == 0xffffffff && x != 0xffffffff) return -1;
    if (y < x) return 1;
    if (x < y) return -1;
    return r32(b + 0xc) < r32(a + 0xc) ? 1 : -1;
}

/* 0x40ad20: the lobby's list (rows of 0x10: the name, the state, the best total, the id): the players
   in use with a name, the local player (state 3), the rest emptied, sorted. The names are copied whole
   at +0 over the row's other fields, as the original does; the count is 0x531f80 (which is also the
   best splits' slot 0). */
void NetRec_Lobby(void)
{
    int n = 0;
    gw32(0x531f80, 0);
    for (int i = 0; i < 7; i++) {
        uint32_t f = flags(i);
        if (!(f & 0x80)) continue;
        uint32_t e = 0x5320b0 + 0x10u * (uint32_t)n;
        gw32(e + 0xc, gm32(rec(i)));
        gw32(e + 4, f >> 18 & 0xf);
        gw32(e + 8, gm32(rec(i) + 0x78));
        const char *name = NetRec_Name(i);
        n = (int)gm32(0x531f80);
        if (name) {
            gstr(0x5320b0 + 0x10u * (uint32_t)n, name);
            gw32(0x531f80, (uint32_t)++n);
        }
    }
    uint32_t e = 0x5320b0 + 0x10u * (uint32_t)n;
    gw32(e + 4, 3);
    gw32(e + 8, gm32(0x531df8));
    gstr(0x5320b0 + 0x10 * gm32(0x531f80), Player_Name(0));
    uint32_t count = gm32(0x531f80) + 1;
    gw32(0x5320bc + 0x10 * gm32(0x531f80), Net_LocalPlayer());
    gw32(0x531f80, count);
    for (uint32_t k = count; k < 7; k++) {
        uint32_t r = 0x5320b0 + 0x10 * k;
        gw32(r + 4, 0);
        gw32(r + 8, 0xffffffff);
        gstr(r, (const char *)gm(EMPTY));
        gw32(r + 0xc, 0);
    }
    crt_qsort(gm(0x5320b0), count, 0x10, cmp_lobby);
}

uint32_t NetRec_LobbyCount(void) { return gm32(0x531f80); }                                   /* 0x40ae90 */
const char *NetRec_LobbyName(int k) { return (const char *)gm(0x5320b0 + 0x10u * (uint32_t)k); }  /* 0x40aea0 */
uint32_t NetRec_LobbyState(int k) { return gm32(0x5320b4 + 0x10u * (uint32_t)k); }           /* 0x40aeb0 */
uint32_t NetRec_LobbyBest(int k) { return gm32(0x5320b8 + 0x10u * (uint32_t)k); }            /* 0x40aec0 */
/* 0x40aed0: the local player's row (0 if not found) */
int NetRec_LobbyLocal(void)
{
    for (int k = 0; k < 8; k++)
        if (gm32(0x5320bc + 0x10u * (uint32_t)k) == Net_LocalPlayer()) return k;
    return 0;
}

/* 0x40af40: message 0x10 */
void Net_SendStart(void)
{
    uint8_t m = 0x10;
    Net_Send(0, true, &m, 1);
}

/* 0x40af60: message 0x11, the leaderboard (0x104 bytes at 0x533900, if there is one) */
void Net_SendBoard(void)
{
    uint8_t m[0x108];
    memset(m, 0, sizeof m);
    m[0] = 0x11;
    if (gm8(0x535b8c)) {
        memcpy(m + 4, gm(0x533900), 0x104);
        m[1] = 1;
    }
    Net_Send(0, true, m, 0x108);
}

/* 0x40afb0 (message 0x11): the leaderboard taken (0x40e890, 0x40e8a0) */
void NetRec_TakeBoard(bool have, const uint8_t *board)
{
    gw32(0x535b8c, 0);
    if (have) {
        memcpy(gm(0x533900), board, 0x104);
        gw32(0x535b8c, 1);
    }
}

/* 0x40afd0: every record's bit 22 */
void NetRec_MarkAll(void)
{
    for (int i = 0; i < NREC; i++) gw32(rec(i) + 4, flags(i) | 0x400000);
}

/* 0x40b020, 0x40b050: the record in use with the car slot */
int NetRec_BySlot(uint32_t slot)
{
    for (int i = 0; i < 7; i++)
        if (flags(i) & 0x80 && gm32(rec(i) + 8) == slot) return i;
    return 0;
}
bool NetRec_HasSlot(uint32_t slot)
{
    for (int i = 0; i < 7; i++)
        if (flags(i) & 0x80 && gm32(rec(i) + 8) == slot) return true;
    return false;
}

/* 0x40b080: ascending ids */
static int cmp_ids(const void *a, const void *b) { return r32(b) < r32(a) ? 1 : -1; }

/* 0x40b120: the start order: the local player and the others in use, by id (0x531cc0; 0x531b98 the
   unsorted copy) */
void NetRec_Order(void)
{
    uint32_t n = 0, id = Net_LocalPlayer();
    gw32(0x531cc0, id), gw32(0x531b98, id), n = 1;
    for (int i = 0; i < 7; i++)
        if (NetRec_Used(i)) {
            gw32(0x531cc0 + 4 * n, NetRec_Id(i)), gw32(0x531b98 + 4 * n, NetRec_Id(i));
            n++;
        }
    gw32(0x531770, n);
    crt_qsort(gm(0x531cc0), n, 4, cmp_ids);
}
uint32_t NetRec_OrderId(int k) { return gm32(0x531cc0 + 4u * (uint32_t)k); }   /* 0x40b1a0 */
uint32_t NetRec_OrderCount(void) { return gm32(0x531770); }                    /* 0x40b1b0 */

/* 0x40b1c0: every record's bit 23 cleared */
void NetRec_ClearBit23(void)
{
    for (int i = 0; i < 7; i++) gw32(rec(i) + 4, flags(i) & ~0x800000u);
}
