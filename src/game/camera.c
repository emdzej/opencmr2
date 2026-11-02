/* The race cameras (see camera.h). */
#include "game/camera.h"
#include "sim/car.h"
#include "sim/walls.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "exe.h"
#include "game/car_load.h"
#include "pending.h"
#include "crt.h"
#include "game/cockpit.h"
#include "render/d3d7.h"
#include "game/race.h"
#include "game/hud.h"
#include "render/view.h"
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
/* the pause's camera page's look-around (0x423010): its state (0x538f00: 0 off, 1 opening, 2 open, 3
   closing), its share (0x538c98, a tenth of 0.8 a step) and its swing's phase (0x538df8) */
static int look[4];
static int32_t look_k[4];
static uint16_t look_ph[4];

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

void Frame_Pitch(int32_t *m, uint32_t ang) { pitch(m, ang); }   /* 0x422e70 */
void Frame_Blend(int32_t *o, const int32_t *a, const int32_t *b, int32_t k0, int32_t k1, int32_t kp, int mode) { blend(o, a, b, k0, k1, kp, mode); }   /* 0x4224e0 */

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

void Chase_SetOffset(int p, const int32_t off[3]) { chase_settings(p, off); }   /* 0x447d20 */
void Chase_SetAngle(int p, int16_t a) { chase_set_angle(p, a); }               /* 0x447e20 */
void Chase_Raw(int p, int32_t off[3]) { memcpy(off, set_raw[p], 12); }        /* 0x447cf0 */
int16_t Chase_RawAngle(int p) { return set_ang_raw[p]; }                       /* 0x447e00 */
int32_t Chase_Dist(int p) { return set_dist[p]; }                              /* 0x447ea0 */
void Chase_SetDist(int p, int32_t d) { set_dist[p] = d; }                      /* 0x447ec0 */

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

/* The trackside cameras (kind 7, 0x48caa0-0x48df10): the stage's .tsc, a count and per camera 0x6c
   bytes (docs/formats/stage.md): +0x02 its roll (about a quarter turn either way: a camera on the car,
   the interior's roof or behind view), +0x04 three rows (the forward one +0x1c), +0x28 its place,
   +0x34 the far end of its dolly, +0x40 the radius it follows the car within, +0x44 the field of view
   at the radius and +0x54 at the camera, +0x48 the lead by the speed, +0x4c/+0x50 the aim's rates
   (closing, opening), +0x58 the distance below which it shakes, +0x60 the dolly's rate, +0x64/+0x68 the
   field of view's rates. Per camera record: the camera's index (0x591740), following (0x5916d0),
   aimed (0x5916f0), the target (0x5916a0) and the aim eased towards it (0x591898), the lead
   (0x591868), the distance last (0x5916e0), the fields of view wanted and had (0x591710, 0x591690),
   the rates eased (0x591754, 0x591720, 0x591700) and the dolly's share (0x591730). */
static const uint8_t *tsc;            /* DAT_00591750 */
static uint32_t ntsc;                 /* DAT_005918c8 */
static int32_t ts_idx[8], ts_follow[8], ts_aimed[8], ts_dist[8], ts_fov_t[8], ts_fov[8];
static int32_t ts_rate[8], ts_rate2[8], ts_speed[8], ts_slide[8];
static int32_t ts_target[8][3], ts_aim[8][3], ts_lead[8][3];

static int32_t T32(int i, uint32_t o)
{
    const uint8_t *b = tsc + 0x6c * (uint32_t)i + o;
    return (int32_t)((uint32_t)b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24);
}
static void T3(int i, uint32_t o, int32_t v[3]) { for (int k = 0; k < 3; k++) v[k] = T32(i, o + 4u * (uint32_t)k); }
static int16_t troll(int i) { return (int16_t)(tsc[0x6c * (uint32_t)i + 2] | tsc[0x6c * (uint32_t)i + 3] << 8); }
/* the cameras on the car: a roll within 0x3f5-0x40a (the roof) or the same below 0 (behind) */
static int on_car(int i)
{
    int16_t a = troll(i);
    return 0x3f4 < a && a < 0x40b ? 1 : a < -0x3f4 && -0x40b < a ? 2 : 0;
}
static int32_t dot3(const int32_t a[3], const int32_t b[3]) { return fx_mul(a[0], b[0]) + fx_mul(a[1], b[1]) + fx_mul(a[2], b[2]); }

