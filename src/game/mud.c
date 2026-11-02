/* The mud on the first car's body texture (see mud.h). */
#include "game/mud.h"
#include "crt.h"
#include "exe.h"
#include "fixmath.h"
#include "game/mem.h"
#include "game/weather.h"
#include "render/tex.h"
#include "sim/car.h"
#include <math.h>
#include <string.h>

/* a patch of the texture (0x588ed8, 0x1c bytes each, up to 11) */
typedef struct {
    int16_t x, y, w, h;   /* +0x00: in the low detail's texels (times 4 at the full detail) */
    uint16_t rate;        /* +0x10: added to the count each throw (its share of the largest patch) */
    uint16_t cap;         /* +0x12: splats before it is full */
    uint16_t acc;         /* +0x14: a splat taken each time it passes 255 */
    uint16_t splats;      /* +0x16 */
    bool full;            /* +0x18 */
    bool valid;           /* +0x19: given a size */
} Patch;

/* a splat to paint (0x589010, 8 bytes each, up to 64) */
typedef struct {
    const Patch *p;
    uint8_t x, y, rows;   /* +0x04-+0x06 (the rows are not read back) */
} Splat;

static Tex *body[8];      /* 0x588ed0 */
static int body_w[8];     /* 0x589210 */
static Patch patches[11];
static int npatches;      /* DAT_0058931c */
static Splat splats[64];
static int nsplats;       /* DAT_00589318 */
static int quarter;       /* DAT_00589334 */
static bool flip;         /* DAT_00589331: the class 0's patches the other way round */
static int thrown[4];     /* 0x589320: throws by wheel (not read back) */

/* 0x46e530 */
static void clear(void)
{
    nsplats = 0;
    for (int k = 0; k < npatches; k++) patches[k].acc = patches[k].splats = 0, patches[k].full = false;
    /* (0x589330, set when a splat is painted, is not read back) */
    memset(thrown, 0, sizeof thrown);
}

/* 0x46e6a0: each patch's rate (the body's width) and cap (30,000) by its area over half the largest
   one's, in 16.16; from the third on the rate halved (at least 1) */
static void rates(void)
{
    int most = 0;
    for (int k = 0; k < npatches; k++) {
        int a = patches[k].h * patches[k].w;
        if (a > most) most = a;
    }
    for (int k = 0; k < npatches; k++) {
        Patch *p = &patches[k];
        /* the area's low 16 bits (with the sign of area << 16) as the high word over (most / 2) << 16 */
        int32_t area = p->h * p->w;
        uint32_t hi = (uint32_t)(((int32_t)((uint32_t)area << 16) >> 31) << 16) | ((uint32_t)area & 0xffff);
        int64_t den = (int64_t)(int32_t)((uint32_t)(most / 2) << 16);
        int32_t share = den ? (int32_t)((int64_t)((uint64_t)hi << 32) / den) : 0;
        p->rate = (uint16_t)(((int64_t)share * body_w[0]) >> 16);
        p->cap = (uint16_t)(((int64_t)share * 30000) >> 16);
        p->acc = p->splats = 0;
        p->full = false;
    }
    for (int k = 2; k < npatches; k++) {
        patches[k].rate >>= 1;
        if (!patches[k].rate) patches[k].rate = 1;
    }
}

/* 0x46e620: a patch from the class's table (x, y, w, h), valid with a size */
static void add_patch(const uint8_t *e)
{
    if (npatches < 11) {
        Patch *p = &patches[npatches];
        p->valid = p->full = false;
        p->rate = p->acc = 0;
        int16_t w = (int16_t)(e[4] | e[5] << 8), h = (int16_t)(e[6] | e[7] << 8);
        if (w > 0 && h > 0) {
            p->x = (int16_t)(e[0] | e[1] << 8), p->y = (int16_t)(e[2] | e[3] << 8), p->w = w, p->h = h;
            p->valid = true;
        }
        npatches++;
    }
    rates();
}

