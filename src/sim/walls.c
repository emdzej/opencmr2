/* The cars against the stage's walls (see walls.h). The original works through globals at 0x5918d0-
   0x591ae8 (the car, the wall, its ends and normal, the corners' distances, the strongest hit); they
   are the fields of W here, named after their addresses where the meaning is open. */
#include "sim/walls.h"
#include "world/fin.h"
#include "world/section.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "pending.h"
#include "sim/scenery.h"
#include "sim/damage.h"
#include <math.h>

static int32_t box_rec[CAR_MAX][B_INTS];

static struct {
    Car *car;                   /* 0x5918dc */
    int32_t *box;               /* 0x591984 */
    const Wall *wall;           /* 0x59190c */
    int32_t r;                  /* 0x5919b8: 1.2 times the car's radius (+0x758) */
    int32_t a[3], b[3];         /* 0x591ae0, 0x591ac0: the wall's ends */
    int32_t dax, daz, dbx, dbz; /* 0x5918d0, 0x59195c, 0x5918d4, 0x591960: the ends from the car */
    int32_t n[3];               /* 0x591910 */
    int back;                   /* 0x59192c: the car's last position on the normal's side (or on it) */
    int32_t dist[4];            /* 0x5919a4: the box corners' distances to the wall's plane */
    uint8_t nneg, npos;         /* 0x591934, 0x591935: corners behind, in front */
    uint8_t nin, nout;          /* 0x591944, 0x591945: of those within the segment, ... */
    uint8_t in[4], out[4];      /* 0x591988, 0x59198c: ... their corners */
    uint32_t deep;              /* 0x5919b4: the corner the push is measured at */
    const Wall *deep_wall;      /* 0x591948 */
    int hit;                    /* 0x591930: the strongest hit of the walk so far */
    int32_t hit_imp[3];         /* 0x5918e0 */
    int32_t hit_pt[3];          /* 0x591ad0 */
    int32_t hit_n[3];           /* 0x591938 */
    uint8_t hit_kind;           /* 0x59199c: 0 a face, 2 an end */
    int32_t hit_mag;            /* 0x5919a0 */
    int32_t dir[3];             /* 0x591990 */
} W;

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static int32_t dot3(const int32_t a[3], const int32_t b[3]) { return fx_mul(a[0], b[0]) + fx_mul(a[1], b[1]) + fx_mul(a[2], b[2]); }
static uint32_t len3(const int32_t v[3]) { return Fx_Sqrt((uint32_t)fx_mul(v[0], v[0]) + (uint32_t)fx_mul(v[1], v[1]) + (uint32_t)fx_mul(v[2], v[2])); }
static int32_t tbl00(int type) { return gms32(0x51fb00 + 4 * (uint32_t)(int8_t)type); }   /* restitution */
static int32_t tbl68(int type) { return gms32(0x51fb68 + 4 * (uint32_t)(int8_t)type); }   /* friction */

/* 0x4b9da0: v through the transposed 3 x 3 of the car's orientation (+0x750, its rows at +0x00) */
static void rot_t(int32_t out[3], const int32_t v[3])
{
    const Car *c = W.car;
    for (uint32_t r = 0; r < 3; r++)
        out[r] = fx_mul(CI32(c, 16 * r + 8), v[2]) + fx_mul(CI32(c, 16 * r + 4), v[1]) + fx_mul(CI32(c, 16 * r), v[0]);
}

/* 0x486fc0: the four flat corners from the axes and half lengths, at the highest y */
static void box_corners(int32_t *b, const int32_t pos[3])
{
    int32_t f[3], s[3];
    for (int k = 0; k < 3; k++) f[k] = fx_mul(b[B_FWD + k], b[B_EXT_F]), s[k] = fx_mul(b[B_SIDE + k], b[B_EXT_S]);
    for (int k = 0; k < 3; k++) {
        b[B_CORNER + k] = f[k] - s[k];
        b[B_CORNER + 3 + k] = f[k] + s[k];
        b[B_CORNER + 6 + k] = -f[k] - s[k];
        b[B_CORNER + 9 + k] = -f[k] + s[k];
    }
    for (int i = 0; i < 4; i++) {
        int32_t *p = &b[B_CORNER + 3 * i];
        p[0] += pos[0], p[1] += pos[1], p[2] += pos[2];
        p[1] = b[B_YMAX];
    }
}