/* 0x48caa0 (Stage_Load): the stage's .tsc; false without cameras */
bool Camera_LoadTsc(const uint8_t *d, uint32_t size)
{
    if (!d || size < 4) {
        ntsc = 0, tsc = NULL;
        return false;
    }
    tsc = d + 4;
    ntsc = (uint32_t)d[0] | d[1] << 8 | d[2] << 16 | (uint32_t)d[3] << 24;
    return ntsc != 0;
}

/* 0x48d8b0: the camera whose dolly's end is nearest a point (the first of equals; 0 without) */
static int ts_nearest(const int32_t p[3])
{
    int best = 0;
    int32_t d = 0x270f0000;
    for (uint32_t i = 0; i < ntsc; i++) {
        int32_t e[3];
        T3((int)i, 0x34, e);
        for (int k = 0; k < 3; k++) e[k] -= p[k];
        int32_t l = Vec_Length(e);
        if (l < d) d = l, best = (int)i;
    }
    return best;
}

/* 0x48dce0: the lead ahead of the car: its first axis less its +0x408 vector made unit (turned to
   the axis' side; none under 0.01 of speed), by the speed and the camera's +0x48 */
static void ts_lead_of(int32_t o[3], const Cam *k, const Car *c, const int32_t *r)
{
    int32_t spd = CI32(c, 0x778), v[3] = {CI32(c, 0x408), CI32(c, 0x40c), CI32(c, 0x410)}, f[3];
    row(f, r, 0);
    if (spd < 0x28f) v[0] = v[1] = v[2] = 0;
    else {
        Vec_Unit(v);
        if (dot3(v, f) < 0) v[0] = -v[0], v[1] = -v[1], v[2] = -v[2];
    }
    int32_t m = fx_mul(CI32(c, 0x778), T32(ts_idx[k->slot], 0x48));
    for (int q = 0; q < 3; q++) o[q] = fx_mul(f[q] - v[q], m);
}

/* 0x48d950: the lead eased (0.1 of the new), the target the car's place plus it, the field of view
   wanted between +0x44 at the radius and +0x54 at the camera */
static void ts_track(Cam *k, const int32_t *r)
{
    int s = k->slot, i = ts_idx[s];
    int32_t l[3], pos[3], d[3];
    ts_lead_of(l, k, Car_Get(k->car), r);
    for (int q = 0; q < 3; q++) ts_lead[s][q] = fx_mul(ts_lead[s][q], 0xe666);
    for (int q = 0; q < 3; q++) ts_lead[s][q] += fx_mul(l[q], 0x1999);
    T3(i, 0x28, pos);
    for (int q = 0; q < 3; q++) ts_target[s][q] = r[12 + q] + ts_lead[s][q], d[q] = ts_target[s][q] - pos[q];
    int32_t t = (int32_t)(((int64_t)(uint32_t)Vec_Length(d) << 16) / T32(i, 0x40));
    ts_fov_t[s] = fx_mul(0x10000 - t, T32(i, 0x54)) + fx_mul(t, T32(i, 0x44));
}

/* 0x48d0f0: the camera at its place (along its dolly by twice the share), shaking by up to half a
   unit while the car was within +0x58; its own rows while not following, else looking at the aim
   level across; the field of view's scale (0.625, times the field of view above 1.0), rolled by +0x02 */
