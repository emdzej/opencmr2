/* The car state and its set-up (see car.h). */
#include "sim/car.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "pending.h"
#include "world/stage.h"
#include "world/tod.h"
#include <string.h>

static Car cars[CAR_MAX];   /* the block at *0x53c9a4 */
static CarSlot slots[CAR_MAX];
Car *g_car;

Car *Car_Get(int n) { return n >= 0 && n < CAR_MAX ? &cars[n] : NULL; }
CarSlot *Car_Slot(int n) { return n >= 0 && n < CAR_MAX ? &slots[n] : NULL; }

Obj *Obj_FindType(Obj *o, uint32_t type)
{
    for (; o; o = o->next) {
        if ((o->type & 0xff) == type) return o;
        if (o->child) {
            Obj *f = Obj_FindType(o->child, type);
            if (f) return f;
        }
    }
    return NULL;
}

/* Out of the old parent's child list, appended as the new parent's last child (no move when it is
   already there, or when the new parent is its child). */
void Obj_Reparent(Obj *o, Obj *np)
{
    if (!o || !np || o->parent == np || np->parent == o) return;
    Obj *op = o->parent;
    if (op) {
        if (op->child == o) op->child = o->next;
        else {
            Obj *p = op->child;
            while (p && p->next != o) p = p->next;
            if (!p) return;
            p->next = o->next;
        }
    }
    o->parent = np;
    o->next = NULL;
    if (!np->child) np->child = o;
    else {
        Obj *p = np->child;
        while (p->next) p = p->next;
        p->next = o;
    }
}

/* 0x43e5a0: the car's two models from its table entry, the wheels (types 1-4) and the parts (0xe,
   0xf), both models under the world root. For a car whose entry's kind is not 'A', 0x42cb90 first sets
   up its textures (not ported). */
static void attach_models(int n, Car *c)
{
    CarSlot *s = Car_Slot(n);
    c->main = s->main;
    c->second = s->second;
    for (int i = 0; i < 4; i++) c->wheel[i] = Obj_FindType(c->main, (uint32_t)i + 1);
    c->part74c = Obj_FindType(c->second, 0xe);
    c->part748 = Obj_FindType(c->second, 0xf);
    if (Car_Slot(CI8(c, 0xb1a))->kind != 'A') PENDING_NOTE(0x42cb90, "Car_Textures");
    if (c->main && c->main->parent != &g_world) Obj_Reparent(c->main, &g_world);
    if (c->second && c->second->parent != &g_world) Obj_Reparent(c->second, &g_world);
}

/* 0x4086f0 (0x4083f0 before it is empty): mode 4, bit 0 of the byte at 0x52f3ec + n x 0xc4; else
   the driver's team (bits 18-21 of 0x531350 + n x 0x30) and bit 5 of that team's word at 0x52fa5c +
   team x 0x650, 1 without a team (15). */
static bool Champ_CarFlag(uint8_t n)
{
    if (Gs_Mode() == 4) return gm8(0x52f3ec + n * 0xc4u) & 1;
    uint32_t t = gm32(0x531350 + n * 0x30u) >> 0x12 & 0xf;
    if (t == 0xf) return true;
    return gm32(0x52fa5c + t * 0x650u) >> 5 & 1;
}

static int32_t inv(int32_t v) { return v ? (int32_t)(0x100000000LL / v) : 0; }
static int32_t pct(int8_t b) { return fx_mul(b * 0x10000, 0x28f); }   /* a set-up byte as a fraction */

