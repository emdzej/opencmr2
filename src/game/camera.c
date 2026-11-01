/* The race cameras (see camera.h). */
#include "game/camera.h"
#include "sim/car.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "exe.h"
#include "game/car_load.h"
#include "pending.h"
#include <string.h>
#include <math.h>

/* A camera record, 0x64 bytes */
typedef struct {
    uint8_t slot, player, car, pad;   /* +0x00: the record's index (for the chase tables), +0x01, +0x02 */
    int32_t kind;                     /* +0x04: 1, 2, 10 interior; 3; 4-6 chase; 7 trackside; 8 blending */
    int32_t m[16];                    /* +0x08: the camera's matrix (rows: side, up, forward; position) */
    int32_t f48, f4c, f50, f54, f58, f5c, f60;   /* +0x48-+0x60: the projection's terms, the ground lift */
} Cam;

static Cam slots[8];          /* 0x539018: two per player, the current one by cur[] */
static Cam out[4], prev[4];   /* 0x538d28, 0x538e38: the step's camera and the last step's */
static uint8_t cur[4];        /* 0x538e0c */
static int32_t sw[4];         /* 0x538d20: a switch's blend left (12.5 steps), negative none */
static int32_t ref[4][16];    /* 0x538ca0: the followed car's frame */
static uint8_t snap[4];       /* 0x5391b0 */
/* the chase cameras (0x447530-0x447ec0): per record its kind (0, 3, 4) and smoothed axes; per player
   the settings from the profile */
static uint8_t chase_kind[8];            /* 0x53cff8 */
static int32_t chase_fwd[8][3];          /* 0x53d018: the forward axis */
static int32_t chase_up[8][3];           /* 0x53d060 */
static int32_t set_off[4][3], set_raw[4][3];   /* 0x53d000, 0x53d048 */
static int16_t set_ang_raw[4], set_ang[4];     /* 0x53d090, 0x53d094 */
static int32_t set_dist[4];                     /* 0x53d098 */

static void row(int32_t o[3], const int32_t *m, int r) { o[0] = m[4 * r], o[1] = m[4 * r + 1], o[2] = m[4 * r + 2]; }
static void set_row(int32_t *m, int r, const int32_t v[3]) { m[4 * r] = v[0], m[4 * r + 1] = v[1], m[4 * r + 2] = v[2]; }
static void cross(int32_t o[3], const int32_t a[3], const int32_t b[3])
{
    int32_t x = fx_mul(a[1], b[2]) - fx_mul(a[2], b[1]), y = fx_mul(a[2], b[0]) - fx_mul(a[0], b[2]),
            z = fx_mul(a[0], b[1]) - fx_mul(a[1], b[0]);
    o[0] = x, o[1] = y, o[2] = z;
}
static int32_t lerp(int32_t a, int32_t b, int32_t k) { return a + fx_mul(b - a, k); }

/* 0x4224e0: a blend of two frames: forward by k0 (left as it falls), then with mode 0 up by k1 made
   unit and side from them, with mode 1 side by k1 and up from them; forward made square to both;
   the position by kp */
static void blend(int32_t *o, const int32_t *a, const int32_t *b, int32_t k0, int32_t k1, int32_t kp, int mode)
{
    int32_t f[3], u[3], s[3], x[3], y[3], p[3];
    row(x, a, 0), row(y, b, 0);
    for (int i = 0; i < 3; i++) f[i] = lerp(x[i], y[i], k0);
    if (mode == 0) {
        row(x, a, 1), row(y, b, 1);
        for (int i = 0; i < 3; i++) u[i] = lerp(x[i], y[i], k1);
        Vec_Unit(u);
        cross(s, f, u);
        Vec_Unit(s);
    } else {
        row(x, a, 2), row(y, b, 2);
        for (int i = 0; i < 3; i++) s[i] = lerp(x[i], y[i], k1);
        Vec_Unit(s);
        cross(u, s, f);
        Vec_Unit(u);
    }
    cross(f, u, s);
    row(x, a, 3), row(y, b, 3);
    for (int i = 0; i < 3; i++) p[i] = lerp(x[i], y[i], kp);
    memset(o, 0, 16 * sizeof *o);
    o[15] = 0x10000;
    set_row(o, 0, f), set_row(o, 1, u), set_row(o, 2, s), set_row(o, 3, p);
}

