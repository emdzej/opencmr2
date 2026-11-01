#include "game/skids.h"
#include <stdio.h>
#include <string.h>
#include "exe.h"
#include "fixmath.h"
#include "game/common.h"
#include "game/effects.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/weather.h"
#include "render/polyq.h"
#include "sim/car.h"
#include "sim/pose.h"
#include "sim/sim.h"
#include "world/shadow.h"
#include "world/tod.h"

/* The game-state tables (docs/re/world.md#skid-marks). A segment is 0x28 bytes: +0 its age (nothing
   ever raises it), +4 the frame it was laid in, +8 and +0x14 the left and right points, +0x20 the
   colour, +0x24 and +0x25 the two alphas, +0x26 the surface class in the low nibble. */
#define SEGS   0x549da0u   /* [8 cars][4 wheels][200] */
#define ALIVE  0x548220u   /* a byte per segment */
#define HEAD   0x549ba0u   /* the ring's head per wheel (4 car + w) */
#define STEP   0x5885a0u   /* the head moves on at the next step */
#define LAID   0x543708u   /* the last segment was laid on hard ground */
#define SOFT   0x549b20u   /* ... on soft ground */
#define COLOUR 0x588620u   /* the running colour per car */
#define CENTRE 0x549c20u   /* the wheels' ground points, 0x30 per car */
#define FRAME  0x588750u   /* the frame count, raised by car 0 */
#define SPAN   0x51bca8u   /* L: the segments a quad spans */
#define CLASS  0x51bc68u   /* 8 bytes a class: flags (bit 0 marks, bit 1 soft, bits 2-3 texture), +4 rgb */
#define MAT    0x51bcb0u   /* a surface's class (signed) */
#define RUN    0x543b58u   /* the wheels' runs (Wheels_Track), 0x30 per car */

static Tex *tex[3];   /* 0x588740 skids_1, 0x588744 skids_mark, 0x588748 skids_blank */

static uint32_t seg(int car, int w, int s) { return SEGS + 0x28u * (uint32_t)(car * 800 + w * 200 + s); }
static uint32_t alive(int car, int w, int s) { return ALIVE + (uint32_t)(car * 800 + w * 200 + s); }
static int cls(uint8_t mat) { return gms8(MAT + (uint32_t)(int8_t)mat); }
static uint32_t cflags(int c) { return gm32(CLASS + 8u * (uint32_t)c); }
static int detail(void) { return (int)(gm32(0x52afd0) >> 25 & 3); }   /* 0x405cd0 */
static void rd3(uint32_t va, int32_t v[3]) { for (int k = 0; k < 3; k++) v[k] = gms32(va + 4u * (uint32_t)k); }
static void wr3(uint32_t va, const int32_t v[3]) { for (int k = 0; k < 3; k++) gw32(va + 4u * (uint32_t)k, (uint32_t)v[k]); }

/* Skids_Reset 0x465530 */
void Skids_Reset(void)
{
    for (int c = 0; c < 8; c++)
        for (int w = 0; w < 4; w++)
            for (int s = 0; s < 200; s++) gw8(alive(c, w, s), 0), gw32(seg(c, w, s), 0);
    static const uint32_t zero[] = {HEAD, 0x543788, 0x5430a0, 0x543ad8, SOFT, LAID};
    for (int i = 0; i < 32; i++) {
        for (size_t k = 0; k < sizeof zero / sizeof *zero; k++) gw32(zero[k] + 4u * (uint32_t)i, 0);
        gw32(0x543140 + 4u * (uint32_t)i, 1);
    }
    for (int i = 0; i < 32 * 3; i++) gw32(RUN + 4u * (uint32_t)i, 0);
}