void Mud_Setup(int car, int cls)
{
    /* 0x46e580: the first record whose name has "BODF" eight from its end (the original reads
       before a shorter name) */
    clear();
    flip = cls == 0;
    npatches = 0;
    if (car >= 0 && car < 8) {
        for (int i = 0; i < TEX_MAX; i++) {
            const Tex *t = g_tex[i];
            size_t len = t ? strlen(t->name) : 0;
            if (len >= 8 && !strncmp(t->name + len - 8, exe_str(0x51c680), 4)) {
                body[car] = g_tex[i];
                break;
            }
        }
        if (body[car]) body_w[car] = body[car]->width;
    }
    const uint8_t *tab = exe_data(0x51a3e8 + 0x58u * (uint32_t)(cls & 0xff), 0x58);
    for (int k = 0; k < 11 && tab; k++) add_patch(tab + 8 * k);
}

/* 0x46ed40: the patch takes a splat when its count passes 255 */
static bool takes(int k)
{
    Patch *p = &patches[k];
    if (p->full) return false;
    p->acc = (uint16_t)(p->acc + p->rate);
    if (p->acc <= 0xff) return false;
    p->acc = 0;
    return true;
}

/* 0x46ec40: a splat at (x, y) of patch k (x within four times its width less 4, y within its
   height), the patch full once it has its cap */
static void splat(int k, int x, int y)
{
    if (k >= npatches || !patches[k].valid) return;
    Patch *p = &patches[k];
    if (!takes(k) || x < 0 || y < 0 || x >= p->w * 4 - 4 || y >= p->h || nsplats >= 64) return;
    Splat *s = &splats[nsplats];
    s->p = p;
    s->x = (uint8_t)x, s->y = (uint8_t)y;
    int over = y - p->h + 4;
    s->rows = (uint8_t)(over < 1 ? 4 : 4 - over);
    nsplats++;
    if (p->cap <= p->splats) p->full = true;
    else p->splats++;
}

/* 0x46ed80: a splat along a run from (x0, y0): a random step of n/2 along len (less 16) in the way
   dx, and a random row up to hgt scaled by the step's sine, in the way dy, each jittered by -8 to 8
   over 1 to 3 */
static void throw_run(int k, int x0, int y0, int len, int hgt, int dx, int dy, int n)
{
    int16_t a = (int16_t)(crt_rand() % (n / 2));
    crt_rand();
    if (a < 0x401) {
        int32_t h = (int32_t)llrint((double)hgt * 65536.0);
        int32_t t = fx_mul(h, g_sin[(uint32_t)a & 0xfff]);
        if (t < 0) t = -t;
        hgt = t >> 16;
    }
    int r1 = crt_rand(), r2 = crt_rand(), r3 = crt_rand(), r4 = crt_rand(), r5 = crt_rand();
    splat(k, ((a * len) / (n / 2) - 0x10) * dx + x0 + (r2 % 0x11 - 8) / (r3 % 3 + 1),
          (r1 % (hgt + 1)) * dy + y0 + (r4 % 0x11 - 8) / (r5 % 3 + 1));
}

/* 0x46ea80: wheel w's throw at strength s: a rear wheel a splat anywhere on each patch from the third;
   a front one a run along its patch (0 or 1) from the fifth of its width, as long as the strength
   (0.5 to 1.5) allows over 0.7, from the corner the class's way round and the wheel's side give */
static void throw_wheel(int w, int s)
{
    if (w == 2 || w == 3) {
        for (int k = 2; k < npatches; k++) {
            int rx = crt_rand();
            int16_t pw = patches[k].w;
            int ry = crt_rand();
            splat(k, (rx * pw) / 0x7fff, (ry * patches[k].h) / 0x7fff);
        }
    } else {
        int u = w != 0;
        int h = patches[u].h, x0 = patches[u].w / 5, x1 = patches[u].w - x0;
        if (s < 0x8000) s = 0;
        else if (s > 0x18000) s = 0x18000;
        int len = (s * (x1 - x0)) / 0xb333, hgt = (s * (h - 8)) / 0xb333;
        if (len > 0x20) {
            if (u == 0) {
                if (!flip) throw_run(0, x1 - 1, 1, len, hgt, -1, 1, 0x4fa);
                else throw_run(0, x0, h - 1, len, hgt, 1, -1, 0x4fa);
            } else if (!flip) throw_run(u, x1 - 1, h - 1, len, hgt, -1, -1, 0x4fa);
            else throw_run(u, x0, 1, len, hgt, 1, 1, 0x4fa);
        }
    }
    thrown[w]++;
}