/* 0x423070: a frame made level: up the world's, the first row across it and the third, the third from
   the two */
static void level(int32_t *m)
{
    static const int32_t up[3] = {0, 0x10000, 0};
    int32_t z[3], x[3];
    row(z, m, 2);
    cross(x, up, z);
    Vec_Unit(x);
    cross(z, x, up);
    set_row(m, 0, x), set_row(m, 1, up), set_row(m, 2, z);
}

/* 0x422e70: the frame pitched by ang about its first row */
static void pitch(int32_t *m, uint32_t ang)
{
    FxMat r, t, o;
    Mat_Identity(&r);
    int32_t c = g_sin[(ang + 0x400) & 0xfff], s = g_sin[ang & 0xfff];
    r.m[5] = c, r.m[6] = s, r.m[9] = -s, r.m[10] = c;
    memcpy(t.m, m, sizeof t.m);
    int32_t p[3] = {t.m[12], t.m[13], t.m[14]};
    t.m[12] = t.m[13] = t.m[14] = 0;
    Mat_Mul(&o, &r, &t);
    memcpy(m, o.m, sizeof o.m);
    m[12] = p[0], m[13] = p[1], m[14] = p[2];
}

/* 0x423d70: the frame a camera follows: a player's (0x538ca0), another car's orientation */
static const int32_t *follow(uint8_t car)
{
    static int32_t tmp[16];
    if (car < Gs_SelBits14()) return ref[car];
    const Car *c = Car_Get(car);
    for (uint32_t i = 0; i < 16; i++) tmp[i] = i < 12 ? CI32(c, 4 * i) : 0;
    tmp[12] = CI32(c, 0x30), tmp[13] = CI32(c, 0x34), tmp[14] = CI32(c, 0x38), tmp[15] = 0x10000;
    return tmp;
}

/* 0x447ee0: the camera's offset from the car: the player's settings (kind 4), else the kinds' table
   at 0x519ea0 (12 bytes each) */
static void chase_offset(int32_t o[3], const Cam *k)
{
    if (chase_kind[k->slot] == 4) memcpy(o, set_off[k->player], 12);
    else
        for (uint32_t i = 0; i < 3; i++) o[i] = (int32_t)exe_u32(0x519ea0 + 12 * chase_kind[k->slot] + 4 * i);
    PENDING_NOTE(0x411880, "Split_Screen (the chase camera's offset lowered)");
}

/* 0x447a40: the camera put at its offset in the smoothed axes from the followed frame, pitched by the
   player's angle (kind 4) */
static void chase_place(Cam *k, const int32_t *r)
{
    int32_t off[3], f[3], u[3], s[3];
    chase_offset(off, k);
    FxMat m, a, o;
    Mat_Identity(&m);
    m.m[12] = off[0], m.m[13] = off[1], m.m[14] = off[2];
    memcpy(f, chase_fwd[k->slot], 12);
    memcpy(u, chase_up[k->slot], 12);
    cross(s, u, f);
    set_row(k->m, 0, s), set_row(k->m, 1, u), set_row(k->m, 2, f);
    k->m[12] = k->m[13] = k->m[14] = 0;
    memcpy(a.m, k->m, sizeof a.m);
    Mat_Mul(&o, &m, &a);
    memcpy(k->m, o.m, sizeof o.m);
    k->m[12] += r[12], k->m[13] += r[13], k->m[14] += r[14];
    if (chase_kind[k->slot] == 4) pitch(k->m, (uint16_t)set_ang[k->player]);
    k->f48 = 0, k->f4c = 0x10000, k->f58 = 0x10000, k->f54 = 0xa000;
    k->f5c = off[2] < 0 ? -off[2] : off[2];
    k->f50 = k->f5c - 0x10000;
}