/* 0x486c30: the car's flat box, once a step: the forward row (+0x360, or the up row +0x36c when the car
   points within 8 degrees of straight up or down) flattened and made unit, the side axis across it, the
   eight box corners' (+0x270) furthest reach along both and their y range. */
static void box_make(int32_t *b, Car *c)
{
    if (b[B_VALID]) return;
    b[B_EXT_F] = b[B_EXT_S] = b[B_YMAX] = b[B_YMIN] = 0;
    uint32_t row = iabs(CI32(c, 0x364)) < 0xfd71 ? 0x360 : 0x36c;
    int32_t *f = &b[B_FWD];
    f[0] = CI32(c, row), f[1] = 0, f[2] = CI32(c, row + 8);
    Vec_Unit(f);
    b[B_SIDE] = f[2], b[B_SIDE + 1] = 0, b[B_SIDE + 2] = -f[0];
    int32_t pos[3] = {CI32(c, 0x2d0), CI32(c, 0x2d4), CI32(c, 0x2d8)};
    for (uint32_t i = 0; i < 8; i++) {
        int32_t d[3] = {CI32(c, 0x270 + 12 * i) - pos[0], CI32(c, 0x274 + 12 * i) - pos[1], CI32(c, 0x278 + 12 * i) - pos[2]};
        int32_t ef = dot3(f, d), es = dot3(&b[B_SIDE], d);
        if (ef > 0 && b[B_EXT_F] < ef) b[B_EXT_F] = ef;
        if (es > 0 && b[B_EXT_S] < es) b[B_EXT_S] = es;
        if (b[B_YMAX] < d[1]) b[B_YMAX] = d[1];
        if (d[1] < b[B_YMIN]) b[B_YMIN] = d[1];
    }
    b[B_YMAX] += pos[1];
    b[B_YMIN] += pos[1];
    b[B_VALID] = 1;
    box_corners(b, pos);
}

/* 0x48c870: the cars in contact with this one (the lists at 0x5913dc/0x5913f8, made by the car-to-car
   pass 0x48a1f0) moved with it. The pass is not ported, so the lists are empty. */
static void push_group(const int32_t move[3])
{
    (void)move;
}

/* The move of the car, its eight box corners and its flat box's four corners */
static void move_car(const int32_t m[3])
{
    Car *c = W.car;
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x2d0 + 4 * k, CI32(c, 0x2d0 + 4 * k) + m[k]);
    for (uint32_t i = 0; i < 8; i++)
        for (uint32_t k = 0; k < 3; k++) CW32(c, 0x270 + 12 * i + 4 * k, CI32(c, 0x270 + 12 * i + 4 * k) + m[k]);
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 3; k++) W.box[B_CORNER + 3 * i + k] += m[k];
    push_group(m);
}

/* 0x48f400: no box corner's y strictly between the ends' y (the wall is above or below the car) */
static bool above_or_below(void)
{
    int32_t lo = W.b[1], hi = W.a[1];
    for (uint32_t i = 0; i < 8; i++) {
        int32_t y = CI32(W.car, 0x274 + 12 * i);
        if (W.b[1] < W.a[1] ? (y < hi && lo < y) : (hi < y && y < lo)) return false;
    }
    return true;
}

/* 0x490570 (flag bit 0) and 0x490640: the wall's line within reach of the car's centre; the side of the
   car's last position (+0x2e8). The one-sided test also drops a car that was behind the wall. */
static bool near_wall(bool two_sided)
{
    const Car *c = W.car;
    int32_t o[3] = {W.a[0] - CI32(c, 0x2e8), W.a[1] - CI32(c, 0x2ec), W.a[2] - CI32(c, 0x2f0)};
    int32_t side = dot3(o, W.n);
    if (!two_sided && 0 < side) return false;
    if (W.r < iabs(fx_mul(W.daz, W.n[2]) + fx_mul(W.dax, W.n[0]))) return false;
    W.back = side >= 0;
    return true;
}

/* 0x490720: the flat box's corners against the wall: their distances to its plane, how many are behind
   and in front, and which of them lie between the ends (along the wall) and are counted: those on the
   side the car came from, or those that have not crossed since the last step. (Its first part, the
   normal made from the ends when 0x591adc is set, never runs here: the walk clears it.) */
