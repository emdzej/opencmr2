/* The cars' loose parts (see parts.h). */
#include "sim/parts.h"
#include "sim/damage.h"
#include "sim/aerials.h"
#include "game/car_load.h"
#include "game/mem.h"
#include "game/effects.h"
#include "crt.h"
#include "fixmath.h"
#include "pending.h"
#include <math.h>
#include <string.h>

/* A part's record (0x1a0 bytes in the original; offsets in the comments). The original keeps two
   pointers at +0x04 and +0x08 to its own +0x0c and +0xcc; the port names those matrices. */
typedef struct {
    Obj *obj;              /* +0x000: the part's object (NULL: not loose) */
    FxMat local;           /* +0x00c: the part to its parent, this step's */
    FxMat prev;            /* +0x04c: the last step's */
    FxMat drawn;           /* +0x08c: blended between them for drawing */
    FxMat world;           /* +0x0cc: the part to the world (local times the parent) */
    const FxMat *parent;   /* +0x10c: the main model's local matrix (the car's drawn pose) */
    void (*fn)(void);      /* +0x110: the type's update */
    int32_t ext[3];        /* +0x114: the slot's half extents */
    int32_t hinge[3];      /* +0x120: the point it turns about, in the car's frame */
    int32_t pivot[3];      /* +0x12c: the slot's centre */
    int32_t vel[3];        /* +0x138: off its hinge, its velocity */
    int32_t disp[3];       /* +0x144: and how far it has moved */
    uint8_t flags;         /* +0x150: 1 loose, 2 swinging free, 4 broken off, 8 moving; high nibble the type */
    int32_t r154;          /* +0x154: cleared at the launch (the other types' updates) */
    int16_t ang;           /* +0x158: the swing angle */
    int32_t len;           /* +0x15c: the extents' length */
    int32_t r160;          /* +0x160: cleared at the launch */
    int32_t av[3];         /* +0x164: the angular velocity */
    int32_t inertia[3];    /* +0x170 */
    int32_t rows[9];       /* +0x17c: the three base rows the swing turns */
} Part;

static Part parts[4][CAR_MAX];        /* 0x590d7c + 4k: per type an array of 0x1a0-byte records */
static int nparts;                    /* 0x590c64 */
static Obj *part_obj[4][CAR_MAX];     /* 0x590b7c: the parts' objects, from the damage slots */
static uint8_t slot_of[4][CAR_MAX];   /* 0x590c24: their damage slots */
static int32_t thresh[4];             /* 0x590c44: the panel value that works a part loose */
static uint8_t panel_of[4];           /* 0x590c60: its damage panel */
static int32_t wobble[CAR_MAX];       /* 0x590b10: per car the wobble, towards a target */
static int16_t wob_ang;               /* 0x590c68: the current car's as an angle */
static const uint8_t *cin3[CAR_MAX];  /* 0x590b5c (and +0xc, +0x10 at 0x590b30, 0x590c00): .cin section 3 */
static Car *cur_car;                  /* 0x590d74 */
static const uint8_t *cur_dmg;        /* 0x590d78 */
static Part *cur;                     /* 0x590c20 */

static const double TO_ANG = 4096.0 / (2.0 * M_PI * 65536.0);   /* 0x511380: radians to angle units */
static const double DEG = 1.0 / 5760.0;                          /* 0x511300: degrees (x 5760) to units */

static int idx(const Car *c) { int n = CI8(c, 0xb1a); return n >= 0 && n < CAR_MAX ? n : 0; }
static int32_t dt(void) { return gms32(0x519c8c); }
static int32_t dmg(uint32_t o) { return DI32(cur_dmg, o); }
static int type_of(const Part *p) { return p->flags >> 4; }
/* the x87 FISTP of a double (to nearest), its low word kept */
static int16_t rnd16(double v, double k) { return (int16_t)(uint16_t)X87_MulRound(v, k); }

static uint32_t len3(const int32_t v[3]) { return Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2])); }
static int32_t dot3(const int32_t a[3], const int32_t b[3]) { return fx_mul(a[0], b[0]) + fx_mul(a[1], b[1]) + fx_mul(a[2], b[2]); }
static void scale3(int32_t o[3], const int32_t v[3], int32_t k) { for (int i = 0; i < 3; i++) o[i] = fx_mul(v[i], k); }
static void cross3(int32_t o[3], const int32_t a[3], const int32_t b[3])
{
    int32_t x = fx_mul(a[1], b[2]) - fx_mul(a[2], b[1]), y = fx_mul(a[2], b[0]) - fx_mul(a[0], b[2]),
            z = fx_mul(a[0], b[1]) - fx_mul(a[1], b[0]);
    o[0] = x, o[1] = y, o[2] = z;
}
static void car_v(int32_t v[3], const Car *c, uint32_t o) { for (uint32_t k = 0; k < 3; k++) v[k] = CI32(c, o + 4 * k); }

/* Vec_Mul3 0x4b9c20: v times the 3 x 3 of a matrix (row vector on the left) */
static void vec_mul3(int32_t o[3], const int32_t v[3], const int32_t *m)
{
    for (int k = 0; k < 3; k++) o[k] = fx_mul(m[8 + k], v[2]) + fx_mul(m[4 + k], v[1]) + fx_mul(m[k], v[0]);
}
/* Vec_RotateT 0x4b9da0: v through the transposed 3 x 3 */
static void vec_rot_t(int32_t o[3], const int32_t v[3], const int32_t *m)
{
    for (int r = 0; r < 3; r++) o[r] = fx_mul(m[4 * r + 2], v[2]) + fx_mul(m[4 * r + 1], v[1]) + fx_mul(m[4 * r], v[0]);
}
/* the car's orientation (+0x750 points at the car's own +0x00, rows of four) */
static void car_orient(int32_t m[12], const Car *c) { for (uint32_t i = 0; i < 12; i++) m[i] = CI32(c, 4 * i); }