/* 0x447e20 and 0x447d20: the player's settings, the height and angle eased for longer distances */
static int32_t far_factor(int p)
{
    int32_t z = set_raw[p][2];
    int64_t d = (((int64_t)(z - 0x50000)) << 16) / 0x50000;
    return 0x10000 - fx_mul((int32_t)d, 0xcccc);
}

static void chase_set_angle(int p, int16_t a)
{
    set_ang_raw[p] = a;
    set_ang[p] = (int16_t)(((int64_t)far_factor(p) * a) >> 16);
}

static void chase_settings(int p, const int32_t off[3])
{
    memcpy(set_raw[p], off, 12);
    memcpy(set_off[p], off, 12);
    set_off[p][1] = 0x20000 - fx_mul(far_factor(p), 0x20000 - set_off[p][1]);
    set_off[p][2] = -set_off[p][2];
    chase_set_angle(p, set_ang_raw[p]);
}

/* 0x447530: a chase camera's kind (0, 4 the player's, 3 the 45 degree view) and settings from the
   profile (0x408c20: its +0x5e0 position, angle and distance) */
static void chase_init(Cam *k, const int32_t *r, uint8_t kind)
{
    chase_kind[k->slot] = kind;
    uint32_t prof = gm32(0x531350 + 0x30u * (k->car < Gs_SelBits14() ? (uint32_t)(Gs_Byte537ef6() + k->car) : 0)) >> 0x12 & 0xf;
    uint32_t base = prof == 0xf ? 0 : 0x53003c + prof * 0x650;
    int32_t off[3] = {0, 0, 0};
    int16_t ang = 0;
    int32_t dist = 0;
    if (base) off[0] = gms32(base), off[1] = gms32(base + 4), off[2] = gms32(base + 8), ang = (int16_t)gm16(base + 0xc), dist = gms32(base + 0x10);
    chase_settings(k->player, off);
    set_dist[k->player] = dist;
    chase_set_angle(k->player, ang);
    chase_place(k, r);
}

/* 0x4475f0: the axes eased towards the followed frame (forward the car's, up its up; with flag set,
   the camera's own made level), at dt over the distance (kind 4: the player's, else 14) times the
   car's +0xa74; unchanged when within 0.02 */
static void chase_step(Cam *k, const int32_t *r, int flag)
{
    int32_t f[3], u[3], s[3], a[16], t[16];
    memcpy(f, chase_fwd[k->slot], 12);
    memcpy(u, chase_up[k->slot], 12);
    cross(s, u, f);
    memset(a, 0, sizeof a);
    a[15] = 0x10000;
    set_row(a, 0, s), set_row(a, 1, u), set_row(a, 2, f);
    if (!flag) {
        memcpy(t, r, sizeof t);
        t[0] = -r[8], t[1] = -r[9], t[2] = -r[10];
        t[8] = r[0], t[9] = r[1], t[10] = r[2];
    } else {
        memcpy(t, a, sizeof t);
        level(t);
    }
    if (chase_kind[k->slot] == 3) {
        /* pitched 45 degrees about the first row (the table's sin 0x200 and sin 0x600) */
        int32_t c = g_sin[0x200], sn = g_sin[0x600], y[3], z[3];
        row(y, t, 1), row(z, t, 2);
        for (int i = 0; i < 3; i++) {
            t[8 + i] = fx_mul(z[i], c) - fx_mul(y[i], sn);
            t[4 + i] = fx_mul(y[i], c) + fx_mul(z[i], sn);
        }
    }
    bool near = true;
    for (int i = 0; i < 11 && near; i++)
        if (i % 4 != 3) {
            int32_t d = a[i] - t[i];
            if ((d < 0 ? -d : d) >= 0x51e) near = false;
        }
    if (!near) {
        int32_t d = chase_kind[k->slot] == 4 ? set_dist[k->player] : 0xe0000;
        int32_t kk = d ? (int32_t)(((int64_t)gms32(0x519c8c) << 16) / d) : 0;
        kk = fx_mul(kk, CI32(Car_Get(k->car), 0xa74));
        int32_t o[16];
        blend(o, a, t, kk, kk, 0x10000, 0);
        row(chase_up[k->slot], o, 1);
        row(chase_fwd[k->slot], o, 2);
    }
    chase_place(k, r);
}