/* Skids_Load 0x465600 (from the cars' placing, 0x424c50) */
void Skids_Load(void)
{
    Skids_Reset();
    static const uint32_t name[3] = {0x51bd20, 0x51bd00, 0x51bce0};
    for (int i = 0; i < 3; i++) {
        char p[260];
        snprintf(p, sizeof p, "%s%s", exe_str(0x516a50), exe_str(name[i]));   /* 0x40ed50, "%s%s" 0x517d98 */
        uint8_t found;
        tex[i] = Bfl_FindTexture(Bfl_CommonArc(), p, &found, false, 0);
    }
    uint32_t m = Gs_Mode();
    gw32(SPAN, m == 5 || m == 6 || m == 7 ? 4 : 2);
    /* class 2's colour by the rally (the original tests rallies 4 and 5 apart, with the same colour as
       the rest), copied to class 7 */
    uint32_t rgb = (Tod_Rally() & 0xff) == 0 ? 0x6f7a89 : 0x4389b7;
    uint32_t v = (gm32(CLASS + 0x14) & 0xff000000u) | rgb;
    gw32(CLASS + 0x14, v);
    gw32(CLASS + 0x3c, v);
}

/* Skids_Step 0x464c60 */
void Skids_Step(int car)
{
    for (int w = 0; w < 4; w++) {
        uint32_t n = 4u * (uint32_t)(4 * car + w);
        if (gm32(STEP + n)) {
            gw32(STEP + n, 0);
            gw32(HEAD + n, (uint32_t)((gms32(HEAD + n) + 1) % 200));
        }
    }
}

/* Skids_Centres 0x4657d0: per wheel the middle of the shadow record's two points for it (+0x198 and
   +0x1c8 on), raised by 0.05 */
void Skids_Centres(int car)
{
    if (car >= 8 || !Shadow_Get(car)) return;
    for (int i = 0; i < 4; i++) {
        const uint8_t *r = Shadow_Get(car);
        int32_t q[3], p[3], o[3];
        memcpy(q, r + 0x1c8 + 12 * i, sizeof q);
        memcpy(p, r + 0x198 + 12 * i, sizeof p);
        for (int k = 0; k < 3; k++) o[k] = fx_mul(p[k] - q[k], 0x8000) + q[k];
        o[1] += 0xccc;
        wr3(CENTRE + 0x30u * (uint32_t)car + 12u * (uint32_t)i, o);
    }
}