/* 0x4bace0 / 0x4bad00 / 0x4bad20 / 0x4bacc0: a matrix's row 0, 1, 2, 3 set */
static void set_row(FxMat *m, int r, const int32_t v[3]) { m->m[4 * r] = v[0], m->m[4 * r + 1] = v[1], m->m[4 * r + 2] = v[2]; }

/* 0x469de0 with (s, 0, 0): a surface a scraping part sparks on */
static bool sparks_on(uint16_t s)
{
    switch (s) {
    case 1: case 6: case 7: case 8: case 9: case 10: case 11: case 16: case 24: case 25: case 27: case 28:
        return true;
    default:
        return false;
    }
}

/* ---- the set-up ---- */

/* Parts_Init 0x480a60 */
void Parts_Init(void)
{
    crt_srand(400);
    thresh[0] = thresh[1] = thresh[2] = 0x8000;
    thresh[3] = 0x3333;
    panel_of[0] = 0x16, panel_of[1] = 0x14, panel_of[2] = 0x15, panel_of[3] = 0x12;
    gw8(0x590bfc, 1), gw8(0x590bfd, 2);   /* read by 0x485690 / 0x485860 (sim/aerials.c) */
}

void Parts_ClearFlag(void) { gw32(0x590d70, 0); }   /* Parts_Clear 0x480a50 */

/* Parts_Register 0x480af0: a slot whose object is one of the four part types (0x51f888) is that part's */
void Parts_Register(const Car *c, Obj *o, int slot)
{
    for (int k = 3; k >= 0; k--)
        if ((uint8_t)o->type == gm8(0x51f888 + (uint32_t)k)) {
            part_obj[k][idx(c)] = o;
            slot_of[k][idx(c)] = (uint8_t)slot;
        }
}

int Parts_Slot(int k, int car) { return k >= 0 && k < 4 && car >= 0 && car < CAR_MAX ? slot_of[k][car] : 0xff; }   /* 0x484d10 */
Obj *Parts_Obj(int car, int k) { return k >= 0 && k < 4 && car >= 0 && car < CAR_MAX ? part_obj[k][car] : NULL; }   /* 0x484de0 */

/* Parts_Reset 0x480b40: the generator seeded; each of the car's taken records gives its object the
   rest matrix back and is let go. The flags and the object's passes are left as they are. */
void Parts_Reset(const Car *c)
{
    crt_srand(400);
    cur_car = (Car *)c;
    for (int k = 3; k >= 0; k--) {
        Part *p = &parts[k][idx(c)];
        if (!p->obj) continue;
        p->obj->local = p->obj->rest;
        p->obj = NULL;
    }
}

/* Part_Hide 0x480ac0: with the flag, the part's object drawn in no pass. Nothing here gives the
   passes back: the view's 0x46b8f0 (Car_Masks) sets the car's every frame. */
void Parts_Hide(const Car *c, int k, int32_t off)
{
    Obj *o = part_obj[k][idx(c)];
    if (o && off) o->mask = 0;
}

/* Parts_HideFallen 0x46b670: each part with the damage record's flag (+0x4b0 + 4k) hidden */
void Parts_HideFallen(const Car *c)
{
    for (int k = 0; k < 4; k++) Parts_Hide(c, k, Damage_Flag(c, k));
}

/* Parts_Alloc 0x480900: four arrays of n records and n blocks of 0xb4 bytes (0x590c6c, the aerials of
   sim/aerials.c); the port's records are static. Parts_Free 0x480870 is its clean-up. */
void Parts_Alloc(int n)
{
    nparts = n < CAR_MAX ? n : CAR_MAX;
    memset(parts, 0, sizeof parts);
}

/* Parts_CinLinks 0x4809e0: per listed car (from the last) section 3 of its .cin (for 0x484e00) */
void Parts_CinLinks(const int16_t *cars, int n)
{
    for (int i = n - 1; i >= 0; i--) {
        Car *c = Car_Get(cars[i]);
        cin3[idx(c)] = Car_CinSection(cars[i], 3);
    }
}

const uint8_t *Parts_Cin3(int car) { return car >= 0 && car < CAR_MAX ? cin3[car] : NULL; }

/* Parts_Empty 0x480980: every record zeroed, no part objects, every slot 0xff */
void Parts_Empty(void)
{
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < nparts; i++) memset(&parts[k][i], 0, sizeof parts[k][i]);
    memset(part_obj, 0, sizeof part_obj);
    memset(slot_of, 0xff, sizeof slot_of);
}

/* ---- the step ---- */

/* Part_Turn 0x481560: the base rows, turned by a (only above a speed of 0.03), as the record's rows */
static void part_turn(const uint16_t a[3])
{
    int32_t m[9];
    memcpy(m, cur->rows, sizeof m);
    if (CI32(cur_car, 0x778) > 0x7ae) Rows_Turn3(m, 3, a);
    for (int r = 0; r < 3; r++) set_row(&cur->local, r, m + 3 * r);
}

/* Part_Place 0x4815e0: the displacement moved on by the velocity; the position so that the hinge
   stays put under the turn, plus the displacement */
static void part_place(void)
{
    int32_t v[3], t[3];
    scale3(v, cur->vel, dt());
    for (int i = 0; i < 3; i++) cur->disp[i] += v[i];
    vec_mul3(t, cur->hinge, cur->local.m);
    for (int i = 0; i < 3; i++) t[i] = cur->hinge[i] - t[i] + cur->disp[i];
    set_row(&cur->local, 3, t);
}