/* The interior views (0x486740-0x486c00): per record the mode (0 the bonnet, 1 the roof, 2 behind;
   0x590d8c) and the car (0x590ec0); the offset from section 4 of the car's .cin, 12 bytes a mode */
static uint8_t in_mode[8], in_car[8];
static int32_t in_fov[8];   /* 0x590db0 */

/* the followed frame looking along the car (mode 2 backwards), pitched down 10 degrees for the bonnet */
static void interior_target(int32_t *t, const int32_t *r, int mode)
{
    memcpy(t, r, 16 * sizeof *t);
    if (mode == 2) t[0] = r[8], t[1] = r[9], t[2] = r[10], t[8] = -r[0], t[9] = -r[1], t[10] = -r[2];
    else t[0] = -r[8], t[1] = -r[9], t[2] = -r[10], t[8] = r[0], t[9] = r[1], t[10] = r[2];
    int32_t deg = mode == 0 ? 10 : 0;
    pitch(t, (uint16_t)llrint((double)llrint(deg * 65536.0) * (1.0 / 5760.0)));
}

/* 0x4869e0: the camera at the .cin's offset for its mode in its own frame, from the followed frame */
static void interior_place(Cam *k, const int32_t *r)
{
    PENDING_NOTE(0x4869e0, "Interior_Place (the split screen's modes)");
    int32_t off[3] = {0, 0, 0};
    const uint8_t *sec = Car_CinSection(in_car[k->slot], 4);
    if (sec)
        for (int q = 0; q < 3; q++) {
            const uint8_t *b = sec + 12 * in_mode[k->slot] + 4 * q;
            off[q] = (int32_t)((uint32_t)b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24);
        }
    FxMat m, a, o;
    Mat_Identity(&m);
    m.m[12] = off[0], m.m[13] = off[1], m.m[14] = off[2];
    k->m[12] = k->m[13] = k->m[14] = 0;
    memcpy(a.m, k->m, sizeof a.m);
    Mat_Mul(&o, &m, &a);
    memcpy(k->m, o.m, sizeof o.m);
    k->m[12] += r[12], k->m[13] += r[13], k->m[14] += r[14];
    k->f48 = in_fov[k->slot];
    k->f4c = 0x1999, k->f54 = 0xa000, k->f58 = 0, k->f5c = 0x10000, k->f50 = 0;
}

/* 0x486740 / 0x486910 */
static void interior_init(Cam *k, const int32_t *r, int mode)
{
    in_mode[k->slot] = (uint8_t)mode;
    in_car[k->slot] = k->car;
    in_fov[k->slot] = 0x10000;   /* 0x486b20: set for the interior kinds */
    PENDING_NOTE(0x486740, "Interior_Init (the cockpit objects, types 9 and 5)");
    int32_t t[16];
    interior_target(t, r, mode);
    memcpy(k->m, t, sizeof t);
    interior_place(k, r);
}

/* 0x486810: forward taken at once, up eased (by 0.3 dt, at once with flag) */
static void interior_step(Cam *k, const int32_t *r, int flag)
{
    int32_t t[16], o[16];
    interior_target(t, r, in_mode[k->slot]);
    int32_t ku = flag ? 0x10000 : fx_mul(gms32(0x519c8c), 0x4ccc);
    blend(o, k->m, t, 0x10000, ku, 0x10000, 0);
    memcpy(k->m, o, sizeof o);
    interior_place(k, r);
}

