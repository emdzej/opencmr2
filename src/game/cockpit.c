/* The cockpit view (see cockpit.h). */
#include "game/cockpit.h"
#include "game/camera.h"
#include "game/car_load.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/weather.h"
#include "game/dash.h"
#include "sim/car.h"
#include "sim/car_internal.h"
#include "sim/sim.h"
#include "world/frame.h"
#include "render/mesh.h"
#include "render/tex.h"
#include "bfl.h"
#include "c3d.h"
#include "crt.h"
#include "exe.h"
#include "fixmath.h"
#include "pending.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* per car (the first two): the model (0x58d49c) and its file (0x58d6a0); the second model's type 9
   and type 5 objects (0x58d340, 0x58d47c); per 0x1c bytes at 0x58d528 the hands' two poses (types
   0x1a, 0x1c), the hand's rest under the wheel (a new node), the head (a new node under the second
   model, the camera's frame), the wheel (0x1b), the wipers (0x16, 0x17); the .cin's section 5 (0x58d4f0:
   +0x00 the first wiper's angles, +0x08 its sweep, +0x0c the head's offset from the wheel, +0x18 the
   head's turn, +0x1c the wheel's angles, +0x24 the second wiper's angles, +0x2c its sweep); the head's
   place at rest (0x58d2f8); the left-hand drive classes (0x58d2f0) */
typedef struct {
    C3d model;
    uint8_t *file;
    BflSlot tex;
    Obj *root, *o9, *o5;
    Obj *pose_a, *hand, *rest, *head, *wheel, *wiper[2];
    const uint8_t *sec5;
    int32_t base[3];
    bool lhd, ready;               /* 0x58d2f0, 0x58d6a8 */
    /* 0x58d368 + 0x24 n: the bob's phase, the head's angles, its offset, the eye (0, 0.2, 0) */
    int16_t phase, ang[3];
    int32_t off[3], eye[3];
    bool snap;                     /* 0x58d3b0: the next step takes the head's pose at once */
    /* the hand (0x58d360 state, 0x58d478 count, 0x58d2d0 the gear seen) */
    int hstate;
    uint8_t hcount, hgear;
    /* the wipers (0x58d4d8 + 0xc n): state, direction, angle, speed */
    int wstate, wdir;
    int16_t wang, wspeed;
    Tex *revct, *digit;            /* 0x58d4c0, 0x58d4c4 */
    /* the rev counter's twelve bands and the gear's seven segments, now and as last drawn (0x58d310 /
       0x58d2a0 + 0x18 n, 0x58d2d4 / 0x58d6b0 + 0xe n; the last ones start unset) */
    uint16_t bands[12], bands_was[12], segs[7], segs_was[7];
} Cockpit;

static Cockpit ck[2];
static uint8_t cls_of[8];          /* 0x58d4d0: per camera record the car's class */
static int32_t level_frame[16];    /* 0x58d560: the followed frame turned to look along the car, pitched */

static int16_t S16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static int32_t S32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static void angles_at(uint16_t a[3], const uint8_t *p) { for (int i = 0; i < 3; i++) a[i] = (uint16_t)S16(p + 2 * i); }

/* The files of cockpits set aside by a reset: a restart loads the cockpits again (s5) while the old
   objects stay hung under the cars' second models, as in the original, whose race heap keeps their
   data until the race ends; the port frees them then (Cockpit_Free) */
static uint8_t *spent[16];
static int nspent;

void Cockpit_Free(void)
{
    for (int i = 0; i < nspent; i++) free(spent[i]);
    nspent = 0;
    for (int i = 0; i < 2; i++) free(ck[i].file), ck[i].file = NULL;
}

/* 0x475f80 (0x421610, the cameras' set-up): nothing loaded, the heads and hands at rest */
void Cockpit_Reset(void)
{
    for (int i = 0; i < 2; i++) {
        Cockpit *k = &ck[i];
        if (k->file) {
            if (nspent == (int)(sizeof spent / sizeof *spent)) free(spent[--nspent]);   /* the port's bound */
            spent[nspent++] = k->file;
        }
        memset(k, 0, sizeof *k);
        k->eye[1] = 0x3333;
        k->hstate = 1;
        k->hcount = 3;
        k->wdir = 1;
        k->snap = true;
        memset(k->bands_was, 0xff, sizeof k->bands_was);
        memset(k->segs_was, 0xff, sizeof k->segs_was);
    }
}

/* 0x476540: the classes driven from the left (7, 8, 9, 13) and the .cin's section 5 */
static void cin_part(int car)
{
    int cls = CI8(Car_Get(car), 0xb1b);
    ck[car].lhd = cls == 8 || cls == 7 || cls == 9 || cls == 13;
    ck[car].sec5 = Car_CinSection(car, 5);
}

/* 0x477340: the model's rev counter and digits textures (named REVCT and DIGIT) */
static void textures(int car)
{
    for (int i = 0; i < g_tex_count; i++) {
        Tex *t = g_tex[i];
        if (!t || !t->name[0]) continue;
        char base[64];
        const char *s = strrchr(t->name, '\\');
        s = s ? s + 1 : t->name;
        size_t n = 0;
        for (; s[n] && s[n] != '.' && n < sizeof base - 1; n++) base[n] = (char)(s[n] >= 'a' && s[n] <= 'z' ? s[n] - 32 : s[n]);
        base[n] = 0;
        if (!strcmp(base, "REVCT")) ck[car].revct = t, Tex_SetBlend(t, 2);
        if (!strcmp(base, "DIGIT")) ck[car].digit = t;
    }
}

/* 0x4760a0 (per camera record, its car): the second model's type 9 and 5 objects; for the first two
   cars, not in selection bit 24, once: <car>a5.c3d (c5 with 0x52afd4's low bits) and its .bfl, the
   model under the second model, its objects found, the hand's rest a copy of the hand's pose under
   the model, the head a node under the second model at the wheel plus the .cin's offset */
void Cockpit_Load(int slot, int car)
{
    bool ok = true;
    if (Gs_ModeBit19() && car != 0) return;
    const Car *c = Car_Get(car);
    if (car < g_all.n && c->second) {
        if (car < 2) ck[car].o9 = Obj_FindType(c->second, 9), ck[car].o5 = Obj_FindType(c->second, 5);
    } else
        ok = false;
    if (car >= 2 || ck[car].head) return;
    if (Gs_SelBit24() || !ok) return;
    Cockpit *k = &ck[car];
    int id = (int)(gm32(0x531350 + 0x30u * (uint32_t)(Gs_Byte537ef6() + car)) & 0x3f);   /* 0x4086b0 (not in mode 4) */
    const char *name = exe_str(exe_u32(0x516bb0 + 4u * (uint32_t)id));   /* Car_Dir 0x4200d0 */
    char p[96];
    bool c5 = (gm32(0x52afd4) & 3) != 0;   /* 0x405d10 */
    snprintf(p, sizeof p, "Game\\Cars\\%s%s.c3d", name, c5 ? "c5" : "a5");
    k->file = File_Load(p, 0);
    uint32_t size = k->file ? File_LastSize() : 0;
    snprintf(p, sizeof p, "Game\\Cars\\%s%s.bfl", name, c5 ? "c5" : "a5");
    Bfl_Mount(&k->tex, p);
    cin_part(car);
    if (!k->file || !k->sec5) return;
    if (!C3d_Load(&k->model, k->file, size, g_tex_count)) return;
    C3d_Textures(k->model.data + k->model.slots, k->model.data + k->model.paths, k->model.ntex, k->model.kind, &k->tex, "");
    C3d_Build(&k->model);
    k->root = C3d_Objects(&k->model, c->second);
    if (!k->root) return;
    k->rest = Obj_New(k->root);
    k->pose_a = Obj_FindType(k->root, 0x1a);
    k->hand = Obj_FindType(k->root, 0x1c);
    k->head = Obj_New(c->second);
    k->wheel = Obj_FindType(k->root, 0x1b);
    k->wiper[0] = Obj_FindType(k->root, 0x16);
    k->wiper[1] = Obj_FindType(k->root, 0x17);
    if (!k->hand || !k->wheel) return;
    k->rest->local = k->hand->local;
    k->rest->mesh = NULL;
    k->rest->kind = 3;
    k->rest->type = k->hand->type;
    Obj_Reparent(k->hand, k->root);   /* 0x4ac7a0 */
    for (int q = 0; q < 3; q++) k->head->local.m[12 + q] = k->wheel->local.m[12 + q] + S32(k->sec5 + 0xc + 4 * q);
    k->ready = true;
    cls_of[slot & 7] = (uint8_t)CI8(c, 0xb1b);
    for (int q = 0; q < 3; q++) k->base[q] = k->head->local.m[12 + q];
    textures(car);
}

bool Cockpit_Ready(int car) { return car >= 0 && car < 2 && ck[car].ready; }   /* 0x476520 */
Obj *Cockpit_Model(int car) { return car >= 0 && car < 2 && ck[car].ready ? ck[car].root : NULL; }

/* 0x4addd0 / 0x4add90: the mask of every mesh of a tree */
static void mask_tree(Obj *o, uint8_t m)
{
    if (o->kind == 0) o->mask = m;
    for (Obj *x = o->child; x; x = x->next) mask_tree(x, m);
}

/* 0x486740's part: an interior view's car's objects: a kind C or A car's type 9 (else its type 5
   again) and its type 5 under the second model (the port keeps the first two cars') */
void Cockpit_InteriorObjs(int car)
{
    const Car *c = Car_Get(car);
    if (car >= g_all.n || !c->second || car >= 2) return;
    char kind = Car_Slot(car)->kind;
    ck[car].o9 = Obj_FindType(c->second, kind == 'C' || kind == 'A' ? 9 : 5);
    ck[car].o5 = Obj_FindType(c->second, 5);
}

/* 0x46b8f0's part: the model's meshes and the type 9 and 5 objects */
void Cockpit_Masks(int car, uint8_t model, uint8_t objs)
{
    if (car < 0 || car >= 2) return;
    Cockpit *k = &ck[car];
    if (k->root) mask_tree(k->root, model);
    if (k->o9) k->o9->mask = objs;
    if (k->o5) k->o5->mask = objs;
}

/* 0x476e00: the head's pose: the bob's phase on by 3; turned by the car's acceleration (+0x408 less
   +0x414) along its axes +0x378 and +0x360 (over 30), a fifth of the way a step (at once after a
   switch); moved against it (over 0.3: sideways -0.01 to 0.1, along -0.06 to 0.06) by 0.04 a step;
   with option 6 lifted by half of +0xa8c */
static void head(Cockpit *k, const Car *c, const int32_t *r)
{
    memcpy(level_frame, r, sizeof level_frame);
    level_frame[0] = -r[8], level_frame[1] = -r[9], level_frame[2] = -r[10];
    level_frame[8] = r[0], level_frame[9] = r[1], level_frame[10] = r[2];
    level_frame[12] = level_frame[13] = level_frame[14] = 0;
    Frame_Pitch(level_frame, exe_u16(0x51c9b0));
    k->phase = (int16_t)((k->phase + 3) % 0x1000);
    int32_t d[3] = {CI32(c, 0x408) - CI32(c, 0x414), CI32(c, 0x40c) - CI32(c, 0x418), CI32(c, 0x410) - CI32(c, 0x41c)};
    int32_t a360[3] = {CI32(c, 0x360), CI32(c, 0x364), CI32(c, 0x368)}, a378[3] = {CI32(c, 0x378), CI32(c, 0x37c), CI32(c, 0x380)};
    int32_t u = fx_mul(a360[0], d[0]) + fx_mul(a360[1], d[1]) + fx_mul(a360[2], d[2]);
    int32_t w = fx_mul(a378[0], d[0]) + fx_mul(a378[1], d[1]) + fx_mul(a378[2], d[2]);
    int16_t t2 = (int16_t)(((int64_t)u << 16) / 0x1e0000), t0 = (int16_t)(((int64_t)w << 16) / 0x1e0000);
    int16_t tg[3] = {t0, S16(k->sec5 + 0x18), t2};
    for (int i = 0; i < 3; i++) k->ang[i] = (int16_t)(k->ang[i] + (tg[i] - k->ang[i]) / 5);
    if (k->snap) memcpy(k->ang, tg, sizeof tg);
    uint16_t a[3] = {(uint16_t)k->ang[0], (uint16_t)k->ang[1], (uint16_t)k->ang[2]};
    Obj_SetAngles(k->head, a);
    int32_t ox = (int32_t)(((int64_t)-u << 16) / 0x4ccc), oz = (int32_t)(((int64_t)-w << 16) / 0x4ccc);
    ox = ox < -0x28f ? -0x28f : ox > 0x1999 ? 0x1999 : ox;
    oz = oz < -0xf5c ? -0xf5c : oz > 0xf5c ? 0xf5c : oz;
    if (!k->snap) {
        k->off[0] += fx_mul(ox - k->off[0], 0xa3d);
        k->off[1] += fx_mul(-k->off[1], 0xa3d);
        k->off[2] += fx_mul(oz - k->off[2], 0xa3d);
    } else {
        k->off[0] = ox, k->off[1] = 0, k->off[2] = oz;
        k->snap = false;
    }
    int32_t pos[3] = {k->base[0] + k->off[0], k->base[1] + k->off[1], k->base[2] + k->off[2]};
    if (Gs_Option(6)) pos[1] += fx_mul(CI32(c, 0xa8c), 0x8000);
    Obj_SetPosition(k->head, pos);
}

/* 0x476850: the hand's share between the wheel and the gear lever: on a change of gear (+0xb20) or the
   handbrake (+0x1d8) over to the lever in 7 steps, held 4 (longer while it goes on), back in 7 */
static int32_t hand_share(Cockpit *k, const Car *c)
{
    int32_t t = 0;
    uint8_t g = (uint8_t)CI8(c, 0xb20);
    bool hb = CI32(c, 0x1d8) != 0;
    if (k->hstate == 1 && (k->hgear != g || hb)) k->hgear = g, k->hstate = 2, k->hcount = 0;
    if (k->hstate == 2) {
        t = (int32_t)(((int64_t)k->hcount << 32) / 0x70000);
        k->hgear = g;
        if ((uint8_t)++k->hcount > 7) k->hstate = 3, k->hcount = 0;
    }
    if (k->hstate == 3) {
        t = 0x10000;
        k->hcount++;
        if (k->hgear != g || hb) k->hcount = 0, k->hgear = g;
        if (k->hcount > 3) k->hstate = 4, k->hcount = 0;
    }
    if (k->hstate == 4) {
        t = 0x10000 - (int32_t)(((int64_t)k->hcount << 32) / 0x70000);
        if (k->hgear == g && !hb) {
            if ((uint8_t)++k->hcount > 7) {
                k->hstate = 1, k->hcount = 0;
                return 0;
            }
        } else {
            uint8_t was = k->hcount;
            k->hgear = g, k->hstate = 2, k->hcount = (uint8_t)(7 - was);
        }
    }
    return t;
}

/* local = local rot (the translation kept) */
static void turn_local(Obj *o, const int32_t rot[16])
{
    int32_t p[3] = {o->local.m[12], o->local.m[13], o->local.m[14]};
    o->local.m[12] = o->local.m[13] = o->local.m[14] = 0;
    FxMat r, out;
    memcpy(r.m, rot, sizeof r.m);
    Mat_Mul(&out, &o->local, &r);
    o->local = out;
    o->local.m[12] = p[0], o->local.m[13] = p[1], o->local.m[14] = p[2];
}

/* 0x476640: the wheel turned by the steering (+0xb10) about its first axis; the hand blended between
   its rest on the wheel (turned with it) and the lever's pose by the share, in the table's steps */
static void wheel(Cockpit *k, const Car *c)
{
    static const int32_t steps[13] = {0, 0x51e, 0xccc, 0x1999, 0x3333, 0x6666, 0x9999, 0xcccc, 0xe666, 0xf333, 0xfae1, 0x10000, 0x10000};
    uint16_t a[3];
    angles_at(a, k->sec5 + 0x1c);
    Obj_SetAngles(k->wheel, a);
    int32_t axis[3] = {k->wheel->local.m[0], k->wheel->local.m[1], k->wheel->local.m[2]}, rot[16];
    Mat_AxisAngleRows(rot, axis, -CI16(c, 0xb10));
    int32_t wp[3] = {k->wheel->local.m[12], k->wheel->local.m[13], k->wheel->local.m[14]};
    turn_local(k->wheel, rot);
    Obj_SetAngles(k->rest, a);
    FxMat r, on;
    memcpy(r.m, rot, sizeof r.m);
    Mat_Mul(&on, &k->rest->local, &r);
    on.m[12] += wp[0], on.m[13] += wp[1], on.m[14] += wp[2];
    FxMat lever = k->pose_a ? k->pose_a->local : on;
    int32_t s = hand_share(k, c);
    int32_t o[16];
    Frame_Blend(o, on.m, lever.m, 0, 0, steps[fx_mul(s, 0xc0000) >> 16], 1);
    memcpy(k->hand->local.m, o, sizeof o);
    for (Obj *x = k->hand; x; x = x->parent) x->dirty = 1;
}

/* 0x476c70: the wipers in the rain (the weather record's type 1 and level): on above 0.2, fast above
   0.5, slow again below 0.4, off below 0.1 (after the sweep back); 45-55 or 91-101 a step */
static void wiper_step(Cockpit *k, int car)
{
    if (Weather_Kind(car) == 1) {
        int32_t w = Weather_Level(car);
        if (0x3333 < w && k->wstate == 0) k->wstate = 2, k->wspeed = (int16_t)(crt_rand() % 0xb + 0x2d);
        if (0x8000 < Weather_Level(car) && k->wstate == 2) k->wstate = 3, k->wspeed = (int16_t)(crt_rand() % 0xb + 0x5b);
        if (Weather_Level(car) < 0x1999 && k->wstate == 2) k->wstate = 1;
        if (Weather_Level(car) < 0x6666 && k->wstate == 3) k->wstate = 2, k->wspeed = (int16_t)(crt_rand() % 0xb + 0x2d);
    } else
        k->wstate = 0;
    if (!k->wstate) return;
    if (k->wdir == 0) {
        k->wang = (int16_t)(k->wang - k->wspeed);
        if (k->wang < 0) {
            k->wdir = 1;
            if (k->wstate == 1) k->wstate = 0;
            k->wang = 0;
        }
    } else {
        k->wang = (int16_t)(k->wang + k->wspeed);
        int16_t max = S16(k->sec5 + 8);
        if (max < k->wang) k->wdir = 0, k->wang = max;
    }
}

/* 0x476a40: the wipers turned about their second axes by the sweep (the second one's scaled by the
   sweeps' ratio; the other way when driven from the left) */
static void wipers(Cockpit *k, int car)
{
    for (int i = 0; i < 2; i++) {
        Obj *w = k->wiper[i];
        if (!w) continue;
        uint16_t a[3];
        angles_at(a, k->sec5 + (i ? 0x24 : 0));
        Obj_SetAngles(w, a);
        int32_t axis[3] = {w->local.m[4], w->local.m[5], w->local.m[6]}, rot[16];
        int32_t g = i == 0 ? k->wang : S16(k->sec5 + 0x2c) * k->wang / S16(k->sec5 + 8);
        if (k->lhd) g = -g;
        Mat_AxisAngleRows(rot, axis, (int16_t)g);
        turn_local(w, rot);
    }
    wiper_step(k, car);
}

/* 0x4775f0: the gear's digit on the DIGIT texture's seven segments (alpha 0x80, 0xe0, 0xd0, 0xc0, 0xb0,
   0xa0, 0x90 in it), 0 to 9, anything else the top segment only; drawn again from its copy (16 + car)
   when they change */
static void digit(Cockpit *k, int car, int gear)
{
    static const uint8_t on[11][7] = {
        {0, 1, 1, 1, 1, 1, 1}, {0, 0, 1, 1, 0, 0, 0}, {1, 1, 1, 0, 1, 1, 0}, {1, 1, 1, 1, 1, 0, 0},
        {1, 0, 1, 1, 0, 0, 1}, {1, 1, 0, 1, 1, 0, 1}, {1, 1, 0, 1, 1, 1, 1}, {1, 0, 0, 0, 0, 1, 0},
        {1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 0, 0, 1}, {1, 0, 0, 0, 0, 0, 0},
    };
    const uint8_t *o = on[gear >= 0 && gear <= 9 ? gear : 10];
    for (int i = 0; i < 7; i++) k->segs[i] = o[i] ? 0xff : 0;
    if (!memcmp(k->segs, k->segs_was, sizeof k->segs)) return;
    int n = car + 2 * 8;
    Tex_RestoreCopy(k->digit, n);
    Tex_KeyAlpha(k->digit, 0x80, (uint8_t)k->segs[0], 0xe0, (uint8_t)k->segs[1], 0xd0, (uint8_t)k->segs[2], n);
    Tex_KeyAlpha(k->digit, 0xc0, (uint8_t)k->segs[3], 0xb0, (uint8_t)k->segs[4], 0xa0, (uint8_t)k->segs[5], n);
    Tex_KeyAlpha(k->digit, 0x90, (uint8_t)k->segs[6], 0x90, (uint8_t)k->segs[6], 0x90, (uint8_t)k->segs[6], n);
    memcpy(k->segs_was, k->segs, sizeof k->segs);
}

/* 0x477460: the rev counter: of the REVCT texture's bands (alpha 0xe0 down to 0x30 in it) the first
   always lit, then one for each eleventh of the shown revs (0x445dd0), the last at 0x6c; the gear's
   digit; drawn again from the copy (8 + car) when they change */
static void dash(Cockpit *k, int car)
{
    int32_t lit = fx_mul(Dash_Revs(car), 0xb0000) >> 16;
    for (int i = 0; i < 11; i++) k->bands[i] = lit <= i ? 0 : 0xff;
    if (k->digit) digit(k, car, CI8(Car_Get(car), 0xb1e));
    if (!k->revct) return;
    k->bands[11] = 0x6c;
    if (!memcmp(k->bands, k->bands_was, sizeof k->bands)) return;
    int n = car + 8;
    const uint16_t *b = k->bands;
    Tex_RestoreCopy(k->revct, n);
    Tex_KeyAlpha(k->revct, 0xe0, 0xff, 0xd0, (uint8_t)b[1], 0xc0, (uint8_t)b[2], n);
    Tex_KeyAlpha(k->revct, 0xb0, (uint8_t)b[3], 0xa0, (uint8_t)b[4], 0x90, (uint8_t)b[5], n);
    Tex_KeyAlpha(k->revct, 0x80, (uint8_t)b[6], 0x70, (uint8_t)b[7], 0x60, (uint8_t)b[8], n);
    Tex_KeyAlpha(k->revct, 0x50, (uint8_t)b[9], 0x40, (uint8_t)b[10], 0x30, (uint8_t)b[11], n);
    memcpy(k->bands_was, k->bands, sizeof k->bands);
}

/* 0x4778b0: the camera at the head: the eye (0, 0.2, 0) in a frame looking along the car, pitched by
   0x51c9b0, through the head into the second model's pose (the car's +0x40) */
static void place(int32_t m[16], Cockpit *k, const Car *c)
{
    FxMat e, t, car, o;
    Mat_Identity(&e);
    e.m[0] = 0, e.m[1] = 0, e.m[2] = -0x10000;
    e.m[4] = 0, e.m[5] = 0x10000, e.m[6] = 0;
    e.m[8] = 0x10000, e.m[9] = 0, e.m[10] = 0;
    Frame_Pitch(e.m, exe_u16(0x51c9b0));
    e.m[12] = k->eye[0], e.m[13] = k->eye[1], e.m[14] = k->eye[2];
    Mat_Mul(&t, &e, &k->head->local);
    for (int i = 0; i < 16; i++) car.m[i] = CI32(c, 0x40 + 4u * (uint32_t)i);
    Mat_Mul(&o, &t, &car);
    memcpy(m, o.m, sizeof o.m);
}

/* 0x476410 (Camera_Init, kind 3): the record's class; the camera placed (0x477850). (The model is put
   in its stage section, 0x4b8b10: the port draws it with the cars.) */
void Cockpit_Init(int slot, int car, int cls, int32_t m[16])
{
    cls_of[slot & 7] = (uint8_t)cls;
    if (car < 0 || car >= 2 || !ck[car].head) return;
    place(m, &ck[car], Car_Get(car));
}

/* 0x4765e0 (Camera_StepOne, kind 3) */
void Cockpit_Step(int car, const int32_t *r, int32_t m[16])
{
    if (car < 0 || car >= 2 || !ck[car].ready) return;
    Cockpit *k = &ck[car];
    const Car *c = Car_Get(car);
    head(k, c, r);
    wheel(k, c);
    wipers(k, car);
    dash(k, car);   /* 0x477460 */
    place(m, k, c);
}

/* 0x477850 with 0x4778b0: the camera put back at the head */
void Cockpit_Place(int car, int32_t m[16])
{
    if (car < 0 || car >= 2 || !ck[car].head) return;
    place(m, &ck[car], Car_Get(car));
}

/* 0x4764e0: the next step's head pose at once (a switch to the view) */
void Cockpit_Snap(int car) { if (car >= 0 && car < 2) ck[car].snap = true; }

/* 0x4764c0 / 0x476500: the world's effects mode while the view is on (0) and off (1) */
void Cockpit_On(int car, bool on) { if (car >= 0 && car < 2 && ck[car].root) World_SetMode(on ? 0 : 1); }