/* Part_Drop 0x483010: no longer loose; the damage record's part flag set (0x469bf0) */
static void part_drop(void)
{
    cur->flags &= 0xfe;
    Damage_PartOff(cur_car, cur->flags >> 4);
}

/* Part_TooFar 0x482f30: once the displacement passes the car's size (+0x758) plus the part's, it falls off */
static bool part_too_far(void)
{
    if ((int32_t)len3(cur->disp) > CI32(cur_car, 0x758) + cur->len) {
        part_drop();
        return true;
    }
    return false;
}

/* Part_Unhinge 0x483050: past 0.8 on its panel, the part hangs from one end: the hinge moved along z
   by the half extent towards the less damaged side (panels 2/3 for type 1, else 0/1), swinging free */
static void part_unhinge(void)
{
    if (dmg(0x240 + 4u * panel_of[type_of(cur)]) <= 0xcccc) return;
    uint32_t a = type_of(cur) == 1 ? 2 : 0, b = a + 1;
    if (dmg(0x240 + 4 * a) > dmg(0x240 + 4 * b)) cur->hinge[2] -= cur->ext[2];
    else cur->hinge[2] += cur->ext[2];
    cur->flags |= 2;
    cur->ang = 0;
}

/* Part_Scrape 0x483100: on a surface that sparks under the part's corner wheel (+0xaae), a point at the
   part's low edge (its hinge, down twice the slot's y extent, along its third row 1.8 z extents
   towards the far end) in the car's orientation; sparks there against the scrape g, stronger with
   it (5 g, at most 1.0; none below 0.2): up to 4, 0.4 of them chips. */
static void part_scrape(const int32_t g[3], int corner)
{
    uint32_t s = slot_of[type_of(cur)][idx(cur_car)];
    if (!sparks_on((uint16_t)CI16(cur_car, 0xaae + 2 * (uint32_t)corner))) return;
    int32_t p[3] = {cur->hinge[0], cur->hinge[1], cur->hinge[2]}, t[3], at[3], m[12];
    p[1] -= fx_mul(0x20000, dmg(0x16c + 12 * s));
    int32_t k = fx_mul(0x1cccc, dmg(0x170 + 12 * s));
    if (0 < cur->hinge[2]) k = -k;
    scale3(t, cur->rows + 6, k);
    for (int i = 0; i < 3; i++) p[i] += t[i];
    car_orient(m, cur_car);
    vec_mul3(at, p, m);
    int32_t l = (int32_t)len3(g);
    if (l <= 0) return;
    int32_t dir[3];
    scale3(dir, g, (int32_t)(0x100000000LL / -l));
    int32_t str = fx_mul(l, 0x50000);
    if (str > 0x10000) str = 0x10000;
    else if (str <= 0x3333) return;
    /* the rows: against g, the ground normal (+0x48c), and their cross made unit */
    int32_t rows[9] = {dir[0], dir[1], dir[2], CI32(cur_car, 0x48c), CI32(cur_car, 0x490), CI32(cur_car, 0x494)};
    rows[6] = fx_mul(rows[1], rows[5]) - fx_mul(rows[2], rows[4]);
    rows[7] = fx_mul(rows[2], rows[3]) - fx_mul(rows[0], rows[5]);
    rows[8] = fx_mul(rows[0], rows[4]) - fx_mul(rows[1], rows[3]);
    Vec_Unit(rows + 6);
    Sparks_Add(str, at, cur_car, rows, 0x40000, 0x6666);
}

/* Part_Break 0x482ac0: off its hinge with the push f (the car's frame) as a tenth of its velocity;
   a type 0 part (never, in practice: types 1 and 3 call this) kept from moving down or back against
   its swing, then sped up five times; the displacement so far as the turn about the hinge would
   have moved it, the hinge to the centre; broken off, not swinging */
static void part_break(const int32_t f[3])
{
    int32_t v[3], m[12];
    car_orient(m, cur_car);
    vec_rot_t(v, f, m);
    scale3(cur->vel, v, 0x1999);
    if ((cur->flags & 0xf0) == 0) {
        if (cur->vel[1] < 0) cur->vel[1] = 0;
        uint16_t a = (uint16_t)cur->ang;
        int32_t dir[3] = {g_sin[a & 0xfff], g_sin[(uint16_t)(a + 0x400) & 0xfff], 0};
        int32_t d = dot3(dir, cur->vel);
        if (d < 0) {
            uint32_t l = len3(cur->vel);
            scale3(dir, dir, d);
            for (int i = 0; i < 3; i++) cur->vel[i] -= dir[i];
            Vec_Unit(cur->vel);
            scale3(cur->vel, cur->vel, fx_mul((int32_t)l, 0x50000));
        }
    }
    int32_t r[3], t[3];
    for (int i = 0; i < 3; i++) r[i] = cur->hinge[i] - cur->pivot[i];
    vec_mul3(t, r, cur->local.m);
    for (int i = 0; i < 3; i++) cur->disp[i] = cur->hinge[i] - (t[i] + cur->pivot[i]);
    memcpy(cur->hinge, cur->pivot, sizeof cur->hinge);
    cur->flags |= 4;
    cur->flags &= 0xfd;
}

/* Part_Swing 0x4814d0 (type 0): turned about z by the wobble, its sign from the first row's x
   (type 2's sense is the other way; 0x4814d0 is type 0's only) */
static void part_swing(void)
{
    uint16_t a[3] = {0, 0, 0};
    if ((cur->flags & 0xf0) == 0x20) a[2] = (uint16_t)(cur->rows[0] < 0 ? wob_ang : -wob_ang);
    else a[2] = (uint16_t)(0 < cur->rows[0] ? wob_ang : -wob_ang);
    part_turn(a);
    part_place();
}