static void classify(void)
{
    int32_t tx = -W.n[2], tz = W.n[0];
    W.nin = W.nout = W.nneg = W.npos = 0;
    for (int i = 0; i < 4; i++) {
        const int32_t *p = &W.box[B_CORNER + 3 * i];
        int32_t ax = W.a[0] - p[0], az = W.a[2] - p[2];
        int32_t e1 = fx_mul(tz, az) + fx_mul(tx, ax);
        int32_t e2 = fx_mul(tz, W.b[2] - p[2]) + fx_mul(tx, W.b[0] - p[0]);
        int32_t d = fx_mul(W.n[0], ax) + fx_mul(W.n[1], W.a[1] - p[1]) + fx_mul(W.n[2], az);
        W.dist[i] = d;
        if (d < 0) W.nneg++;
        else W.npos++;
        if (e1 != 0 && e2 != 0 && (e1 < 1 || e2 >= 0) && (e1 >= 0 || e2 < 1)) continue;   /* outside the ends */
        bool count = (d < 0) == (W.back == 0);
        if (!count) {
            if (!W.box[B_PREV]) count = true;
            else {
                const int32_t *q = &W.box[B_OLD + 3 * i];
                int32_t was = fx_mul(W.n[0], W.a[0] - q[0]) + fx_mul(W.n[1], W.a[1] - q[1]) + fx_mul(W.n[2], W.a[2] - q[2]);
                count = d < 1 ? (d >= 0 || d <= was) : was <= d;
            }
        }
        if (!count) continue;
        if (W.dist[i] < 0) W.in[W.nin++] = (uint8_t)i;
        else W.out[W.nout++] = (uint8_t)i;
    }
}

/* 0x48f470: an impulse imp on the car at pt (mode 1), or (mode 0) at two units along the normal on the
   car's side of the wall: the velocity (twice with option 4), the contact point in the car's frame
   (+0x5dc, within the half size +0x204), and unless the car is loose (+0xc00) a turn (+0x5d0) from the
   impulse at 2.5 times (at most 3.3) about the point. A hard knock (1.2, or 0.9 lifted by four units)
   makes the car loose. */
static void impulse(const int32_t imp[3], const int32_t pt[3], int mode)
{
    Car *c = W.car;
    int32_t pos[3] = {CI32(c, 0x2d0), CI32(c, 0x2d4), CI32(c, 0x2d8)}, l[3], v[3];
    if (mode == 0) {
        int32_t d[3] = {W.a[0] - pos[0], W.a[1] - pos[1], W.a[2] - pos[2]};
        int32_t k = dot3(W.n, d) < 1 ? -0x20000 : 0x20000;
        for (int i = 0; i < 3; i++) l[i] = fx_mul(W.n[i], k);
    } else
        for (int i = 0; i < 3; i++) l[i] = pt[i] - pos[i];
    rot_t(v, l);
    for (uint32_t i = 0; i < 3; i++) {
        int32_t h = CI32(c, 0x204 + 4 * i);
        if (h < iabs(v[i])) v[i] = v[i] < 0 ? -h : h;
        CW32(c, 0x5dc + 4 * i, v[i]);
    }
    for (int n = Gs_Option(4) ? 2 : 1; n > 0; n--)
        for (uint32_t i = 0; i < 3; i++) CW32(c, 0x408 + 4 * i, CI32(c, 0x408 + 4 * i) + imp[i]);
    if (CI32(c, 0xc00)) return;
    int32_t m = (int32_t)len3(imp);
    int knock = 0;
    if (!CI32(c, 0xb64)) knock = m >= 0x13334 ? 1 : 0xe666 < m ? 2 : 0;
    int32_t ti[3], a[3], b[3], t[3];
    for (int i = 0; i < 3; i++) ti[i] = fx_mul(imp[i], 0x28000);
    m = fx_mul(m, 0x28000);
    if (0x34ccc < m) {
        int32_t r = (int32_t)(0x100000000LL / m);
        for (int i = 0; i < 3; i++) ti[i] = fx_mul(fx_mul(ti[i], r), 0x34ccc);
    }
    rot_t(a, ti);
    l[0] = pt[0] - pos[0], l[1] = pt[1] - (knock ? 0x40000 : 0) - pos[1], l[2] = pt[2] - pos[2];
    rot_t(b, l);
    t[0] = fx_mul(a[1], b[2]) - fx_mul(a[2], b[1]);
    t[1] = fx_mul(a[2], b[0]) - fx_mul(a[0], b[2]);
    t[2] = fx_mul(a[0], b[1]) - fx_mul(a[1], b[0]);
    if (knock == 1) {
        for (int i = 0; i < 3; i++) t[i] = fx_mul(t[i], 0x40000);
        CW32(c, 0x40c, CI32(c, 0x40c) + 0x4000);
    }
    if (knock) {
        CW32(c, 0xc00, 1);
        CW32(c, 0x96c, 0x10000);
        CW32(c, 0xc04, 1);
    }
    int32_t k = gms32(0x519c90);
    for (uint32_t i = 0; i < 3; i++) {
        if (0x80000 < iabs(t[i])) t[i] = t[i] < 1 ? -0x80000 : 0x80000;
        CW32(c, 0x5d0 + 4 * i, CI32(c, 0x5d0 + 4 * i) + fx_mul(t[i], k));
    }
}