static void ts_place(Cam *k, const int32_t *r)
{
    int s = k->slot, i = ts_idx[s];
    if (on_car(i)) {
        interior_place(k, r);   /* 0x4869e0 */
        return;
    }
    int32_t p[3], car[3] = {r[12], r[13], r[14]};
    T3(i, 0x28, p);
    if (0 < T32(i, 0x60)) {
        int32_t e[3];
        T3(i, 0x34, e);
        for (int q = 0; q < 3; q++) p[q] += fx_mul(fx_mul(e[q] - p[q], ts_slide[s]), 0x20000);
    }
    if (ts_dist[s] < T32(i, 0x58)) {
        uint32_t b = exe_u32(0x5112f0);
        float kf;
        memcpy(&kf, &b, 4);
        int32_t j[3];
        for (int q = 0; q < 3; q++) j[q] = -(int32_t)lrintf((float)crt_rand() * kf * -65536.0f) - 0x8000;
        /* (the original takes the length and keeps the vector as it is, zero only when zero) */
        if (Fx_Sqrt((uint32_t)fx_mul(j[0], j[0]) + (uint32_t)fx_mul(j[1], j[1]) + (uint32_t)fx_mul(j[2], j[2])) == 0) j[0] = j[1] = j[2] = 0;
        for (int q = 0; q < 3; q++) p[q] += j[q];
    }
    int32_t d[3] = {p[0] - car[0], p[1] - car[1], p[2] - car[2]};
    ts_dist[s] = Vec_Length(d);
    int32_t fov;
    if (!ts_follow[s]) {
        int32_t v[3];
        T3(i, 0x04, v), set_row(k->m, 0, v);
        T3(i, 0x10, v), set_row(k->m, 1, v);
        T3(i, 0x1c, v), set_row(k->m, 2, v);
        fov = T32(i, 0x54);
    } else {
        int32_t f[3] = {ts_aim[s][0] - p[0], ts_aim[s][1] - p[1], ts_aim[s][2] - p[2]};
        Vec_Normalize(f);
        int32_t x[3] = {fx_mul(f[2], 0x10000), 0, -fx_mul(f[0], 0x10000)};
        int32_t u[3] = {fx_mul(f[1], x[2]), fx_mul(f[2], x[0]) - fx_mul(f[0], x[2]), -fx_mul(f[1], x[0])};
        Vec_Normalize(x), Vec_Normalize(u), Vec_Normalize(f);
        set_row(k->m, 0, x), set_row(k->m, 1, u), set_row(k->m, 2, f);
        fov = ts_fov[s];
    }
    k->f54 = fov < 0x10000 ? 0xa000 : fx_mul(fov, 0xa000);
    k->m[12] = k->m[13] = k->m[14] = 0;
    FxMat rr, a, o;
    Mat_Identity(&rr);
    uint32_t ang = (uint16_t)troll(i);
    rr.m[0] = rr.m[5] = g_sin[(ang + 0x400) & 0xfff];
    rr.m[4] = g_sin[ang & 0xfff];
    rr.m[1] = -rr.m[4];
    memcpy(a.m, k->m, sizeof a.m);
    Mat_Mul(&o, &rr, &a);
    memcpy(k->m, o.m, sizeof o.m);
    k->m[12] = p[0], k->m[13] = p[1], k->m[14] = p[2];
    k->f48 = 0;
    k->f4c = fx_mul(0x7ffd, (int32_t)(((int64_t)k->f54 << 16) / 0xa000));
    k->f50 = 0, k->f58 = 0, k->f5c = 0x10000;
}

/* 0x48ce80: following, the lead and target at once; else the target on the camera's forward axis at
   the radius (unless the car is within it or behind the camera); the aim and the field of view taken
   at once */
static void ts_start(Cam *k, const int32_t *r)
{
    int s = k->slot, i = ts_idx[s];
    if (on_car(i)) {
        interior_place(k, r);   /* 0x486910 */
        return;
    }
    ts_slide[s] = 0;
    if (ts_follow[s]) {
        ts_lead_of(ts_lead[s], k, Car_Get(k->car), r);
        ts_track(k, r);
        if (0 < ts_rate[s]) ts_slide[s] = 0x10000;
        ts_aimed[s] = 1;
    } else {
        int32_t pos[3], f[3], d[3];
        T3(i, 0x28, pos), T3(i, 0x1c, f);
        for (int q = 0; q < 3; q++) d[q] = r[12 + q] - pos[q];
        int32_t rad = T32(i, 0x40);
        if (Vec_Length(d) < rad || (dot3(f, d) < 0 && 0 < rad)) ts_track(k, r);
        else {
            for (int q = 0; q < 3; q++) ts_target[s][q] = fx_mul(f[q], rad) + pos[q];
            ts_fov_t[s] = T32(i, 0x54);
        }
        ts_lead[s][0] = ts_lead[s][1] = ts_lead[s][2] = 0;
    }
    ts_speed[s] = 0;
    memcpy(ts_aim[s], ts_target[s], 12);
    ts_fov[s] = ts_fov_t[s];
    ts_place(k, r);
}