/* Part_Hinged 0x4816f0 (type 1). Nothing once too far. For the car kinds 9 and 11 (+0xb1b) only a
   wobble about y (once and twice the wobble, negated). Otherwise: worked loose further (0x483050)
   while neither swinging nor broken off; swinging, it is pulled back towards its rest angle (a
   spring of 4 per unit off), pushed by the car's acceleration (+0x408 less +0x414) and, with the
   corner wheel's contact (+0xbac), by the velocity along the ground (0.2 of it, which also scrapes
   0x483100); a push of more than 0.3 across the car (+0x360) swings the angle (+0x158) by up to 5
   degrees, held to one side, and past 0xe3 units it breaks off. The pushes, -10 and -4 times (and
   gravity of 0.25), or -10 and -20 when breaking, through the part's frame, give a torque about the
   hinge times the inertias (+0x170); the angular velocity damped by 0.5 plus 0.79 (1 - dt, at most
   0.5) and turned into the base rows (the torque's half step taken back). Swinging and moving, the
   third row is held to one side of x and above y (that velocity stopped) and the rows made square
   again. Then the drawn rows: turned by the wobble (whole about z while not swinging, half about
   x and y while it swings) unless broken off, and placed (0x4815e0); breaking, off the hinge. */
static void part_hinged(void)
{
    Part *p = cur;
    Car *c = cur_car;
    if (part_too_far()) return;
    uint8_t kind = c->s[0xb1b];
    if (kind == 9 || kind == 11) {
        int32_t w = fx_mul(wob_ang * 5760, kind == 9 ? 0x10000 : 0x20000);
        uint16_t a[3] = {0, (uint16_t)rnd16((double)w, -DEG), 0};
        part_turn(a);
        part_place();
        return;
    }
    if (!(p->flags & 6)) part_unhinge();
    int32_t A[3] = {0, 0, 0}, F[3] = {0, 0, 0}, S[3] = {0, 0, 0}, G[3] = {0, 0, 0}, kept[3] = {0, 0, 0};
    int32_t T[3], D[3];
    bool brk = false;
    bool t1 = (p->flags & 0xf0) == 0x10;
    if (p->flags & 2) {
        /* the spring towards the rest angle */
        uint16_t a = (uint16_t)p->ang;
        int32_t sn = g_sin[a & 0xfff], cs = g_sin[(a + 0x400) & 0xfff];
        if (0 < p->hinge[2]) T[0] = sn - p->rows[6], T[1] = -p->rows[7], T[2] = cs - p->rows[8];
        else T[0] = p->rows[6] - sn, T[1] = p->rows[7], T[2] = p->rows[8] - cs;
        int32_t l = (int32_t)len3(T);
        if (0 < l) {
            int32_t r = (int32_t)(0x100000000LL / l);
            scale3(T, T, r);
            scale3(S, T, -fx_mul(l, 0x40000));
        }
        /* the car's acceleration, and the ground's push at the part's corner */
        for (uint32_t i = 0; i < 3; i++) D[i] = CI32(c, 0x408 + 4 * i) - CI32(c, 0x414 + 4 * i);
        uint8_t corner = (uint8_t)((0 < p->hinge[2]) + (t1 ? 2 : 0));
        if (CI32(c, 0xbac + 4u * corner)) {
            int32_t n[3], v[3];
            car_v(n, c, 0x48c), car_v(v, c, 0x408);
            int32_t d = dot3(n, v);
            scale3(G, n, d);
            for (int i = 0; i < 3; i++) G[i] = v[i] - G[i];
            scale3(G, G, 0x3333);
            part_scrape(G, corner);
        }
        for (int i = 0; i < 3; i++) T[i] = G[i] + D[i];
        int32_t lt = (int32_t)len3(T);
        if (0x4ccc < lt) {
            int32_t ax[3];
            car_v(ax, c, 0x360);
            int32_t d = dot3(ax, T), s;
            bool pos;   /* the angle held at or below 0 (else at or above) */
            if (t1) {
                if (p->hinge[2] < 0) s = -0x10000, pos = true;
                else s = 0x10000, pos = false;
            } else {
                if (p->hinge[2] < 0) s = -0x10000, pos = false;
                else s = 0x10000, pos = true;
            }
            if (d < 0) s = fx_mul(s, -0x10000), d = -d;
            if (0x4ccc < d) {
                int32_t k = fx_mul(d - 0x4ccc, (int32_t)(0x100000000LL / 0x4ccc));
                if (k > 0x10000) k = 0x10000;
                k = fx_mul(fx_mul(k, 0x50000), s);
                p->ang = rnd16((double)(p->ang * 5760 + k), DEG);
                if (pos ? 0 < p->ang : p->ang < 0) p->ang = 0;
                if ((p->ang < 0 ? -p->ang : p->ang) > 0xe3) brk = true;
            }
            /* (the original makes T unit times 0.3 here, then overwrites it) */
        }
        scale3(D, D, -0xa0000);
        for (int i = 0; i < 3; i++) F[i] += D[i];
        if (!brk) {
            scale3(G, G, -0x40000);
            for (int i = 0; i < 3; i++) F[i] += G[i];
            F[1] -= 0x4000;
        } else {
            scale3(G, G, -0x140000);
            for (int i = 0; i < 3; i++) F[i] += G[i];
            memcpy(kept, F, sizeof kept);
        }
        /* the angular velocity damped */
        int32_t k = 0x10000 - dt();
        if (k < 0) k = 0;
        else if (k > 0x8000) k = 0x8000;
        k = fx_mul(k, 0xc937) + 0x8000;
        scale3(p->av, p->av, k);
        /* the push in the part's frame, the torque about the hinge */
        vec_rot_t(T, F, p->world.m);
        memcpy(F, T, sizeof F);
        if (!brk) for (int i = 0; i < 3; i++) F[i] += S[i];
        else memset(p->av, 0, sizeof p->av);
        for (int i = 0; i < 3; i++) T[i] = p->pivot[i] - p->hinge[i];
        int32_t X[3];
        cross3(X, F, T);
        if (!brk) X[2] = 0;
        for (int i = 0; i < 3; i++) A[i] = -fx_mul(X[i], p->inertia[i]);
        scale3(A, A, dt());
        for (int i = 0; i < 3; i++) p->av[i] += A[i];
    }
    if (p->flags & 6) {
        p->flags |= 8;
        if (!brk && !(p->flags & 4) && CI32(c, 0x778) <= 0x7ae) p->flags &= 0xf7;
        if ((p->flags & 2) && !(p->flags & 8)) memset(p->av, 0, sizeof p->av);
        else {
            int32_t W[3];
            scale3(W, p->av, dt());
            scale3(A, A, dt() / 2);
            uint16_t a[3];
            for (int i = 0; i < 3; i++) W[i] -= A[i], a[i] = (uint16_t)rnd16((double)W[i], TO_ANG);
            Rows_Turn3(p->rows, 3, a);
        }
    }
    if ((p->flags & 2) && (p->flags & 8) && !brk) {
        /* the third row kept to its side of x and above (or below) y */
        int32_t *r0 = p->rows, *r1 = p->rows + 3, *r2 = p->rows + 6;
        bool hit = false, fix = false;
        bool pos = 0 < p->hinge[2];
        if (t1 ? (pos ? r2[0] < 0 : 0 < r2[0]) : (pos ? 0 < r2[0] : r2[0] < 0)) r2[0] = 0, p->av[0] = 0, hit = true;
        if (pos ? r2[1] < 0 : 0 < r2[1]) r2[1] = 0, p->av[1] = 0, fix = true;
        if (hit || fix) {
            Vec_Unit(r2);
            r1[0] = 0;
        }
        int32_t d = dot3(r1, r2);
        for (int i = 0; i < 3; i++) T[i] = r1[i] - fx_mul(r2[i], d);
        Vec_Unit(T);
        memcpy(r1, T, sizeof T);
        cross3(T, r1, r2);
        Vec_Unit(T);
        memcpy(r0, T, sizeof T);
    }
    if (!(p->flags & 4)) {
        uint16_t a[3] = {0, 0, 0};
        int32_t w = wob_ang * 5760;
        if (!(p->flags & 2)) {
            int16_t v = rnd16((double)fx_mul(w, 0x10000), DEG);
            a[2] = (uint16_t)(t1 ? v : -v);
        } else {
            int16_t v = rnd16((double)fx_mul(w, 0x8000), DEG);
            if (t1) a[0] = (uint16_t)(0 < p->hinge[2] ? -v : v), a[1] = (uint16_t)(0 < p->hinge[2] ? v : -v);
            else a[0] = a[1] = (uint16_t)(0 < p->hinge[2] ? -v : v);
        }
        part_turn(a);
    } else
        for (int r = 0; r < 3; r++) set_row(&p->local, r, p->rows + 3 * r);
    part_place();
    if (brk) part_break(kept);
}