/* 0x43dff0: the suspension from the set-up value (option 6: -0.4) */
static void setup_suspension(Car *c, int32_t p)
{
    if (Gs_Option(6)) p = -0x6666;
    CW32(c, 0x9b8, fx_mul(p, 0x6666) + 0x9999);
    CW32(c, 0x9bc, fx_mul(p, 0x3333) + 0x6666);
    CW32(c, 0x8d8, CI32(c, 0x75c) / 8);
    CW32(c, 0x9c0, inv(fx_mul(p, -0x1999) + 0x3333));
    CW32(c, 0x9c4, inv(fx_mul(p, -0x1aaaa) + 0x50000));
    for (uint32_t o = 0x9f8; o < 0xa08; o += 4) {
        CW32(c, o - 0x10, 0x4ccc);
        CW32(c, o, fx_mul(p, 0x5999) + 0x4000);
        CW32(c, o - 0x70, fx_mul(0x10000 - p, 0x1999));
        CW32(c, o - 0x80, fx_mul(p, 0x8000) + 0x8000);
    }
    CW32(c, 0xa08, (int32_t)0xffffe667);
}

/* 0x43e1f0: one of five gear sets (neutral, six forward, reverse) with its final-drive change, then all
   eight divided by 1.14. Set 4 leaves +0x788 as it was. */
static void setup_gears(Car *c, int set)
{
    static const int32_t g[5][7] = {
        {0x4d91, 0x651e, 0x7ae1, 0x8c08, 0x9ae1, 0xa8f5, (int32_t)0xffffb26f},
        {0x4f9d, 0x69fb, 0x83d7, 0x9999, 0xad4f, 0xc189, (int32_t)0xffffb063},
        {0x5168, 0x6ed9, 0x8d0e, 0xa76c, 0xc000, 0xd9db, (int32_t)0xffffae98},
        {0x5374, 0x73b6, 0x9604, 0xb4fd, 0xd26e, 0xf26e, (int32_t)0xffffac8c},
        {0x553f, 0x7893, 0x9ef9, 0xc28f, 0xe4dd, 0x10ac0, (int32_t)0xffffaac1},
    };
    static const int32_t fd[4] = {0x7ae, 0x5c2, 0x3d7, 0x1eb};
    if (set >= 0 && set <= 4) {
        CW32(c, 0x7dc, 0);
        for (int i = 0; i < 7; i++) CW32(c, 0x7e0 + 4 * (uint32_t)i, g[set][i]);
        if (set < 4) CW32(c, 0x788, CI32(c, 0x788) + fd[set]);
    }
    for (uint32_t o = 0x7dc; o < 0x7fc; o += 4) CW32(c, o, (int32_t)(((int64_t)CI32(c, o) << 16) / 0x123d7));
}

/* 0x43f280: the corners moved by the weight split (+0x764, +0x768, +0x76c along the body rows) */
static void corner_offsets(Car *c)
{
    int32_t a[3], b[3], d[3];
    for (int i = 0; i < 3; i++) {
        a[i] = fx_mul(CI32(c, 0x360 + 4 * (uint32_t)i), CI32(c, 0x764));
        b[i] = fx_mul(CI32(c, 0x378 + 4 * (uint32_t)i), CI32(c, 0x76c));
        d[i] = fx_mul(CI32(c, 0x360 + 4 * (uint32_t)i), CI32(c, 0x768));
    }
    for (uint32_t i = 0; i < 3; i++) {
        CW32(c, 0x2a0 + 4 * i, CI32(c, 0x2a0 + 4 * i) - a[i] - b[i]);
        CW32(c, 0x2ac + 4 * i, CI32(c, 0x2ac + 4 * i) - a[i] + b[i]);
        CW32(c, 0x2b8 + 4 * i, CI32(c, 0x2b8 + 4 * i) + d[i] - b[i]);
        CW32(c, 0x2c4 + 4 * i, CI32(c, 0x2c4 + 4 * i) + d[i] + b[i]);
    }
}

/* The eight corners of the half-size box (+0x204) along the orientation rows (+0x00, +0x10, +0x20),
   at the position (+0x2d0); then, unless +0xb64 is set, the weight-split offsets. */