/* 0x48cae0: camera idx; on the car the interior's roof or behind view, else following at once when
   the car is within the radius or behind it */
static void ts_init(Cam *k, const int32_t *r, int idx)
{
    int s = k->slot;
    ts_idx[s] = idx;
    int m = on_car(idx);
    if (m) {
        interior_init(k, r, m);   /* 0x486740 */
        return;
    }
    int32_t pos[3], f[3], d[3];
    T3(idx, 0x28, pos), T3(idx, 0x1c, f);
    ts_rate[s] = ts_rate2[s] = 0;
    for (int q = 0; q < 3; q++) d[q] = r[12 + q] - pos[q];
    int32_t rad = T32(idx, 0x40);
    ts_follow[s] = ts_aimed[s] = Vec_Length(d) < rad || (dot3(f, d) < 0 && 0 < rad);
    ts_start(k, r);
    ts_slide[s] = 0;
}

/* 0x48db00, 0x48dc30, 0x48dca0: the aim moved towards the target by rate (unit, taken when within
   it, then kept on it); the field of view by rate; the dolly by v up to its end */
static void ts_aim_step(int s, int32_t rate)
{
    if (!ts_aimed[s]) {
        int32_t d[3] = {ts_target[s][0] - ts_aim[s][0], ts_target[s][1] - ts_aim[s][1], ts_target[s][2] - ts_aim[s][2]};
        if (Vec_Length(d) < rate) ts_aimed[s] = 1;
        else {
            Vec_Normalize(d);
            for (int q = 0; q < 3; q++) ts_aim[s][q] += fx_mul(d[q], rate);
        }
        if (!ts_aimed[s]) return;
    }
    memcpy(ts_aim[s], ts_target[s], 12);
}

static void ts_fov_step(int s, int32_t rate)
{
    int32_t d = ts_fov_t[s] - ts_fov[s];
    if ((d < 0 ? -d : d) < rate) ts_fov[s] = ts_fov_t[s];
    else ts_fov[s] += 0 < d ? rate : -rate;
}

static void ts_slide_step(int s, int32_t v)
{
    if (v <= 0) return;
    ts_slide[s] += v;
    if (ts_slide[s] > 0x10000) ts_slide[s] = 0x10000;
}

/* 0x48cc30: on the car the interior's step; else following once the car is within the radius, then
   the rates eased (0.2 of the new: the closing ones while nearer than the last place's distance),
   the target, the aim, the field of view and the dolly */
static void ts_step(Cam *k, const int32_t *r, int flag)
{
    int s = k->slot, i = ts_idx[s];
    if (on_car(i)) {
        interior_step(k, r, flag);
        return;
    }
    int32_t pos[3], d[3];
    T3(i, 0x28, pos);
    for (int q = 0; q < 3; q++) d[q] = pos[q] - r[12 + q];
    int32_t l = Vec_Length(d);
    if (ts_follow[s]) {
        bool closing = l < ts_dist[s];
        int32_t a = T32(i, closing ? 0x4c : 0x50), b = T32(i, closing ? 0x64 : 0x68);
        ts_rate[s] = fx_mul(ts_rate[s], 0xcccc) + fx_mul(a, 0x3333);
        ts_rate2[s] = fx_mul(ts_rate2[s], 0xcccc) + fx_mul(b, 0x3333);
        ts_speed[s] = fx_mul(ts_speed[s], 0xe666) + fx_mul(T32(i, 0x60), 0x1999);
        ts_track(k, r);
        ts_aim_step(s, ts_rate[s]);
        ts_fov_step(s, ts_rate2[s]);
        ts_slide_step(s, ts_speed[s]);
        ts_place(k, r);
        return;
    }
    if (l < T32(i, 0x40)) ts_follow[s] = 1;
    ts_place(k, r);
}