/* the wobble (16.16 degrees: the angle times 5760) times k, as an angle unit (0x511300, or 0x511308
   negated) */
static int16_t wob_turn(int32_t k, bool neg) { return rnd16((double)fx_mul(wob_ang * 5760, k), neg ? -DEG : DEG); }

/* the base rows made square again: r0 unit, r2 square to it, r1 = r2 x r0 */
static void rows_square(int32_t *rows)
{
    int32_t *r0 = rows, *r1 = rows + 3, *r2 = rows + 6, t[3];
    Vec_Unit(r0);
    int32_t d = dot3(r2, r0);
    scale3(t, r0, d);
    for (int i = 0; i < 3; i++) r2[i] -= t[i];
    Vec_Unit(r2);
    cross3(t, r2, r0);
    Vec_Unit(t);
    memcpy(r1, t, sizeof t);
}

/* Part_Flap 0x484310 (type 2). Kinds 9 and 11 only wobble (about y once, about x three times). The
   car's acceleration (+0x408 less +0x414) times -6 (x negated unless kind 8 for the type 12 objects,
   which are all of them) less 0.328 of gravity, through the part's frame, gives a torque about the
   hinge (the arm to the slot's centre, 0.2 up, no x for kind 8); its z starts the part moving and
   then drives the spin about z (damped by 0.893 a step, times the z inertia, the half step taken
   back). The first row's y is held at or above 0 (kind 8; else at or below), its x at least 0.4
   (kind 8; else 0.8), the spin stopped or bounced back at 0.6 (stopped, and the part still, below
   0.06); after a stop the rows are made square. Then the wobble about z (negated) and the place. */