/* 0x423300: a camera of a kind made for its car */
static void cam_init(Cam *k, int kind, int sub)
{
    (void)sub;
    k->kind = kind;
    const int32_t *r = follow(k->car);
    switch (kind) {
    case 1: interior_init(k, r, 1); break;
    case 2: interior_init(k, r, 0); break;
    case 10: interior_init(k, r, 2); break;
    case 4: chase_init(k, r, 0); break;
    case 5: chase_init(k, r, 4); break;
    case 6: chase_init(k, r, 3); break;
    default: PENDING_NOTE(0x423300, "Camera_Init (kinds 3, 7)"); chase_init(k, r, 0); break;
    }
}

/* 0x423460: a camera's step; the ground lift (+0x60) from the height under it */
static void cam_step(Cam *k)
{
    const Car *c = Car_Get(k->car);
    int flag = CI32(c, 0xc04) != 0;
    const int32_t *r = follow(k->car);
    switch (k->kind) {
    case 1: case 2: case 10: interior_step(k, r, flag || CI32(c, 0xb60)); break;
    case 4: case 5: case 6: chase_step(k, r, flag); break;
    default: PENDING_NOTE(0x423460, "Camera_Step (kinds 3, 7)"); break;
    }
    PENDING_NOTE(0x4930b0, "Camera_GroundLift");
    k->f60 = 0;
}

/* 0x423b20: the followed frame: the car's orientation (+0x750); the look-around (0x538f00) open */
static void cam_ref(int p)
{
    const Car *c = Car_Get(p);
    for (uint32_t i = 0; i < 12; i++) ref[p][i] = CI32(c, 4 * i);
    ref[p][12] = CI32(c, 0x30), ref[p][13] = CI32(c, 0x34), ref[p][14] = CI32(c, 0x38), ref[p][15] = 0x10000;
    if (Gs_Option(6)) PENDING_NOTE(0x423b20, "Camera_Ref (option 6)");
}

static void cam_copy(Cam *d, const Cam *s)   /* 0x423ee0: all but the indices */
{
    d->kind = s->kind;
    memcpy(d->m, s->m, sizeof d->m);
    d->f48 = s->f48, d->f4c = s->f4c, d->f54 = s->f54, d->f58 = s->f58, d->f5c = s->f5c, d->f50 = s->f50, d->f60 = s->f60;
}

void Camera_Reset(int p)
{
    cur[p] = 0;
    sw[p] = -1;
    for (int i = 0; i < 2; i++) {
        Cam *k = &slots[2 * p + i];
        memset(k, 0, sizeof *k);
        k->slot = (uint8_t)(2 * p + i), k->player = (uint8_t)p, k->car = (uint8_t)p;
    }
    memset(&out[p], 0, sizeof out[p]);
    memset(&prev[p], 0, sizeof prev[p]);
}

int Camera_Kind(int p) { return out[p].kind != 8 ? slots[cur[p] + 2 * p].kind : 8; }   /* 0x422f50 */

void Camera_Select(int p, int kind, bool smooth)
{
    cam_ref(p);
    if (Camera_Kind(p) == 8) return;
    Cam *c = &slots[cur[p] + 2 * p], *o = &slots[2 * p + 1 - cur[p]];
    bool b = smooth && kind != 3 && c->kind != 3;
    o->kind = kind;
    o->car = c->car;
    /* 0x423810, 0x423860: the new camera from the old (chase on the same car: its axes) */
    if ((kind == 4 || kind == 5) && c->car == o->car && (c->kind == 4 || c->kind == 5 || c->kind == 6)) {
        memcpy(chase_up[o->slot], chase_up[c->slot], 12);
        memcpy(chase_fwd[o->slot], chase_fwd[c->slot], 12);
    } else if (kind == 4 || kind == 5) {
        const int32_t *r = follow(o->car);
        row(chase_up[o->slot], r, 1);
        row(chase_fwd[o->slot], r, 0);
    }
    cam_init(o, kind, 0xffff);
    o->f60 = 0;
    if (b) {
        sw[p] = 0xc8000;
        return;
    }
    snap[p] = 1;
    cur[p] = (uint8_t)(1 - cur[p]);
}