void Car_BoxCorners(Car *c)
{
    g_car = c;
    int32_t h[3] = {CI32(c, 0x204), CI32(c, 0x208), CI32(c, 0x20c)};
    int32_t p[3][3];   /* per axis k: the three rows' parts */
    for (uint32_t k = 0; k < 3; k++)
        for (uint32_t r = 0; r < 3; r++) p[k][r] = fx_mul(CI32(c, 0x10 * r + 4 * k), h[r]);
    for (uint32_t k = 0; k < 3; k++) {
        int32_t x = p[k][0], y = p[k][1], z = p[k][2];
        CW32(c, 0x2a0 + 4 * k, z + y + x);
        CW32(c, 0x2ac + 4 * k, (y - z) + x);
        CW32(c, 0x2c4 + 4 * k, (y - z) - x);
        CW32(c, 0x2b8 + 4 * k, (z - x) + y);
    }
    /* the lower four mirror the upper ones */
    static const uint16_t lo[12] = {0x2c4, 0x2c8, 0x2cc, 0x2b8, 0x2bc, 0x2c0, 0x2ac, 0x2b0, 0x2b4, 0x2a0, 0x2a4, 0x2a8};
    for (uint32_t i = 0; i < 12; i++) CW32(c, 0x270 + 4 * i, -CI32(c, lo[i]));
    for (uint32_t i = 0; i < 24; i++) CW32(c, 0x270 + 4 * i, CI32(c, 0x270 + 4 * i) + CI32(c, 0x2d0 + 4 * (i % 3)));
    if (!CI32(c, 0xb64)) corner_offsets(c);
}

/* The handling classes' immediates (0 to 13): length, width, height (+0x1f8), +0x788, +0x794,
   +0x764-+0x774 and the mass part (to +0x75c with 120 kg). */
static int32_t handling(Car *c, int cls)
{
    static const int32_t t[14][11] = {
        /* 1f8      1fc      200      788     794      764      768      76c     774     770      mass */
        {0x426e9, 0x16b85, 0x1c51e, 0x570a, 0x98b02, 0x1cccc, 0x8000, 0x3d70, 0xcf5c, 0x10ccc, 0x3e80000},
        {0x4451e, 0x15999, 0x1d70a, 0x570a, 0x98b02, 0x1cccc, 0xf851, 0x570a, 0xcf5c, 0x1147a, 0x4000000},
        {0x40f5c, 0x163d7, 0x1c51e, 0x5687, 0x98b02, 0x1cccc, 0x451e, 0x4ccc, 0xcf5c, 0x1147a, 0x3e80000},
        {0x44560, 0x15eb8, 0x1cfdf, 0x570a, 0x98b02, 0x1cccc, 0x14ccc, 0x4ccc, 0xb0a3, 0xfa9f, 0x3e80000},
        {0x40312, 0x14ccc, 0x1c51e, 0x570a, 0x98b02, 0x1c000, 0xcccc, 0x4ccc, 0xe3d7, 0x12dd2, 0x3e80000},
        {0x41c28, 0x154bc, 0x1d47a, 0x5c28, 0x98b02, 0x1c000, 0x10000, 0x4ccc, 0xe3d7, 0x12dd2, 0x3e80000},
        {0x3e3d7, 0x15eb8, 0x1c28f, 0x63d7, 0x98b02, 0x1ae14, 0x9c28, 0x4ccc, 0xcf5c, 0xfae1, 0x3a60000},
        {0x475c2, 0x1570a, 0x1c28f, 0x4a3d, 0x88b02, 0x1e147, 0x1028f, 0x4ccc, 0xcf5c, 0xfae1, 0x3c70000},
        {0x30083, 0x14041, 0x18000, 0x3ae1, 0x88b02, 0x13333, 0x4ccc, 0x2666, 0xb5c2, 0xfa9f, 0x1bf0000},
        {0x3b958, 0x15db2, 0x1e041, 0x6666, 0x98b02, 0x1a666, 0x9999, 0x4ccc, 0xe3d7, 0xe106, 0x3450000},
        {0x3b333, 0x106a7, 0x1cf5c, 0x570a, 0x98b02, 0x1a666, 0x13333, 0x4ccc, 0xca3d, 0xe106, 0x2dc0000},
        {0x3d333, 0x15999, 0x1c312, 0x75c2, 0x98b02, 0x1a666, 0x9999, 0x4ccc, 0xe3d7, 0xe106, 0x2fc0000},
        {0x3ec49, 0x146a7, 0x1c28f, 0x570a, 0x98b02, 0x1a666, 0x13333, 0x4ccc, 0xca3d, 0xe106, 0x34e0000},
        {0x41687, 0x16147, 0x1bb22, 0x3ae1, 0x88b02, 0x1ae14, 0xfd70, 0x428f, 0xd70a, 0xf581, 0x1bf0000},
    };
    if (cls < 0 || cls > 13) return 0;   /* the switch has no default: the mass part stays 0 */
    static const uint16_t off[10] = {0x1f8, 0x1fc, 0x200, 0x788, 0x794, 0x764, 0x768, 0x76c, 0x774, 0x770};
    for (int i = 0; i < 10; i++) CW32(c, off[i], t[cls][i]);
    return t[cls][10];
}