static void part_type2(void)
{
    Part *p = cur;
    Car *c = cur_car;
    uint8_t kind = c->s[0xb1b];
    uint16_t a[3] = {0, 0, 0};
    if (kind == 9) {
        a[1] = (uint16_t)wob_turn(0x10000, true);
        part_turn(a);
        part_place();
        return;
    }
    if (kind == 11) {
        a[0] = (uint16_t)wob_turn(0x30000, true);
        part_turn(a);
        part_place();
        return;
    }
    int32_t F[3], L[3], r[3], X[3];
    for (uint32_t i = 0; i < 3; i++) F[i] = fx_mul(CI32(c, 0x408 + 4 * i) - CI32(c, 0x414 + 4 * i), -0x60000);
    if (kind != 8 && p->obj && (uint8_t)p->obj->type == 0x0c) F[0] = -F[0];
    F[1] -= 0x53f7;
    vec_rot_t(L, F, p->world.m);
    r[0] = kind == 8 ? 0 : p->pivot[0] - p->hinge[0];
    r[1] = 0x3333;
    r[2] = p->pivot[2] - p->hinge[2];
    cross3(X, L, r);
    if (!(p->flags & 8) && 0 < X[2]) p->flags |= 8;
    if (p->flags & 8) {
        scale3(p->av, p->av, 0xe49b);
        int32_t dA = fx_mul(-fx_mul(X[2], p->inertia[2]), dt());
        p->av[2] += dA;
        a[2] = (uint16_t)rnd16((double)(fx_mul(p->av[2], dt()) - fx_mul(dA, dt() / 2)), TO_ANG);
        Rows_Turn3(p->rows, 3, a);
        bool stop = false, square = true;
        if (kind == 8) {
            if (p->rows[1] < 0) {
                p->rows[1] = 0;
                stop = true;
                if (p->av[2] < 0) {
                    if (p->av[2] < -0xf5c) p->av[2] = -fx_mul(p->av[2], 0x9999);
                    else p->av[2] = 0, p->flags &= 0xf7;
                }
            }
            if (p->rows[0] < 0x6666) {
                p->rows[0] = 0x6666;
                if (0 < p->av[2]) p->av[2] = 0;
            } else if (!stop)
                square = false;
        } else {
            if (0 < p->rows[1]) {
                p->rows[1] = 0;
                stop = true;
                if (0 < p->av[2]) {
                    if (p->av[2] < 0xf5d) p->av[2] = 0, p->flags &= 0xf7;
                    else p->av[2] = -fx_mul(p->av[2], 0x9999);
                }
            }
            if (p->rows[0] < 0xcccc) {
                p->rows[0] = 0xcccc;
                if (p->av[2] < 0) p->av[2] = 0;
            } else if (!stop)
                square = false;
        }
        if (square) rows_square(p->rows);
    }
    memset(a, 0, sizeof a);
    a[2] = (uint16_t)wob_turn(0x10000, true);
    part_turn(a);
    part_place();
}

/* .cin section 3's first vector (the hinge's stop direction) */
static void cin_axis(int32_t n[3])
{
    const uint8_t *q = cin3[idx(cur_car)];
    for (int i = 0; i < 3; i++) n[i] = q ? (int32_t)((uint32_t)q[4 * i] | q[4 * i + 1] << 8 | q[4 * i + 2] << 16 | (uint32_t)q[4 * i + 3] << 24) : 0;
}

/* Part_Hatch 0x483570 (type 3, swinging free from its loosening). Kind 8 only wobbles (about z, five
   times, negated). Nothing once too far. On its hinge: the spring towards the rest angle (as type
   1's, 4 per unit off); the car's acceleration times -5, -10, -5 breaks it off when its flat part
   passes 2.5 (squared 0x64000) with its panel (+0x288) past 0.5, else it pushes with 0.25 of gravity;
   the spin damped (as type 1's), the push through the part's frame (with the spring unless breaking,
   which stops the spin) gives a torque about the hinge times the inertias (none about z). It keeps
   moving while the car does (+0x778 above 0x7ae) or it is off. Moving, the spin turns the base rows
   (the half step taken back). Still on its hinge: past the .cin's stop direction on the hinge's side
   the third row is laid square to it (the first row's y cleared, the rows made square, the spin left
   along the stop direction only), then the wobble about x (half, negated for a hinge at negative z)
   and the place; off it or breaking, the rows as they are. Breaking, off the hinge with the push. */