/* Skids_Add 0x464cb0 */
void Skids_Add(int car)
{
    if (car >= 8 || detail() == 2) return;
    Car *c = Car_Get(car);
    if (car == 0) gw32(FRAME, gm32(FRAME) + 1);
    uint8_t kind = Weather_Kind(car);
    bool wet = (kind == 1 || kind == 2) && Weather_Level(car) > 0x3333;
    /* the expiry (the ages stay 0, so none die) */
    int32_t span = gms32(SPAN);
    for (int w = 0; w < 4; w++)
        for (int s = 0; s < 200; s++)
            if (gm8(alive(car, w, s)) && gms32(seg(car, w, s)) > (wet ? span * 4 : span * 80)) {
                gw8(alive(car, w, s), 0);
                gw8(seg(car, w, s) + 0x24, 0), gw8(seg(car, w, s) + 0x25, 0);
            }
    int32_t yoff = -0x20c;
    for (int w = 0; w < 4; w++, yoff -= 0x83) {
        uint32_t n = 4u * (uint32_t)(4 * car + w);
        uint8_t light[3];   /* 0x463270: the wheel's light (the original's "none" white never happens) */
        for (int k = 0; k < 3; k++) light[k] = gm8(0x543f28 + 16u * (uint32_t)car + 4u * (uint32_t)w + (uint32_t)k);
        uint8_t mat = CI8(c, 0xaae + 2 * (uint32_t)w), prev = gm8(0x5435d0 + n);
        int cm = cls(mat), cp = cls(prev);
        int32_t touch = CI32(c, 0xbac + 4 * (uint32_t)w);
        bool hard = gms32(0x5430a0 + n) > 2 && gms32(0x5439b8 + 4u * (uint32_t)car) > 1 && CI32(c, 0xb74) == 1 &&
                    (cflags(cm) & 1) && (cflags(cp) & 1);
        bool soft = (cflags(cp) & 2) && (cflags(cm) & 2);
        if (!(touch == 1 && (hard || soft))) {
            if (gm32(LAID + n) || gm32(SOFT + n)) gw32(STEP + n, 1);
            continue;
        }
        int32_t slip = Wheel_Slip(car, w);
        if ((cflags(cm) & 2) && touch && CI32(c, 0xbac + 4 * (uint32_t)(w ^ 2))) {
            if (mat == 0x19) continue;
            slip = 0x8000;   /* soft ground under both wheels of a side: a mark at half */
        } else if (slip <= 0) {
            continue;
        }
        uint32_t run = RUN + 0x30u * (uint32_t)car + 12u * (uint32_t)w;
        if ((!gm32(run) && !gm32(run + 8)) || gm32(0x543140 + n)) continue;
        uint8_t alpha = (uint8_t)(fx_mul(slip, 0xff0000) >> 16);
        gw32(STEP + n, 1);
        int head = gms32(HEAD + n);
        uint32_t a = seg(car, w, head);
        /* across the run: run x up, made unit, 0.03 (0.6 of it with narrow wheels; 2.5 with option 6) */
        int32_t r[3], up[3] = {0, 0x10000, 0}, sd[3];
        rd3(run, r);
        sd[0] = fx_mul(r[1], up[2]) - fx_mul(r[2], up[1]);
        sd[1] = fx_mul(r[2], up[0]) - fx_mul(r[0], up[2]);
        sd[2] = fx_mul(r[0], up[1]) - fx_mul(r[1], up[0]);
        Vec_Unit(sd);
        for (int k = 0; k < 3; k++) sd[k] = fx_mul(sd[k], 0x1eb8);
        if (!Gs_Option(6)) {
            if (Pose_Narrow(c, 1))
                for (int k = 0; k < 3; k++) sd[k] = fx_mul(sd[k], 0x9999);
        } else {
            for (int k = 0; k < 3; k++) sd[k] = fx_mul(sd[k], 0x28000);
        }
        int32_t ce[3], L[3], R[3];
        rd3(CENTRE + 0x30u * (uint32_t)car + 12u * (uint32_t)w, ce);
        for (int k = 0; k < 3; k++) L[k] = ce[k] + sd[k], R[k] = ce[k] - sd[k];
        L[1] += yoff, R[1] += yoff;
        wr3(a + 8, L);
        wr3(a + 0x14, R);
        gw8(a + 0x24, alpha), gw8(a + 0x25, alpha);
        gw32(a, 0);
        gw8(a + 0x26, (uint8_t)((gm8(a + 0x26) & 0xf0) | (cm & 0xf)));
        gw8(alive(car, w, head), 1);
        gw32(a + 4, gm32(FRAME));
        uint32_t col = COLOUR + 4u * (uint32_t)car, tc = CLASS + 8u * (uint32_t)(cp & 0xff) + 4;
        if (!(gm8(CLASS + 8u * (uint32_t)(cm & 0xff)) & 2)) {
            /* hard ground: the running colour moves half way to the class's, lit */
            for (int k = 0; k < 3; k++) {
                uint8_t v = gm8(col + (uint32_t)k);
                v = (uint8_t)(v + (int8_t)(((int)gm8(tc + (uint32_t)k) - (int)v) / 2));
                gw8(col + (uint32_t)k, v);
                gw8(a + 0x20 + (uint32_t)k, (uint8_t)((uint32_t)v * light[k] / 0xff));
            }
            gw32(LAID + n, 1), gw32(SOFT + n, 0);
        } else {
            /* soft ground: the class's colour, lit, at once (the dword copied, its fourth byte too) */
            for (int k = 0; k < 3; k++) gw8(col + (uint32_t)k, (uint8_t)((uint32_t)gm8(tc + (uint32_t)k) * light[k] / 0xff));
            gw32(a + 0x20, gm32(col));
            gw32(LAID + n, 0), gw32(SOFT + n, 1);
        }
        uint32_t nx = seg(car, w, (head + 1) % 200);
        gw8(nx + 0x24, 0), gw8(nx + 0x25, 0);
    }
}