/* 0x46eeb0: the wheel on a loose surface (0, 3, 0xc, 0xd, 0x1a) with the car wet */
static bool muddy(const Car *c, int w)
{
    switch ((uint16_t)(c->s[0xaae + 2 * w] | c->s[0xaaf + 2 * w] << 8)) {
    case 0: case 3: case 0xc: case 0xd: case 0x1a: return Weather_CarWet(c) > 0;
    default: return false;
    }
}

void Mud_Throw(int car)
{
    if (npatches < 1) return;
    const Car *c = Car_Get(car);
    for (int w = 0; w < 4; w++) {
        if (!muddy(c, w)) continue;
        int32_t s = Car_WheelSpeed(Car_Get(car), w, 0);   /* 0x42b600 */
        if (s > 0x1e0000)
            for (int i = 0; i < 2; i++) throw_wheel(w, s / 0x3c);
    }
}

/* 0x4a52d0's 32-bit branch for the brush's colour: red and green the colour's share alone (the
   original adds 0.0 where the destination's part would be), blue blended with the texel's, opaque */
static void texel(uint8_t *d, const uint8_t c[4])
{
    if (c[3] != 0xff) {
        double a = (double)c[3] * (double)0.003921568859368563f;   /* 0x511364 */
        d[0] = (uint8_t)llrint((double)c[0] * a + 0.0);
        d[1] = (uint8_t)llrint((double)c[1] * a + 0.0);
        d[2] = (uint8_t)llrint((double)c[2] * a + (double)d[2] * (1.0 - a));
    } else
        d[0] = c[0], d[1] = c[1], d[2] = c[2];
    d[3] = 0xff;
}

void Mud_Paint(void)
{
    if (npatches < 1 || nsplats < 1) return;
    static const uint8_t mud[4] = {0x2f, 0x27, 0x14, 0xc0};
    Tex *t = body[0];
    int q = quarter;
    if (++quarter > 3) quarter = 0;
    if (!t || !t->surface) {
        nsplats = 0;   /* (the original locks nothing then and paints on) */
        return;
    }
    /* this frame's quarter (0x4a50f0 locks it): top right, bottom left, bottom right, top left; its
       edges are left out (the strict tests), so the middle row and column never take mud */
    int W = t->width, H = t->height, l, tp, r, b;
    switch (q) {
    case 0: l = W / 2 - 1, tp = 0, r = W - 1, b = H / 2 - 1; break;
    case 1: l = 0, tp = H / 2 - 1, r = W / 2 - 1, b = H - 1; break;
    case 2: l = W / 2 - 1, tp = H / 2 - 1, r = W - 1, b = H - 1; break;
    default: l = 0, tp = 0, r = W / 2 - 1, b = H / 2 - 1; break;
    }
    bool low = (gm32(0x52afd4) & 3) != 0;   /* 0x405d10 */
    int size = low ? 4 : 0x10;
    const uint8_t *brushes = low ? exe_data(0x51c240, 0x40) : exe_data(0x51c280, 0x400);
    D3dTexture *s = t->surface;
    for (int i = 0; i < nsplats && brushes; i++) {
        int x = (uint8_t)splats[i].x + splats[i].p->x, y = (uint8_t)splats[i].y + splats[i].p->y;
        if (!low) x *= 4, y *= 4;
        int k = crt_rand() & 3;
        for (int row = 0; row < size; row++)
            for (int col = 0; col < size; col++) {
                if (!brushes[(k * size + row) * size + col]) continue;
                int px = (int16_t)x + row, py = (int16_t)y + col;   /* (the row along x) */
                if (l < px && px < r && tp < py && py < b && px < s->width && py < s->height)
                    texel(s->rgba + ((size_t)py * (size_t)s->width + (size_t)px) * 4, mud);
            }
    }
    s->dirty = true;   /* 0x4a56c0 */
    nsplats = 0;
}