static void part_type3(void)
{
    Part *p = cur;
    Car *c = cur_car;
    uint16_t a[3] = {0, 0, 0};
    if (c->s[0xb1b] == 8) {
        a[2] = (uint16_t)wob_turn(0x50000, true);
        part_turn(a);
        part_place();
        return;
    }
    if (part_too_far()) return;
    bool brk = false;
    int32_t A[3] = {0, 0, 0}, K[3] = {0, 0, 0};
    if (!(p->flags & 4)) {
        int32_t S[3] = {0, 0, 0}, T[3], D[3];
        uint16_t g = (uint16_t)p->ang;
        int32_t sn = g_sin[g & 0xfff], cs = g_sin[(uint16_t)(g + 0x400) & 0xfff];
        if (0 < p->hinge[2]) T[0] = sn - p->rows[6], T[1] = -p->rows[7], T[2] = cs - p->rows[8];
        else T[0] = p->rows[6] - sn, T[1] = p->rows[7], T[2] = p->rows[8] - cs;
        int32_t l = (int32_t)len3(T);
        if (0 < l) {
            scale3(T, T, (int32_t)(0x100000000LL / l));
            scale3(S, T, -fx_mul(l, 0x40000));
        }
        for (uint32_t i = 0; i < 3; i++) D[i] = CI32(c, 0x408 + 4 * i) - CI32(c, 0x414 + 4 * i);
        D[0] = fx_mul(D[0], -0x50000), D[1] = fx_mul(D[1], -0xa0000), D[2] = fx_mul(D[2], -0x50000);
        if (fx_mul(D[0], D[0]) + fx_mul(D[2], D[2]) > 0x64000 && dmg(0x288) > 0x8000) {
            memcpy(K, D, sizeof K);
            brk = true;
        } else
            D[1] -= 0x4000;
        int32_t k = 0x10000 - dt();
        if (k < 0) k = 0;
        else if (k > 0x8000) k = 0x8000;
        k = fx_mul(k, 0xc937) + 0x8000;
        scale3(p->av, p->av, k);
        int32_t F[3], r[3], X[3];
        vec_rot_t(F, D, p->world.m);
        if (!brk) for (int i = 0; i < 3; i++) F[i] += S[i];
        else memset(p->av, 0, sizeof p->av);
        for (int i = 0; i < 3; i++) r[i] = p->pivot[i] - p->hinge[i];
        cross3(X, F, r);
        for (int i = 0; i < 3; i++) A[i] = -fx_mul(X[i], p->inertia[i]);
        scale3(A, A, dt());
        for (int i = 0; i < 3; i++) p->av[i] += A[i];
        p->av[2] = 0;
    }
    p->flags |= 8;
    if (!brk && !(p->flags & 4) && CI32(c, 0x778) <= 0x7ae) p->flags &= 0xf7;
    if (p->flags & 8) {
        int32_t W[3], H[3];
        scale3(W, p->av, dt());
        scale3(H, A, dt() / 2);
        for (int i = 0; i < 3; i++) a[i] = (uint16_t)rnd16((double)(W[i] - H[i]), TO_ANG);
        Rows_Turn3(p->rows, 3, a);
    } else
        memset(p->av, 0, sizeof p->av);
    if (!brk && !(p->flags & 4)) {
        int32_t N[3], *r0 = p->rows, *r1 = p->rows + 3, *r2 = p->rows + 6, t[3];
        cin_axis(N);
        int32_t d = dot3(N, r2);
        if ((p->flags & 8) && ((p->hinge[2] < 0 && d < 0) || (0 < p->hinge[2] && 0 < d))) {
            scale3(t, N, d);
            for (int i = 0; i < 3; i++) r2[i] -= t[i];
            Vec_Unit(r2);
            r0[1] = 0;
            int32_t d2 = dot3(r0, r2);
            scale3(t, r2, d2);
            for (int i = 0; i < 3; i++) r0[i] -= t[i];
            Vec_Unit(r0);
            cross3(t, r2, r0);
            Vec_Unit(t);
            memcpy(r1, t, sizeof t);
            int32_t dv = dot3(p->av, N);
            scale3(p->av, N, dv);
        }
        int32_t w = fx_mul(wob_ang * 5760, 0x8000);
        if (p->hinge[2] < 0) w = -w;
        memset(a, 0, sizeof a);
        a[0] = (uint16_t)rnd16((double)w, DEG);
        part_turn(a);
    } else
        for (int r = 0; r < 3; r++) set_row(&p->local, r, p->rows + 3 * r);
    part_place();
    if (brk) part_break(K);
}

/* Part_Take 0x4813b0: a free record takes the car's part k: its object's local and world matrices,
   the main model's local as the parent, the second model's world as its own, the base rows */
static bool part_take(int k)
{
    Obj *o = part_obj[k][idx(cur_car)];
    if (cur->obj || !o) return false;
    cur->obj = o;
    cur->local = o->local;
    cur->world = o->world;
    cur->parent = cur_car->main ? &cur_car->main->local : NULL;
    if (cur_car->second) cur->world = cur_car->second->world;
    for (int r = 0; r < 3; r++) memcpy(cur->rows + 3 * r, o->local.m + 4 * r, 3 * sizeof(int32_t));
    return true;
}

/* Part_Loosen 0x480e50: part k taken and loose (the object's inherit flag off), its type's update
   and its hinge: the slot's centre (+0xb4 + 12s) moved by its extents (+0x168 + 12s) to an edge per
   type and car kind (+0xb1b, kinds 8, 9, 11 their own), the swing angle (0x288 units for 0 and 2),
   the inertias (0.066, 0.12, 0.425; type 1 0.066 on z too); its length, centre and extents kept,
   the motion cleared */
static void part_loosen(int k)
{
    uint32_t s = slot_of[k][idx(cur_car)];
    cur = &parts[k][idx(cur_car)];
    if (!part_take(k)) return;
    cur->flags = (uint8_t)((cur->flags & 0xf) | k << 4);
    cur->flags |= 1;
    cur->flags &= 0xfd, cur->flags &= 0xfb, cur->flags &= 0xf7;
    cur->obj->inherit = 0;
    const uint32_t ex = 0x168 + 12 * s, ey = ex + 4, ez = ex + 8;
    int32_t ox = 0, oy = 0, oz = 0;
    uint8_t kind = cur_car->s[0xb1b];
    bool common = true;
    switch (k) {
    case 0:
        ox = -dmg(ex), oy = dmg(ey);
        cur->ang = 0x288;
        cur->fn = part_swing;
        break;
    case 1:
        if (kind == 9 || kind == 11) ox = dmg(ex), oz = -dmg(ez);
        else ox = fx_mul(dmg(ex), 0x8000), oy = dmg(ey);
        cur->ang = 0;
        cur->fn = part_hinged;
        cur->inertia[0] = 0x10e5, cur->inertia[1] = 0x1eb8, cur->inertia[2] = 0x10e5;
        common = false;
        break;
    case 2:
        if (kind == 11) oy = dmg(ey), oz = -dmg(ez);
        else if (kind == 9) ox = -dmg(ex), oz = dmg(ez);
        else if (kind == 8) ox = -dmg(ex), oy = -dmg(ey);
        else ox = dmg(ex), oy = dmg(ey);
        cur->ang = 0x288;
        cur->fn = part_type2;
        break;
    case 3:
        cur->flags |= 2;
        if (kind == 8) oy = dmg(ey);
        else if (dmg(0x248) > dmg(0x24c)) oy = -dmg(ey), oz = dmg(ez);
        else oy = -dmg(ey), oz = -dmg(ez);
        cur->ang = 0;
        cur->fn = part_type3;
        break;
    }
    if (common) cur->inertia[0] = 0x10e5, cur->inertia[1] = 0x1eb8, cur->inertia[2] = 0x6ccc;
    const uint32_t cx = 0xb4 + 12 * s;
    cur->hinge[0] = dmg(cx) + ox, cur->hinge[1] = dmg(cx + 4) + oy, cur->hinge[2] = dmg(cx + 8) + oz;
    int32_t e[3] = {dmg(ex), dmg(ey), dmg(ez)};
    cur->len = (int32_t)len3(e);
    for (int i = 0; i < 3; i++) cur->pivot[i] = dmg(cx + 4 * (uint32_t)i), cur->ext[i] = e[i];
    memset(cur->vel, 0, sizeof cur->vel);
    memset(cur->disp, 0, sizeof cur->disp);
    memset(cur->av, 0, sizeof cur->av);
    cur->r154 = 0, cur->r160 = 0;
}