static void keep_hit(const int32_t imp[3], const int32_t pt[3], const int32_t n[3], uint8_t kind, int32_t mag)
{
    for (int i = 0; i < 3; i++) W.hit_imp[i] = imp[i], W.hit_pt[i] = pt[i], W.hit_n[i] = n[i];
    W.hit = 1;
    W.hit_kind = kind;
    W.hit_mag = mag;
}

/* 0x48e730: an end p (q the other) inside the car's flat box: the car is moved back along its approach
   (from its last position to p) out of the box's nearer face; then the velocity into the end's cap
   (angle cap, a bounce of the restitution plus 0.3), or with no cap, if the car moves along the wall,
   the velocity along the face it came out of (a bounce of 1 plus the restitution) is taken off. */
static bool end_hit(const int32_t p[3], const int32_t q[3], int which, int type)
{
    Car *c = W.car;
    int32_t *b = W.box;
    int idx = -1;
    box_make(b, c);
    int32_t dx = p[0] - CI32(c, 0x2d0), dz = p[2] - CI32(c, 0x2d8);
    int32_t ef = fx_mul(b[B_FWD + 2], dz) + fx_mul(b[B_FWD], dx);
    int32_t es = fx_mul(b[B_SIDE + 2], dz) + fx_mul(b[B_SIDE], dx);
    if (b[B_EXT_F] < iabs(ef) || b[B_EXT_S] < iabs(es)) return false;
    int32_t ox = p[0] - CI32(c, 0x2e8), oz = p[2] - CI32(c, 0x2f0);
    int32_t l = (int32_t)Fx_Sqrt((uint32_t)fx_mul(ox, ox) + (uint32_t)fx_mul(oz, oz));
    if (l < 1) return false;
    int32_t r = (int32_t)(0x100000000LL / l), ux = fx_mul(ox, r), uz = fx_mul(oz, r);
    int32_t cf = fx_mul(b[B_FWD + 2], uz) + fx_mul(b[B_FWD], ux);
    int32_t cs = fx_mul(b[B_SIDE + 2], uz) + fx_mul(b[B_SIDE], ux);
    int32_t best = 0x7d000000, t0 = 0, t1 = 0;
    bool h0 = false, h1 = false;
    if (cf >= 0x42) t0 = (int32_t)(((int64_t)(b[B_EXT_F] - ef) << 16) / cf), h0 = true;
    else if (cf < -0x41) t0 = (int32_t)(((int64_t)iabs(b[B_EXT_F] + ef) << 16) / -cf), h0 = true;
    if (cs >= 0x42) t1 = (int32_t)(((int64_t)(b[B_EXT_S] - es) << 16) / cs), h1 = true;
    else if (cs < -0x41) t1 = (int32_t)(((int64_t)iabs(b[B_EXT_S] + es) << 16) / -cs), h1 = true;
    if (h0 && t0 < 0x7d000000) idx = 0, best = t0;
    if (h1 && t1 < best) idx = 1, best = t1;
    int32_t m[3] = {fx_mul(-ux, best), 0, fx_mul(-uz, best)};   /* 0x591920 times the distance: 0x591900 */
    move_car(m);
    int32_t imp[3], pt[3] = {p[0], CI32(c, 0x2d4), p[2]};
    uint8_t cap = W.wall->cap[which != 0];
    if (cap != 0xff) {
        uint16_t ang = (uint16_t)llrint((double)fx_mul((int32_t)cap << 16, 0x1cccc) * (1.0 / 5760.0));
        W.dir[0] = g_sin[ang & 0xfff], W.dir[1] = 0, W.dir[2] = g_sin[(ang + 0x400) & 0xfff];
        int32_t vn = fx_mul(W.dir[0], CI32(c, 0x408)) + fx_mul(W.dir[2], CI32(c, 0x410));
        if (vn >= 0) return true;
        int32_t j = -fx_mul(vn, tbl00(type) + 0x4ccc);
        imp[0] = fx_mul(W.dir[0], j), imp[1] = 0, imp[2] = fx_mul(W.dir[2], j);
        impulse(imp, pt, 1);
        int32_t mag = (int32_t)len3(imp);
        if (W.hit && mag <= W.hit_mag) return true;
        keep_hit(imp, pt, W.dir, 2, mag);
        return true;
    }
    if (idx == -1) return true;
    int32_t w[3] = {q[0] - p[0], q[1] - p[1], q[2] - p[2]};
    int32_t ax = iabs(w[0]), ay = iabs(w[1]), az = iabs(w[2]), s;
    if (ay < ax && az < ax) s = ax;
    else if (ax < ay && az < ay) s = ay;
    else s = az;
    s = (int32_t)(0x100000000LL / s);
    for (int i = 0; i < 3; i++) W.dir[i] = fx_mul(w[i], s);
    Vec_Unit(W.dir);
    int32_t v[3] = {CI32(c, 0x408), CI32(c, 0x40c), CI32(c, 0x410)};
    if (dot3(v, W.dir) <= 0) return true;
    for (int i = 0; i < 3; i++) W.dir[i] = b[(idx == 0 ? B_FWD : B_SIDE) + i];
    int32_t j = -fx_mul(dot3(W.dir, v), tbl00(type) + 0x10000);
    for (int i = 0; i < 3; i++) imp[i] = fx_mul(W.dir[i], j);
    impulse(imp, pt, 1);
    int32_t mag = (int32_t)len3(imp);
    if (!W.hit || W.hit_mag < mag) keep_hit(imp, pt, W.dir, 2, mag);
    return true;
}