static void row(Car *c, uint32_t o, const Obj *ob, int r)
{
    for (uint32_t k = 0; k < 3; k++) CW32(c, o + 4 * k, ob ? ob->local.m[4 * r + (int)k] : 0);
}

/* The set-up bytes: 0x407630 (n), the defaults 0x494a40 (bit 24), 0x494a70 (per rally), 0x406890. */
static uint32_t setup_for(Car *c, int n, bool alt)
{
    if (Gs_SelBit24()) {
        uint32_t a = gm32(0x52f2ac) >> 10 & 3, b = gm32(0x52f2ac) >> 12 & 3;
        return 0x5200e8 + (a * 3 + b) * 7;
    }
    uint32_t rally_default;
    switch (Tod_Rally()) {
    case 3: rally_default = 0x52012f; break;
    case 5: rally_default = 0x520136; break;
    case 7: rally_default = 0x52013d; break;
    case 8: rally_default = 0x520144; break;
    default: rally_default = 0x520128; break;
    }
    if (Gs_Mode() == 4) return rally_default;
    if (Gs_SelBit25() && !Gs_ModeBit19()) {
        PENDING_NOTE(0x408500, "Champ_DriverIndex");   /* only reached in the arcade modes */
        if (alt) return rally_default;
    }
    if (Gs_ModeBit17() && n == 1 && Gs_SelBit25()) {
        if (alt) return 0x51682c + Tod_Rally() * 7;
        if (!Gs_ModeBit19()) return rally_default;
    } else if (alt) {
        return 0x51682c + Tod_Rally() * 7;
    }
    return Gs_Setup(Gs_Byte537ef6() + CI8(c, 0xb1a));
}