int Camera_TscIndex(int p) { return ts_idx[slots[cur[p] + 2 * p].slot]; }   /* 0x421980 (0x48d930) */

/* 0x423300: a camera of a kind made for its car */
static void cam_init(Cam *k, int kind, int sub)
{
    k->kind = kind;
    const int32_t *r = follow(k->car);
    switch (kind) {
    case 1: interior_init(k, r, 1); break;
    case 2: interior_init(k, r, 0); break;
    case 10: interior_init(k, r, 2); break;
    case 4: chase_init(k, r, 0); break;
    case 5: chase_init(k, r, 4); break;
    case 6: chase_init(k, r, 3); break;
    case 3:   /* 0x476410 */
        Cockpit_Init(k->slot, k->car, CI8(Car_Get(k->car), 0xb1b), k->m);
        k->f58 = 0, k->f50 = 0, k->f48 = 0x10000, k->f54 = 0xa000, k->f4c = 0x1999, k->f5c = 0x10000;   /* 0x4778b0 */
        break;
    case 7: ts_init(k, r, sub); break;   /* 0x48cae0 */
    default: break;
    }
}

/* 0x423460: a camera's step; the ground lift (+0x60) from the height under it */
static void cam_step(Cam *k)
{
    const Car *c = Car_Get(k->car);
    int flag = CI32(c, 0xc04) != 0 && !look[k->player];
    const int32_t *r = follow(k->car);
    switch (k->kind) {
    case 1: case 2: case 10: interior_step(k, r, flag || CI32(c, 0xb60)); break;
    case 4: case 5: case 6: chase_step(k, r, flag); break;
    case 3:   /* 0x4765e0 */
        Cockpit_Step(k->car, r, k->m);
        k->f58 = 0, k->f50 = 0, k->f48 = 0x10000, k->f54 = 0xa000, k->f4c = 0x1999, k->f5c = 0x10000;
        break;
    case 7: ts_step(k, r, flag || CI32(c, 0xb60)); break;   /* 0x48cc30 */
    default: break;
    }
    PENDING_NOTE(0x4930b0, "Camera_GroundLift");
    k->f60 = 0;
}


void Camera_LookAround(int p, bool on)
{
    if (!on) {
        if (look[p]) look[p] = 3;
    } else if (look[p] != 2) {
        if (look[p] == 0) look_ph[p] = 0, look_k[p] = 0;
        look[p] = 1;
    }
}

/* 0x423b20: the followed frame: the car's orientation (+0x750); with the look-around the car's frame
   (+0x754) blended by its share to the level one turned about the up axis by the swing: the sine of
   its phase times 20 degrees, and 45 more over the distance's 8 to 24 */
static void cam_ref(int p)
{
    const Car *c = Car_Get(p);
    for (uint32_t i = 0; i < 12; i++) ref[p][i] = CI32(c, 4 * i);
    ref[p][12] = CI32(c, 0x30), ref[p][13] = CI32(c, 0x34), ref[p][14] = CI32(c, 0x38), ref[p][15] = 0x10000;
    if (Gs_Option(6)) PENDING_NOTE(0x423b20, "Camera_Ref (option 6)");
    if (!look[p]) return;
    int32_t d = set_dist[p];
    int32_t amp = (int32_t)((((int64_t)(d - 0x80000)) << 16) / 0x100000) * 0x5a + 0x140000;
    int32_t deg = fx_mul(g_sin[look_ph[p] & 0xfff], amp);
    uint16_t a = (uint16_t)llrint((double)deg * (1.0 / 5760.0));
    int32_t own[16];
    for (uint32_t i = 0; i < 16; i++) own[i] = CI32(c, 0x40 + 4 * i);   /* +0x754 */
    FxMat lv, r, o;
    memcpy(lv.m, ref[p], sizeof lv.m);
    level(lv.m);   /* 0x423070 */
    memset(r.m, 0, sizeof r.m);
    int32_t s = g_sin[a & 0xfff], co = g_sin[(a + 0x400) & 0xfff];
    r.m[0] = co, r.m[2] = -s, r.m[5] = 0x10000, r.m[8] = s, r.m[10] = co, r.m[15] = 0x10000;
    Mat_Mul(&o, &r, &lv);
    blend(ref[p], own, o.m, look_k[p], look_k[p], look_k[p], 0);
}