/* 0x48e580: each end within the car's radius (+0x758) of its centre, through end_hit */
static bool ends(int type)
{
    const Car *c = W.car;
    int32_t r = CI32(c, 0x758), r2 = fx_mul(r, r);
    bool h0 = false, h1 = false;
    int32_t dx = W.a[0] - CI32(c, 0x2d0), dz = W.a[2] - CI32(c, 0x2d8);
    if (iabs(dx) <= r && r >= 0 && iabs(dz) <= r && fx_mul(dz, dz) + fx_mul(dx, dx) < r2) h0 = end_hit(W.a, W.b, 0, type);
    dx = W.b[0] - CI32(c, 0x2d0), dz = W.b[2] - CI32(c, 0x2d8);
    if (iabs(dx) <= r && r >= 0 && iabs(dz) <= r && fx_mul(dz, dz) + fx_mul(dx, dx) < r2) h1 = end_hit(W.b, W.a, 1, type);
    return h0 || h1;
}

/* 0x490b90 (with 1, the two-sided test; 0x490640 with 0): the car's box across the wall */
static bool across(void)
{
    if (!near_wall(true)) return false;
    box_make(W.box, W.car);
    classify();
    if (W.nout != 0 && W.nin != 0) return true;
    if (W.nout == 0 && W.nin == 0) return false;
    /* the original's test: the first clause always holds when the second does */
    return (W.npos != 4 || W.back) && W.nneg == 4 && W.back;
}

/* The deepest corner among list (or all four with list NULL): the most negative distance when the car
   is behind (back), the most positive otherwise. */
static int32_t deepest(const uint8_t *list, int n, bool most_negative)
{
    int32_t best = most_negative ? 0x27100000 : -0x27100000;
    for (int k = 0; k < n; k++) {
        uint32_t i = list ? list[k] : (uint32_t)k;
        if (most_negative ? W.dist[i] < best : best < W.dist[i]) W.deep_wall = W.wall, best = W.dist[i], W.deep = i;
    }
    return best;
}

/* 0x48fb80: a face of the wall (type type). The box is pushed out by its deepest corner's distance;
   the velocity into the wall bounces back by the restitution and the velocity along it is cut by the
   friction, the impulse applied at that corner. Metal-like types (2, 14, 15, 18) give sparks. */