static int clamp255(int v) { return v < 0 ? 0 : v > 0xff ? 0xff : v; }   /* 0x465ea0 */

static void vert(PolyVert *v, uint32_t va, uint32_t rgb_va, int alpha, int32_t u, int32_t t)
{
    rd3(va, &v->x);
    for (int k = 0; k < 3; k++) v->rgba[k] = gm8(rgb_va + (uint32_t)k);
    v->rgba[3] = (uint8_t)alpha;
    v->u = u, v->v = t;
}

/* 0x4658e0: per wheel, back from the head, a quad from each segment to the one laid L - 1 to 1 frames
   before it (the farthest found), then on from there */
static void draw_car(int car)
{
    if (car >= 8 || detail() == 2) return;
    for (int w = 0; w < 4; w++) {
        int head = gms32(HEAD + 4u * (uint32_t)(4 * car + w));
        int start = 0;
        do {
            bool found = false;
            int cur = (head - start + 200) % 200;
            int k = gms32(SPAN);
            if (k > 1) {
                int j = cur - k + 200;
                do {
                    if (found) break;
                    k--, j++;
                    if (!gm8(alive(car, w, cur)) || !gm8(alive(car, w, j % 200))) continue;
                    uint32_t a = seg(car, w, cur), b = seg(car, w, j % 200);
                    found = gms32(a + 4) == gms32(b + 4) + k;
                    if (!found) continue;
                    int a24 = gm8(a + 0x24), a25 = gm8(a + 0x25), b24 = gm8(b + 0x24), b25 = gm8(b + 0x25);
                    if (!(gm8(b + 0x20) <= 0xc8 && gms32(b) <= 0x4d8)) {
                        /* faded by the age (never reached: the ages stay 0, the test is on b's red) */
                        int32_t aa = gms32(a), ba = gms32(b);
                        a24 -= a24 * aa * 0x7c / 0x4d8, a25 -= a25 * aa * 0x7c / 0x4d8;
                        b24 -= b24 * ba * 0x7c / 0x4d8, b25 -= b25 * ba * 0x7c / 0x4d8;
                    }
                    a24 = clamp255(a24), a25 = clamp255(a25), b24 = clamp255(b24), b25 = clamp255(b25);
                    if (a24 <= 1 && a25 <= 1 && b24 <= 1 && b25 <= 1) continue;
                    PolyVert v1, v2, v3, v4;
                    vert(&v1, a + 8, a + 0x20, a24, 0, 0);
                    vert(&v2, a + 0x14, a + 0x20, a25, 0x10000, 0);
                    vert(&v3, b + 8, b + 0x20, b24, 0, 0x10000);
                    vert(&v4, b + 0x14, b + 0x20, b25, 0x10000, 0x10000);
                    if (v3.x == v4.x && v3.y == v4.y) continue;
                    Tex *t = tex[0];
                    switch (cflags(gm8(a + 0x26) & 0xf) >> 2 & 3) {
                    case 1: t = tex[1]; break;
                    case 2:
                        /* blank: black, a third of a's alphas on both ends */
                        t = tex[2];
                        memset(v1.rgba, 0, 4), memset(v2.rgba, 0, 4);
                        v1.rgba[3] = (uint8_t)(a24 / 3), v2.rgba[3] = (uint8_t)(a25 / 3);
                        memcpy(v3.rgba, v1.rgba, 4), memcpy(v4.rgba, v2.rgba, 4);
                        break;
                    }
                    Poly_Add(&v2, &v1, &v3, t, 0x26);
                    Poly_Add(&v4, &v2, &v3, t, 0x26);
                } while (k > 1);
            }
            start += k;
        } while (start < 200);
    }
}

/* Skids_Draw 0x465780: the cars but those the view hides (0x46bd20's 7) */
void Skids_Draw(int view)
{
    if (detail() == 2) return;
    for (int i = 0; i < g_all.n; i++)
        if (i < 8 && gm32(0x588cd4 + 4u * (uint32_t)(view + i * 2)) != 7) draw_car(i);
}