void Car_Init(Car *c, Obj *obj, int cls, const int32_t pos[3], int n, bool keep)
{
    g_car = c;
    bool alt = false;   /* local_8: the challenge's own car (bit 28 with 0x52afb8 bit 30) */
    if (Gs_SelBit28() && Gs_Bit30()) PENDING_NOTE(0x456ca0, "Challenge_CarCount");
    CW8(c, 0xb1a, (uint8_t)n);
    CW8(c, 0xb1b, (uint8_t)cls);
    if (!keep) attach_models(n, c);
    CW32(c, 0xa88, 0xb332);
    CW32(c, 0xa8c, CI32(c, 0xa88) + 0x8665);
    for (uint32_t i = 0; i < 4; i++) {
        const Obj *w = c->wheel[i];
        for (uint32_t k = 0; k < 3; k++) CW32(c, 0x3c0 + 0xc * i + 4 * k, w ? w->rest.m[12 + (int)k] : 0);
        if (Gs_Option(6)) CW32(c, 0x3c4 + 0xc * i, CI32(c, 0x3c4 + 0xc * i) - CI32(c, 0xa88));
    }
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x2d0 + 4 * k, pos[k]);
    CW8(c, 0xb1e, 0);
    /* the body rows from the model given, then the orientation (+0x00, +0x10, +0x20, through the self
       pointer at +0x750) from them and again from the second model */
    row(c, 0x360, obj, 0);
    row(c, 0x36c, obj, 1);
    row(c, 0x378, obj, 2);
    for (uint32_t r = 0; r < 3; r++)
        for (uint32_t k = 0; k < 3; k++) CW32(c, 0x10 * r + 4 * k, CI32(c, 0x360 + 0xc * r + 4 * k));
    for (int r = 0; r < 3; r++) row(c, 0x10 * (uint32_t)r, c->second, r);
    int32_t mass = handling(c, cls);
    bool fixed = false;
    if (Gs_SelBit26() && Gs_SelBits14() == 1 && CI8(c, 0xb1a) != 0) {
        switch (c->s[0xb1b]) {
        case 6: case 7: case 9: case 10: case 11: case 13:
            CW32(c, 0x788, 0x570a);
            mass = 0x3e80000;
            fixed = true;
            CW32(c, 0x794, 0x98b02);
        }
    }
    if (Gs_Option(6)) {
        CW32(c, 0x1fc, CI32(c, 0x1fc) + CI32(c, 0xa8c));
        CW32(c, 0x774, CI32(c, 0x774) + CI32(c, 0xa8c));
        CW32(c, 0x770, CI32(c, 0x770) + CI32(c, 0xa8c));
    }
    static const int32_t gears[8] = {0, 0x5555, 0x7878, 0x9ee5, 0xc28f, 0xe50d, 0x10aaa, (int32_t)0xffffaaab};
    for (uint32_t i = 0; i < 8; i++) CW32(c, 0x7dc + 4 * i, gears[i]);
    CW32(c, 0x788, CI32(c, 0x788) - 0x51e);
    if (Gs_Option(7)) {
        CW32(c, 0x788, (int32_t)((uint32_t)CI32(c, 0x788) << 1));
        CW32(c, 0x794, fx_mul(CI32(c, 0x794), 0x14ccc));
    }
    CW32(c, 0x75c, mass + 0x780000);
    CW32(c, 0x760, inv(CI32(c, 0x75c)));
    CW32(c, 0x824, 0x1333);
    CW32(c, 0x828, 0x5555);
    CW32(c, 0xb7c, (int32_t)Gs_ClassB7c(CI8(c, 0xb1b)));
    CW32(c, 0xb80, (int32_t)Gs_ClassB80(CI8(c, 0xb1b)));
    uint32_t su = setup_for(c, n, alt);
    int8_t s[7];
    for (uint32_t i = 0; i < 7; i++) s[i] = gms8(su + i);
    setup_suspension(c, pct(s[2]));
    CW32(c, 0x830, s[4] == '2' ? 0x8000 : pct(s[4]));
    int32_t diff;
    if (!CI32(c, 0xb80)) {
        switch (c->s[0xb1b]) {
        case 7: case 0xd: diff = 0; break;
        case 8: diff = 0x10000; break;
        case 10: diff = 0x1999; break;
        default: diff = 0x8000;
        }
    } else {
        diff = s[3] == '2' ? 0x8000 : fx_mul(s[3] * 0x10000, 0x83) + 0x6666;
        if (c->s[0xb1b] == 9) diff -= 0x2666;
    }
    CW32(c, 0x7b4, diff);
    CW32(c, 0x82c, fx_mul(pct(s[5]), 0x13333) + 0x3333);
    CW8(c, 0xb29, (uint8_t)s[0]);
    setup_gears(c, s[1]);
    int32_t br = pct(s[6]);   /* 0x43e530 */
    CW32(c, 0x804, fx_mul(br, 0xcccc) + 0x9999);
    CW32(c, 0x7fc, fx_mul(br, 0xa3d) + 0xccc);
    CW32(c, 0x800, fx_mul(br, 0x3333) + 0x1999);
    if (fixed) CW32(c, 0x7b4, 0x8000);
    CW32(c, 0x784, 0x83);
    CW32(c, 0x790, 0xccc);
    CW32(c, 0x798, inv(CI32(c, 0x794)));
    CW32(c, 0x834, 0xccc);
    CW32(c, 0x840, 0x1999);
    CW32(c, 0x844, 0x8000);
    CW8(c, 0xb1c, 3);
    CW16(c, 0xb16, 0x2aa);
    CW32(c, 0x820, 0x3333);
    CW32(c, 0x7bc, 0);
    for (uint32_t i = 0; i < 7; i++) CW32(c, 0x7c0 + 4 * i, inv(CI32(c, 0x7e0 + 4 * i)));
    CW32(c, 0x7b8, CI32(c, 0x7b4));
    int32_t h[3] = {fx_mul(CI32(c, 0x1f8), 0x8000), fx_mul(CI32(c, 0x1fc), 0x8000), fx_mul(CI32(c, 0x200), 0x8000)};
    CW32(c, 0x758, (int32_t)Fx_Sqrt((uint32_t)fx_mul(h[0], h[0]) + (uint32_t)fx_mul(h[1], h[1]) + (uint32_t)fx_mul(h[2], h[2])));
    CW32(c, 0x3fc, 1);
    CW32(c, 0x400, 2);
    CW32(c, 0x404, 1);
    int32_t a = CI32(c, 0x1f8) / 2, b = CI32(c, 0x1fc) / 2, d = CI32(c, 0x200) / 2;
    CW32(c, 0x204, a);
    CW32(c, 0x208, b);
    CW32(c, 0x20c, d);
    /* the box's corners in the car's frame, then a loop of 8 that does nothing */
    const int32_t corner[8][3] = {{a, -b, d}, {a, -b, -d}, {-a, -b, d}, {-a, -b, -d}, {a, b, d}, {a, b, -d}, {-a, b, d}, {-a, b, -d}};
    for (uint32_t i = 0; i < 8; i++)
        for (uint32_t k = 0; k < 3; k++) CW32(c, 0x210 + 0xc * i + 4 * k, corner[i][k]);
    Car_BoxCorners(c);
    gw32(0x53cad0, 0);
    gw32(0x53cad4, 0xffff8000u);
    gw32(0x53cad8, 0);
    CW32(c, 0x91c, 0);
    CW32(c, 0x920, 0x10000);
    CW32(c, 0x924, 0);
    if (!alt && CI8(c, 0xb1a) < (int)Gs_SelBits14()) {
        CW32(c, 0xb48, Champ_CarFlag((uint8_t)(Gs_Byte537ef6() + c->s[0xb1a])) ? 2 : 1);
    } else {
        CW32(c, 0xb48, 2);
    }
    CW32(c, 0xb50, 1);
    uint32_t m = Gs_Mode();
    if (m == 5 || m == 6 || m == 7 || (m == 4 && CI8(c, 0xb1a) != 0) || (Gs_SelBit28() && Gs_Bit30()) ||
        Gs_Byte52ea52() || m == 11 || m == 12)
        CW32(c, 0xb50, 0);
    if (Gs_SelBit25() && !Gs_ModeBit19()) PENDING_NOTE(0x408500, "Champ_DriverIndex");
    if (Gs_ModeBit17() && n == 1 && Gs_SelBit25()) CW32(c, 0xb50, 0);
    if (m == 4) CW32(c, 0xb50, 0);
    CW32(c, 0xb4c, 0);
    CW16(c, 0xafe, 0);
    CW32(c, 0xb98, 1);
    CW8(c, 0xb44, 0xff);
    gw32(0x53cadc, 0x9999);
    CW32(c, 0x78c, CI32(c, 0x788));
    CW32(c, 0xa98, 0x41c80000);   /* 25.0f: the step rate */
    if (Gs_ModeBit19() && CI8(c, 0xb1a) > 0) {
        CW32(c, 0xc18, 1);
        CW32(c, 0xc1c, 1);
    }
}