static int face(int type)
{
    Car *c = W.car;
    bool two = W.wall->flags & 1;
    int res = 0;
    if (!near_wall(two)) return 0;
    if (!two) res = ends(type);
    box_make(W.box, c);
    classify();
    int32_t best;
    if (W.nout != 0 && W.nin != 0) {
        if (W.back) best = deepest(W.in, W.nin, true);
        else best = deepest(W.out, W.nout, false);
    } else if (W.nout == 0 && W.nin == 0)
        return res;
    else if (W.npos == 4 && !W.back)
        best = deepest(NULL, 4, false);
    else if (W.nneg == 4 && W.back)
        best = deepest(NULL, 4, true);
    else
        return res;
    if (best != 0 && (W.back ? best < 0 : best > 0)) {
        int32_t m[3] = {fx_mul(W.n[0], best), fx_mul(W.n[1], best), fx_mul(W.n[2], best)};
        move_car(m);
    }
    int32_t v[3] = {CI32(c, 0x408), CI32(c, 0x40c), CI32(c, 0x410)};
    int32_t vn = dot3(W.n, v);
    if (vn != 0 && (W.back ? vn > 0 : vn < 0)) {
        int32_t nv[3], imp[3];
        int32_t k = -fx_mul(gms32(0x519c8c), tbl68(type)), e = -tbl00(type);
        for (int i = 0; i < 3; i++) {
            nv[i] = fx_mul(W.n[i], vn);
            imp[i] = (fx_mul(nv[i], e) - nv[i]) + fx_mul(v[i] - nv[i], k);
        }
        impulse(imp, &W.box[B_CORNER + 3 * W.deep], 0);
        int32_t mag = (int32_t)len3(imp);
        if (!W.hit || W.hit_mag < mag) keep_hit(imp, W.a, W.n, 0, mag);
    }
    if (type == 2 || type == 14 || type == 15 || type == 18) PENDING_NOTE(0x499750, "Sparks_Add");
    return 1;
}

/* 0x48df50 (type 0x17 when +0xc20 is clear): a bump under each wheel from its box corner's position
   and the speed, kept when stronger than the one already set (+0x8fc, the flag +0xbbc, the normal
   +0x564 straight up); the velocity times 0.97. */
static void bumps(Car *c)
{
    for (uint32_t i = 0; i < 4; i++) {
        int32_t s = CI32(c, 0x778);
        if (0x10000 < s) s = 0x10000;
        int32_t x = iabs(CI32(c, 0x270 + 12 * i)), z = iabs(CI32(c, 0x278 + 12 * i));
        int32_t d = x - z < 0 ? z - x : x - z;
        int32_t h = fx_mul((d % 0x401) << 6, fx_mul(s, 0x6666));
        if (!CI32(c, 0xbbc + 4 * i) || CI32(c, 0x8fc + 4 * i) <= h) {
            CW32(c, 0x8fc + 4 * i, h);
            CW32(c, 0xbbc + 4 * i, 1);
            CW32(c, 0x564 + 12 * i, 0);
            CW32(c, 0x568 + 12 * i, 0x10000);
            CW32(c, 0x56c + 12 * i, 0);
        }
    }
    for (uint32_t k = 0; k < 3; k++) CW32(c, 0x408 + 4 * k, fx_mul(CI32(c, 0x408 + 4 * k), 0xf851));
}

int32_t *Walls_CarBox(Car *c)
{
    int32_t *b = box_rec[CI8(c, 0xb1a)];
    box_make(b, c);
    return b;
}

void Walls_Bumps(Car *c) { bumps(c); }

void Walls_RotT(const Car *c, int32_t out[3], const int32_t v[3])
{
    for (uint32_t r = 0; r < 3; r++)
        out[r] = fx_mul(CI32(c, 16 * r + 8), v[2]) + fx_mul(CI32(c, 16 * r + 4), v[1]) + fx_mul(CI32(c, 16 * r), v[0]);
}