/* Parts_CheckLoose 0x480de0: from the last, a part still on (no damage flag, no record taken) whose
   panel has passed its threshold is worked loose */
static void parts_check(void)
{
    for (int k = 3; k >= 0; k--)
        if (!Damage_Flag(cur_car, k) && !parts[k][idx(cur_car)].obj && thresh[k] < dmg(0x240 + 4u * panel_of[k]))
            part_loosen(k);
}

/* Parts_Wobble 0x480cb0: the car's wobble moves towards a target (by 0.06 dt at most): 0.2 dt of the
   speed's share (+0x778 over 1.5, at most 1.0) times a value from the position (|x + z| mod 1024
   as 0..1); the angle the parts swing by from it (radians to units, rounded) */
static void parts_wobble(void)
{
    int32_t k = fx_mul(CI32(cur_car, 0x778), (int32_t)(((int64_t)0x10000 << 16) / 0x18000));
    if (k > 0x10000) k = 0x10000;
    int32_t step = fx_mul(fx_mul(k, dt()), 0xf5c);
    int32_t amp = fx_mul(fx_mul(k, dt()), 0x3333);
    int32_t x = (int32_t)((uint32_t)CI32(cur_car, 0x2d0) + (uint32_t)CI32(cur_car, 0x2d8));
    if (x < 0) x = (int32_t)(0u - (uint32_t)x);
    x %= 1024;   /* INT_MIN stays negative, as the original's */
    int32_t t = fx_mul(amp, (int32_t)((uint32_t)x << 6));
    int32_t *w = &wobble[idx(cur_car)];
    int32_t d = t - *w;
    if ((d < 0 ? -d : d) > step) t = 0 < d ? *w + step : *w - step;
    *w = t;
    wob_ang = rnd16((double)t, TO_ANG);
}

/* Parts_Step 0x480bb0: per listed car (from the last) the loose check, the wobble, then each loose
   part (from the last): its matrix kept as the last step's, its world from the parent, its update;
   then the car's glass pieces (0x49ab20); after the cars 0x484e00 */
void Parts_Step(const CarList *l)
{
    for (int i = l->n - 1; i >= 0; i--) {
        cur_car = Car_Get(l->car[i]);
        cur_dmg = Damage_Get(CI8(cur_car, 0xb1a));
        parts_check();
        parts_wobble();
        for (int k = 3; k >= 0; k--) {
            cur = &parts[k][idx(cur_car)];
            if (!cur->obj || !(cur->flags & 1)) continue;
            cur->prev = cur->local;
            if (cur->parent) Mat_Mul(&cur->world, &cur->local, cur->parent);
            if (cur->fn) cur->fn();
        }
        Glass_Pieces(cur_car);   /* 0x49ab20 */
    }
    Aerials_Step(l);   /* 0x484e00 */
}

/* Frame_Blend 0x4224e0 with mode 0 (as in game/camera.c): forward by k, up by k made unit, side from
   them, forward square to both, the position by k */
static int32_t lerp(int32_t a, int32_t b, int32_t k) { return a + fx_mul(b - a, k); }
static void blend(int32_t *o, const int32_t *a, const int32_t *b, int32_t k)
{
    int32_t f[3], u[3], s[3], p[3];
    for (int i = 0; i < 3; i++) f[i] = lerp(a[i], b[i], k), u[i] = lerp(a[4 + i], b[4 + i], k), p[i] = lerp(a[12 + i], b[12 + i], k);
    Vec_Unit(u);
    cross3(s, f, u);
    Vec_Unit(s);
    cross3(f, u, s);
    memset(o, 0, 16 * sizeof *o);
    o[15] = 0x10000;
    for (int i = 0; i < 3; i++) o[i] = f[i], o[4 + i] = u[i], o[8 + i] = s[i], o[12 + i] = p[i];
}

/* Parts_Interp 0x484d30: per car (from the last) each loose part's drawn matrix blended between its
   last two steps, its 3 x 4 into the object's local matrix (0x4bab80) */
void Parts_Interp(int32_t frac)
{
    for (int i = g_all.n - 1; i >= 0; i--) {
        int n = idx(Car_Get(g_all.car[i]));
        for (int k = 3; k >= 0; k--) {
            Part *p = &parts[k][n];
            if (!p->obj || !(p->flags & 1)) continue;
            blend(p->drawn.m, p->prev.m, p->local.m, frac);
            for (int r = 0; r < 4; r++) memcpy(p->obj->local.m + 4 * r, p->drawn.m + 4 * r, 3 * sizeof(int32_t));
        }
    }
}

bool Parts_Active(int car, int k)
{
    const Part *p = &parts[k & 3][car >= 0 && car < CAR_MAX ? car : 0];
    return p->obj && (p->flags & 1);
}

uint8_t Parts_Flags(int car, int k) { return parts[k & 3][car >= 0 && car < CAR_MAX ? car : 0].flags; }