static void cam_copy(Cam *d, const Cam *s)   /* 0x423ee0: all but the indices */
{
    d->kind = s->kind;
    memcpy(d->m, s->m, sizeof d->m);
    d->f48 = s->f48, d->f4c = s->f4c, d->f54 = s->f54, d->f58 = s->f58, d->f5c = s->f5c, d->f50 = s->f50, d->f60 = s->f60;
}

/* 0x4236b0: a camera put back at its car by its kind: interior (0x486910), cockpit (0x477850),
   chase (0x447a00: its axes the car's), trackside (0x48ce80) */
static void cam_place(Cam *k)
{
    const int32_t *r = follow(k->car);
    switch (k->kind) {
    case 1: case 2: case 10: {
        int32_t t[16];
        interior_target(t, r, in_mode[k->slot]);
        memcpy(k->m, t, sizeof t);
        interior_place(k, r);
        break;
    }
    case 3: Cockpit_Place(k->car, k->m); break;
    case 4: case 5: case 6:
        row(chase_up[k->slot], r, 1);
        row(chase_fwd[k->slot], r, 0);
        chase_place(k, r);
        break;
    case 7: ts_start(k, r); break;
    default: break;
    }
}

/* 0x421d80: player p's camera put back at its car (both while blending), its ground lift cleared,
   the next step taken at once */
void Camera_Replace(int p)
{
    uint8_t c = cur[p];
    cam_ref(p);   /* 0x423b20 */
    Cam *k = &slots[(uint8_t)(c + 2 * p) & 7];
    cam_place(k);
    k->f60 = 0;
    if (Camera_Kind(p) == 8) {
        Cam *o = &slots[(uint8_t)(2 * p - c + 1) & 7];
        cam_place(o);
        o->f60 = 0;
    }
    snap[p] = 1;
}