/* 0x48e0a0 */
void Car_Walls(Car *c, int n)
{
    (void)n;   /* passed on to 0x48fb80, which does not read it */
    W.car = c;
    W.box = box_rec[CI8(c, 0xb1a)];
    int8_t k = (int8_t)(CI8(c, 0xb42) - 1);
    CW8(c, 0xb42, (uint8_t)(k < 0 ? 0 : k));
    W.deep_wall = NULL;
    int16_t cell = (int16_t)(c->s[0xb00] | c->s[0xb01] << 8);
    if (cell == -1) return;
    W.r = fx_mul(CI32(c, 0x758), 0x13333);
    for (const Wall *w = cell >= 0 && cell < g_nsec ? g_sec[cell].walls : NULL; w; w = w->next) {
        W.wall = w;
        W.hit = 0;
        for (int i = 0; i < 3; i++) W.a[i] = w->a[i], W.b[i] = w->b[i];
        W.dax = W.a[0] - CI32(c, 0x2d0), W.daz = W.a[2] - CI32(c, 0x2d8);
        W.dbx = W.b[0] - CI32(c, 0x2d0), W.dbz = W.b[2] - CI32(c, 0x2d8);
        /* both ends on one side of the car and beyond reach on x, or on z: too far */
        bool same_x = (W.dax < 0) == (W.dbx < 0), same_z = (W.daz < 0) == (W.dbz < 0);
        if (same_x && W.r <= iabs(W.dax) && W.r <= iabs(W.dbx)) continue;
        if (same_z && W.r <= iabs(W.daz) && W.r <= iabs(W.dbz)) continue;
        for (int i = 0; i < 3; i++) W.n[i] = w->n[i];
        int type = (int8_t)w->type;
        switch (type) {
        case 0: case 1:
            continue;
        default:
            if (!above_or_below()) {
                if ((w->flags & 1) && ends(type) && type == 0x19) CW32(c, 0xbf8, 1);
                if (face(type) && type == 0x19) CW32(c, 0xbf8, 1);
            }
            break;
        case 6:
            if (!above_or_below() && across()) CW32(c, 0xbf8, 1);
            break;
        case 7: case 8: case 9: case 10: case 11:
            break;   /* 0x4a3240(type - 7), an empty function */
        case 0x17:
            if (CI32(c, 0xc20)) continue;
            if (!above_or_below() && across()) bumps(c);
            break;
        case 0x18:
            if (!above_or_below() && across() && !CI32(c, 0xa7c) && !CI32(c, 0xbf8)) CW32(c, 0xa7c, 0x190000);
            break;
        }
        if (W.hit) {
            /* the damage sees the strongest impulse over (1 - restitution), not within 10 steps of the
               last; 0x5c4 is put back after */
            int32_t keep[3], k = (int32_t)(0x100000000LL / (0x10000 - tbl00(type)));
            for (uint32_t i = 0; i < 3; i++) keep[i] = CI32(c, 0x5c4 + 4 * i), CW32(c, 0x5c4 + 4 * i, fx_mul(W.hit_imp[i], k));
            if (CI8(c, 0xb42) < 1) Car_Damage(c, W.hit_pt, W.hit_n, 0, W.hit_kind, false);
            for (uint32_t i = 0; i < 3; i++) CW32(c, 0x5c4 + 4 * i, keep[i]);
            CW8(c, 0xb42, 10);
        }
    }
    if (0 < CI32(c, 0xa7c)) {
        int32_t t = CI32(c, 0xa7c) - 0x10000;
        if (t < 0) t = 0;
        CW32(c, 0xa7c, t);
        if (t == 0) CW32(c, 0xbf8, 1);
    }
}

/* 0x4877a0 */
void Sim_Collide(const int16_t *list, int n)
{
    for (int i = n - 1; i >= 0; i--) box_rec[list[i]][B_VALID] = 0;
    PENDING_NOTE(0x48a1f0, "Cars_Collide (car to car)");
    for (int i = n - 1; i >= 0; i--) {
        int k = list[i];
        Car *c = Car_Get(k);
        if (!CI32(c, 0xb64)) {
            Car_Scenery(c);   /* 0x4878a0 */
        }
        if (!CI32(c, 0xc18)) {
            if (CI32(c, 0xb64)) PENDING_NOTE(0x47c5b0, "0x47c5b0");   /* with +0xb64, above 2.0 the walls are skipped */
            Car_Walls(c, k);
        }
        int32_t *b = box_rec[k];
        b[B_PREV] = b[B_VALID];
        if (b[B_VALID])
            for (int j = 0; j < 12; j++) b[B_OLD + j] = b[B_CORNER + j];
    }
}