/* 0x423970: a kind the cycle may take: 1, 3, 4, 5 when they can be had, 2 but in the replay */
static bool cyclable(int p, int kind, int state)
{
    switch (kind) {
    case 2: if (state == 10) return false; /* fall through */
    case 1: case 4: case 5: return true;
    case 3: PENDING_NOTE(0x476520, "Camera_Allowed (kind 3)"); return false;
    default: return false;
    }
    (void)p;
}

int Camera_Next(int p, int state)
{
    int kind = slots[cur[p] + 2 * p].kind;
    do {
        if (++kind == 0xb) kind = 0;
    } while (!cyclable(p, kind, state));
    Camera_Select(p, kind, true);
    return kind;
}

void Camera_Step(int p)
{
    PENDING_NOTE(0x538f00, "Camera_LookAround");
    cam_ref(p);
    Cam *c = &slots[cur[p] + 2 * p];
    cam_copy(&prev[p], sw[p] < 0 ? c : &out[p]);
    cam_step(c);
    snap[p] = 0;
    if (sw[p] < 0) {
        cam_copy(&out[p], c);
        return;
    }
    Cam *o = &slots[2 * p + 1 - cur[p]];
    cam_step(o);
    int32_t dt = gms32(0x519c8c);
    if (sw[p] != dt && sw[p] - dt >= 0) {
        /* the switch's blend: f = what is left over 12.5 steps, a = -f half turns, the share of the new
           camera 1 - (1 - (cos a + 1) / 2)^2; between a chase-like kind (4, 5, 10) and an interior (1,
           2) +0x5c bumps by the sine of that share's half turn */
        int32_t f = (int32_t)(((int64_t)sw[p] << 16) / 0xc8000);
        uint16_t a = (uint16_t)llrint((double)(f * 0xb4) * (-1.0 / 5760.0));
        int32_t g = 0x10000 - (g_sin[(0x400 - a) & 0xfff] + 0x10000) / 2;
        int32_t k = 0x10000 - fx_mul(g, g);
        Cam m;
        m = out[p];
        int32_t mm[16];
        blend(mm, c->m, o->m, k, k, k, 1);   /* 0x423de0 */
        memcpy(m.m, mm, sizeof mm);
        m.f48 = lerp(c->f48, o->f48, k), m.f4c = lerp(c->f4c, o->f4c, k), m.f54 = lerp(c->f54, o->f54, k);
        m.f58 = lerp(c->f58, o->f58, k), m.f5c = lerp(c->f5c, o->f5c, k), m.f50 = lerp(c->f50, o->f50, k);
        m.f60 = lerp(c->f60, o->f60, k);
        bool chase_was = c->kind == 4 || c->kind == 5 || c->kind == 10, in_was = c->kind == 1 || c->kind == 2;
        bool chase_now = o->kind == 4 || o->kind == 5 || o->kind == 10, in_now = o->kind == 1 || o->kind == 2;
        if ((chase_was && in_now) || (in_was && chase_now)) {
            uint16_t b = (uint16_t)llrint((double)(k * 0xb4) * (1.0 / 5760.0));
            m.f5c += g_sin[b & 0xfff];
        }
        m.kind = 8;
        out[p] = m;
        sw[p] -= dt;
        return;
    }
    cam_copy(&out[p], o);
    cur[p] = (uint8_t)(1 - cur[p]);
    sw[p] -= dt;
}

void Camera_Frame(int p, int32_t frac, Obj *camera)
{
    int32_t m[16];
    blend(m, prev[p].m, out[p].m, frac, frac, frac, 1);   /* 0x423de0 */
    PENDING_NOTE(0x422140, "Camera_Frame (the ground lift's tilt)");
    for (int i = 0; i < 12; i++) camera->local.m[i] = m[i];
    camera->local.m[12] = m[12], camera->local.m[13] = m[13], camera->local.m[14] = m[14];
    for (Obj *o = camera; o; o = o->parent) o->dirty = 1;
}