void Camera_Reset(int p)
{
    look[p] = 0;   /* (0x421610) */
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

int Camera_Car(int p) { return slots[cur[p] + 2 * p].car; }   /* 0x422fb0 */
int Camera_Kind(int p) { return out[p].kind != 8 ? slots[cur[p] + 2 * p].kind : 8; }   /* 0x422f50 */

/* 0x4238e0, 0x423900 (a switch done): leaving the cockpit the world's mode 0 (0x4764c0), entering it 1
   (0x476500). (Their interior and trackside parts, 0x486c00, 0x486be0, 0x48d850, set the interior's
   field of view, which the port's interior keeps at 1.0.) */
static void switched(const Cam *was, const Cam *now)
{
    if (was->kind == 3) Cockpit_On(was->car, false);
    if (now->kind == 3) Cockpit_On(now->car, true);
}

/* 0x4232a0: whether a kind can be had: 7 with the stage's cameras (0x48ca90), 3 open */
static bool allowed(int p, int kind)
{
    switch (kind) {
    case 1: case 2: case 4: case 5: case 6: case 10: return true;
    case 3: return Cockpit_Ready(p);   /* 0x476520 */
    case 7: return ntsc != 0;
    default: return false;
    }
}

/* 0x421720: the player's other record made kind on car (a trackside camera idx, 0xffff the nearest
   the player's car), switched at once or, with smooth and neither kind 3, blended */
void Camera_SelectAt(int p, int kind, int idx, int car, bool smooth)
{
    bool ok = Camera_Kind(p) != 8 && allowed(p, kind);
    cam_ref(p);
    if (!ok) return;
    Cam *c = &slots[cur[p] + 2 * p], *o = &slots[2 * p + 1 - cur[p]];
    bool b = smooth && kind != 3 && c->kind != 3;
    if (kind == 7 && idx == 0xffff) {
        const Car *pc = Car_Get(p);
        int32_t at[3] = {CI32(pc, 0x2d0), CI32(pc, 0x2d4), CI32(pc, 0x2d8)};
        idx = ts_nearest(at);   /* 0x48d8b0 */
    }
    o->kind = kind;
    o->car = (uint8_t)car;   /* (0x4ae410 on a change, empty) */
    /* 0x423810, 0x423860: the new camera from the old (chase on the same car: its axes) */
    if ((kind == 4 || kind == 5) && c->car == o->car && (c->kind == 4 || c->kind == 5 || c->kind == 6)) {
        memcpy(chase_up[o->slot], chase_up[c->slot], 12);
        memcpy(chase_fwd[o->slot], chase_fwd[c->slot], 12);
    } else if (kind == 4 || kind == 5) {
        const int32_t *r = follow(o->car);
        row(chase_up[o->slot], r, 1);
        row(chase_fwd[o->slot], r, 0);
    }
    if (kind == 3) Cockpit_Snap(o->car);   /* 0x423860 -> 0x4764e0 */
    cam_init(o, kind, idx);
    o->f60 = 0;
    if (b) {
        sw[p] = 0xc8000;
        return;
    }
    switched(c, o);
    snap[p] = 1;
    cur[p] = (uint8_t)(1 - cur[p]);
}

void Camera_Select(int p, int kind, bool smooth) { Camera_SelectAt(p, kind, 0xffff, Camera_Car(p), smooth); }

/* 0x421930 (Race_Update, with a trackside camera): the camera nearest the followed car's place, when
   it is another */
void Camera_Trackside(int p)
{
    const Car *c = Car_Get(Camera_Car(p));
    int32_t at[3] = {CI32(c, 0x30), CI32(c, 0x34), CI32(c, 0x38)};   /* 0x423db0: its +0x750's place */
    int i = ts_nearest(at);
    if (i != Camera_TscIndex(p)) Camera_SelectAt(p, 7, i, Camera_Car(p), false);
}

/* 0x423970: a kind the cycle may take: 1, 3, 4, 5 when they can be had, 2 but in the replay */
static bool cyclable(int p, int kind, int state)
{
    switch (kind) {
    case 2: if (state == 10) return false; /* fall through */
    case 1: case 4: case 5: return true;
    case 3: return Cockpit_Ready(p);   /* 0x476520 */
    default: return false;
    }
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
    if (look[p]) {
        if (look[p] == 1 && (look_k[p] += 0xccc) > 0xffff) look[p] = 2, look_k[p] = 0x10000;
        else if (look[p] == 3 && (look_k[p] -= 0xccc) < 1) look[p] = 0, look_k[p] = 0;
        look_ph[p] = (uint16_t)(look_ph[p] + 0x44);
        if ((int16_t)look_ph[p] > 0xfff) look_ph[p] = (uint16_t)(look_ph[p] - 0x1000);
    }
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
    switched(c, o);   /* 0x4238e0, 0x423900, 0x4239e0 */
    cam_copy(&out[p], o);
    cur[p] = (uint8_t)(1 - cur[p]);
    sw[p] -= dt;
}

static int32_t zoom_f[4], near_f[4];   /* 0x5391cc, 0x538df0: the frame's interpolated +0x54 and +0x4c */

void Camera_Frame(int p, int32_t frac, Obj *camera)
{
    zoom_f[p] = lerp(prev[p].f54, out[p].f54, frac);
    near_f[p] = lerp(prev[p].f4c, out[p].f4c, frac);
    int32_t m[16];
    blend(m, prev[p].m, out[p].m, frac, frac, frac, 1);   /* 0x423de0 */
    PENDING_NOTE(0x422140, "Camera_Frame (the ground lift's tilt)");
    for (int i = 0; i < 12; i++) camera->local.m[i] = m[i];
    camera->local.m[12] = m[12], camera->local.m[13] = m[13], camera->local.m[14] = m[14];
    for (Obj *o = camera; o; o = o->parent) o->dirty = 1;
}

/* 0x422d40: the projection's scales from the frame's zoom (by 2.46, the screen's aspect and 1.14 across,
   2.08 up), one of them doubled on a split screen (across when it is split left and right); with
   option 2 mirrored; the super special's far plane at least 150. The far plane is the port's (the
   draw distance's, 0x423f30 by 0x422f90, not ported). */
void Camera_Projection(int p)
{
    int w, h;
    D3d_ScreenSize(&w, &h);
    int32_t aspect = (int32_t)llrint((double)w / (double)h * 65536.0);
    int32_t xs = fx_mul(fx_mul(fx_mul(zoom_f[p], 0x275c2), aspect), 0x123d7);
    int32_t ys = fx_mul(fx_mul(zoom_f[p], 0x275c2), 0x2147a);
    if (!(Gs_SelBits14() > 1 && Race_State(0) == 10) && Hud_SplitScreen()) {   /* 0x41f3a0, 0x411880 */
        if (!Gs_ModeBit18()) xs *= 2;
        else ys *= 2;
    }
    int32_t far = Proj_Far();
    if (Gs_Option(2)) Proj_Set(-xs, ys, far, near_f[p]);
    else if (Gs_SuperSpecial() && far < 0x960000) Proj_Set(xs, ys, 0x960000, near_f[p]);
    else Proj_Set(xs, ys, far, near_f[p]);
}

void Camera_Position(int p, int32_t pos[3]) { pos[0] = out[p].m[12], pos[1] = out[p].m[13], pos[2] = out[p].m[14]; }
int32_t Camera_Zoom(int p) { return out[p].f54; }
/* 0x4220d0: the step camera's move since the last step (0x538d30 less 0x538e40, 100 n on) */
void Camera_Velocity(int p, int32_t v[3])
{
    for (int k = 0; k < 3; k++) v[k] = p >= 0 && p < 4 ? out[p].m[12 + k] - prev[p].m[12 + k] : 0;
}
/* 0x4baca0 on 0x538d30 + 100 n: the third row of the step camera's matrix (the original indexes the
   records by any car's number, past the players'; the port gives zero there) */
void Camera_Forward(int p, int32_t f[3])
{
    if (p < 0 || p >= 4) {
        f[0] = f[1] = f[2] = 0;
        return;
    }
    f[0] = out[p].m[8], f[1] = out[p].m[9], f[2] = out[p].m[10];
}

/* 0x539200: per player 14 ints of the interior views' head movement (kept in the game state at their
   address: the shake indexes them by any car's number, so cars 4-7 write past the players' four, as
   in the original); the shake sets [9], [10], [12]. 0x539278 is left at the record; the car at
   0x53937c, which the head movement's other functions read, is not kept. */
static uint32_t head(int n, int k) { return 0x539200 + 0x38 * (uint32_t)n + 4 * (uint32_t)k; }

/* above 0.1, 2.5 per unit, at most 1 */
static int32_t shake_level(int32_t x)
{
    int32_t v = fx_mul(x - 0x1999, 0x28000);
    return v < 0 ? 0 : 0x10000 < v ? 0x10000 : v;
}

void Camera_Shake(const Car *c, const int32_t imp[3], bool along)
{
    int n = CI8(c, 0xb1a);
    int32_t l[3];
    gw32(0x539278, head(n, 0));
    Walls_RotT(c, l, imp);   /* Vec_RotateT 0x4b9da0 by the orientation (+0x750) */
    if (along) {
        int32_t v = shake_level(l[2] < 0 ? -l[2] : l[2]);
        if (gms32(head(n, 9)) < v) gw32(head(n, 9), (uint32_t)v), gw32(head(n, 12), l[2] < 0);
    }
    int32_t v = shake_level((int32_t)Fx_Sqrt((uint32_t)fx_mul(l[0], l[0]) + (uint32_t)fx_mul(l[1], l[1]) + (uint32_t)fx_mul(l[2], l[2])));
    if (gms32(head(n, 10)) < v) gw32(head(n, 10), (uint32_t)v);
}

int32_t Camera_ShakeLevel(int p, int k) { return gms32(head(p, k)); }
