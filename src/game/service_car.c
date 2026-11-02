/* The service screens' car on its stand (see service.h, 0x506080-0x509dc0, 0x500550-0x5009c0): each
   player's car loaded with its tarmac (L) and snow (S) wheels, shown on the player's turn, turned to
   the view of the set-up or repair part (12 views, 0x5273c0), and the lines from the panel to the
   part's four points on the screen (0x527420, by class). */
#include "game/service_int.h"
#include "bfl.h"
#include "c3d.h"
#include "exe.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "game/paths.h"
#include "game/timer.h"
#include "pending.h"
#include "render/d3d7.h"
#include "render/mesh.h"
#include "render/tex.h"
#include "render/twod.h"
#include "sim/car.h"
#include "world/obj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* a player's car (0x82cb78, 0x54 bytes each) */
typedef struct {
    int cls;                    /* +0x00 */
    Obj *second, *root;         /* +0x04 type 5, +0x08 */
    Obj *root_l, *root_s;       /* +0x0c, +0x10: the tarmac and snow wheels' models */
    Obj *wheel[4];              /* +0x14: types 1-4 */
    uint8_t *mesh[4], *mesh_l[4], *mesh_s[4];   /* +0x24, +0x34, +0x44: the wheels' meshes */
} SvCar;

static SvCar cars[4];
static C3d models[4][3];
static uint8_t *files[4][3];          /* DAT_0082d19c, 0x8311d8, 0x831158 */
static uint32_t sizes[4][3];
static BflSlot textures[4];           /* DAT_00831088 */
static int tyre_shown[4];             /* DAT_0082d15c */

static int16_t ang[4][3], ang_to[4][3], ang_from[4][3], ang_d[4][3];   /* 0x82d0b8, 0x82fce0, 0x82d0f8, 0x82d0d8 */
static bool turning[4];               /* DAT_0083131c */
static uint32_t turn_at[4], turn_len; /* DAT_00831148, DAT_00831080 */

static int32_t line[4][4][2], line_from[4][4][2], line_to[4][4][2], line_d[4][4][2];   /* 0x82ac68, 0x82acf0, 0x82ad70, 0x82adf0 */
static int32_t points[12][4][2];      /* DAT_008313c8: each view's part points on the screen */

/* 0x50a020, 0x50a050: the class has snow wheels for tyre 6, tarmac wheels for tyres 0-1 (not the
   classes 8, 10, 11, 13) */
static bool has_s(int cls, int tyre) { return tyre == 6 && cls != 0xb && cls != 8 && cls != 10 && cls != 0xd; }
static bool has_l(int cls, int tyre) { return tyre < 2 && cls != 0xb && cls != 8 && cls != 10 && cls != 0xd; }

static uint32_t detail(void) { return gm32(0x52afd4) & 3; }   /* 0x405d10 */

/* 0x4add90 / 0x4addd0: the pass mask of a tree's meshes (below a mesh only) */
static void mask_list(Obj *o, uint8_t m)
{
    for (; o; o = o->next)
        if (o->kind == 0) {
            o->mask = m;
            if (o->child) mask_list(o->child, m);
        }
}
static void mask_tree(Obj *o, uint8_t m)
{
    if (!o) return;
    if (o->kind == 0) o->mask = m;
    if (o->child) mask_list(o->child, m);
}

static void load_model(int p, int k, const char *path)
{
    files[p][k] = File_Load(path, 0);
    sizes[p][k] = files[p][k] ? File_LastSize() : 0;
}

static Obj *build(int p, int k)
{
    if (!files[p][k]) return NULL;
    C3d *m = &models[p][k];
    if (!C3d_Load(m, files[p][k], sizes[p][k], g_tex_count)) return NULL;
    C3d_Textures(m->data + m->slots, m->data + m->paths, m->ntex, m->kind, &textures[p], "");
    C3d_Build(m);
    return C3d_Objects(m, Sv_World());
}

/* 0x5068b0: a point of the car (player 0's) turned to the angles a, by its local 3 x 3 */
static void turned(const int32_t pt[3], int32_t out[3], const int16_t a[3])
{
    Obj *o = cars[0].root;
    FxMat keep = o->local;
    uint16_t u[3] = {(uint16_t)a[0], (uint16_t)a[1], (uint16_t)a[2]};
    Obj_SetAngles(o, u);
    const int32_t *m = o->local.m;
    for (int i = 0; i < 3; i++) out[i] = fx_mul(m[8 + i], pt[2]) + fx_mul(m[4 + i], pt[1]) + fx_mul(m[i], pt[0]);   /* Vec_Mul3 0x4b9c20 */
    o->local = keep;
}

/* 0x50f120: the class's part points (0x527420, 0x240 bytes a class: 12 views of 4) on the screen with
   the car turned to each view, two thirds as the scene's view is (0x501690) */
static void view_points(int cls)
{
    if (!cars[0].root) return;
    for (uint32_t v = 0; v < 12; v++) {
        int16_t a[3] = {gms16(0x5273c0 + 8 * v), gms16(0x5273c2 + 8 * v), gms16(0x5273c4 + 8 * v)};
        for (uint32_t k = 0; k < 4; k++) {
            uint32_t at = 0x527420 + (uint32_t)cls * 0x240 + (v * 4 + k) * 0xc;
            int32_t pt[3] = {gms32(at), gms32(at + 4), gms32(at + 8)}, w[3], s[2];
            turned(pt, w, a);
            Obj_ToScreen(s, w, Sv_Camera());
            points[v][k][0] = fx_mul(s[0], 0xaaaa), points[v][k][1] = fx_mul(s[1], 0xaaaa);
        }
    }
}

/* 0x5062d0: player p's car: <name>A1N.c3d with A1L and A1S for the wheels (the classes that have
   them), at lower detail <name>c1.c3d; the textures <name>A1.bfl (c1); its wheels' meshes; the
   light lenses' alpha keys; the class's part points */
static bool car_load(int p)
{
    int id = (int)(gm32(0x531350 + 0x30u * (uint32_t)p) & 0x3f);   /* Player_Car 0x4086b0 */
    int cls = (int)gm32(0x516b40 + 4u * (uint32_t)id);             /* Car_Class 0x40ee90 */
    const char *name = exe_str(exe_u32(0x516bb0 + 4u * (uint32_t)id));
    char base[200], path[220];
    if (Gs_SelBit24()) Path_Format(base, sizeof base, 0x5196f8, Path_Cars(), name, 1);   /* 0x420060 */
    else Path_Format(base, sizeof base, 0x5196f0, Path_Cars(), name);
    snprintf(path, sizeof path, exe_str(0x527370), base);
    if (detail() == 0) {
        memcpy(path + strlen(path) - 6, exe_str(0x51a8e8), 8);
        load_model(p, 0, path);
        if (has_l(cls, 1)) memcpy(path + strlen(path) - 5, exe_str(0x51a8c4), 6), load_model(p, 1, path);
        if (has_s(cls, 6)) memcpy(path + strlen(path) - 5, exe_str(0x51a8cc), 6), load_model(p, 2, path);
    } else
        load_model(p, 0, path);
    snprintf(path, sizeof path, exe_str(0x51a12c), base);
    if (detail() == 0) memcpy(path + strlen(path) - 6, exe_str(0x527368), 7);
    Bfl_Mount(&textures[p], path);
    if (!files[p][0]) return false;
    SvCar *c = &cars[p];
    memset(c, 0, sizeof *c);
    Obj *root = build(p, 0);
    if (!root) return false;
    if (detail() == 0) {
        for (int k = 0; k < 4; k++) {
            Obj *w = Obj_FindType(root, (uint32_t)k + 1);
            c->mesh[k] = w ? w->mesh : NULL;
        }
        if ((c->root_l = build(p, 1))) {
            for (int k = 0; k < 4; k++) {
                Obj *w = Obj_FindType(c->root_l, (uint32_t)k + 1);
                c->mesh_l[k] = w ? w->mesh : NULL;
            }
            mask_tree(c->root_l, 0);
        }
        if ((c->root_s = build(p, 2))) {
            for (int k = 0; k < 4; k++) {
                Obj *w = Obj_FindType(c->root_s, (uint32_t)k + 1);
                c->mesh_s[k] = w ? w->mesh : NULL;
            }
            mask_tree(c->root_s, 0);
        }
    }
    for (int k = 0; k < 4; k++) c->wheel[k] = Obj_FindType(root, (uint32_t)k + 1);
    c->cls = cls;
    c->second = Obj_FindType(root, 5);
    c->root = root;
    PENDING_NOTE(0x507080, "the stand car's body panels for the damage (0x507080, 0x507a10, 0x507650)");
    Obj *lens = Obj_FindType(root, 0xe);
    if (lens && lens->mesh && lens->model) {
        const uint8_t *poly = C3d_Ref(lens->model, lens->mesh + 0x24, lens->model->polys);
        int32_t t = poly ? (int32_t)(poly[4] | poly[5] << 8 | poly[6] << 16 | (uint32_t)poly[7] << 24) : -1;
        if (t >= 0 && t < TEX_MAX && g_tex[t]) {
            Tex_KeyAlpha(g_tex[t], 0xbf, 0, 0x40, 0, 0x80, 0, p);
            Tex_KeyAlpha(g_tex[t], 0xe0, 0, 0xe0, 0, 0xe0, 0, p);
        }
    }
    tyre_shown[p] = -1;
    view_points(cls);
    return true;
}

/* 0x50a3c0: every player's */
void Sv_CarsInit(void)
{
    for (int p = 0; p < (int)Gs_Players() && p < 4; p++) car_load(p);
}

/* 0x506080: the wheels for the set-up's tyres: snow, tarmac or the gravel ones */
static void wheels(int p)
{
    int t = Sv_SetupValue(p, 0);
    if (tyre_shown[p] == t) return;
    tyre_shown[p] = t;
    SvCar *c = &cars[p];
    if (detail() != 0) {
        PENDING_NOTE(0x506080, "the stand car's tyre textures at lower detail (Tex_Reload)");
        return;
    }
    uint8_t **m = has_s(c->cls, t) ? c->mesh_s : has_l(c->cls, t) ? c->mesh_l : c->mesh;
    for (int k = 0; k < 4; k++)
        if (m[k] && c->wheel[k]) c->wheel[k]->mesh = m[k];
}

/* 0x506b20: player p's car shown (its wheels for the tyres) or hidden */
void Sv_CarShow(int p, bool on)
{
    SvCar *c = &cars[p];
    if (!c->root) return;
    mask_tree(c->root, on ? 0xff : 0);
    mask_tree(c->second, on ? 0xff : 0);
    if (!on) return;
    PENDING_NOTE(0x509150, "the stand car's body panels shown");
    wheels(p);
}

/* 0x506930: the car turning to view v over 0.2 s (or there at once); past half a turn the other way
   round as the original (its positive case comes out positive) */
void Sv_CarView(int p, int v, bool now)
{
    turn_len = 0x14;
    turn_at[p] = Timer_Centis();
    turning[p] = true;
    memcpy(ang_from[p], ang[p], sizeof ang[p]);
    for (uint32_t i = 0; i < 3; i++) ang_to[p][i] = gms16(0x5273c0 + 8u * (uint32_t)v + 2 * i);
    if (now) memcpy(ang[p], ang_to[p], sizeof ang[p]), memcpy(ang_from[p], ang_to[p], sizeof ang[p]);
    double k;
    memcpy(&k, exe_data(0x511300, 8), 8);
    for (int i = 0; i < 3; i++) {
        int32_t d = (ang_to[p][i] - ang_from[p][i]) * 0x1680;
        int32_t a = d < 0 ? -d : d;
        if (a > 0xb40000) d = d < 1 ? d + 0x1680000 : -d + 0x1680000;
        ang_d[p][i] = (int16_t)X87_MulRound((double)d, k);
    }
}

/* 0x5009c0: the lines' ends towards view v's points (or there at once) */
void Sv_CarLines(int p, int v, bool now)
{
    for (int k = 0; k < 4; k++)
        for (int j = 0; j < 2; j++) {
            line_from[p][k][j] = line[p][k][j];
            line_to[p][k][j] = points[v][k][j];
            if (now) line[p][k][j] = points[v][k][j], line_from[p][k][j] = line_to[p][k][j];
            line_d[p][k][j] = line_to[p][k][j] - line_from[p][k][j];
        }
}

/* 0x500920: the lines k percent of the way, or at their ends */
static void lines_step(int p, bool done, int k)
{
    if (done) {
        memcpy(line[p], line_to[p], sizeof line[p]);
        return;
    }
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 2; j++) line[p][i][j] = line_d[p][i][j] * k / 100 + line_from[p][i][j];
}

/* 0x509dc0: the wheels' turns: the right side's half a turn round, the width at 0.6 with more than
   one player or the snow wheels at detail 2 (the spin the original adds up here is not used) */
static void wheel_turns(int p)
{
    SvCar *c = &cars[p];
    for (int k = 0; k < 4; k++) {
        Obj *w = c->wheel[k];
        if (!w) continue;
        int32_t *m = w->local.m;
        int32_t s = (k & 1) ? -0x10000 : 0x10000;
        m[0] = s, m[1] = 0, m[2] = 0;
        m[4] = 0, m[5] = 0x10000, m[6] = 0;
        m[8] = 0, m[9] = 0, m[10] = s;
        if (Gs_Players() > 1 || (has_s(c->cls, Sv_SetupValue(p, 0)) && detail() == 2))
            for (int i = 8; i < 11; i++) m[i] = fx_mul(m[i], 0x9999);
        w->dirty = 1;
    }
}

/* 0x506720: each car turning to its view (squared), its lines following */
void Sv_CarTurn(void)
{
    uint32_t now = Timer_Centis();
    for (int p = 0; p < (int)Gs_Players() && p < 4; p++) {
        if (turning[p] && turn_len < now - turn_at[p]) turning[p] = false;
        int k = 0;
        if (!turning[p]) memcpy(ang[p], ang_to[p], sizeof ang[p]);
        else {
            uint32_t t = (now * 100 - turn_at[p] * 100) / turn_len;
            k = (int)(t * t) / 100;
            for (int i = 0; i < 3; i++) ang[p][i] = (int16_t)(ang_d[p][i] * k / 100 + ang_from[p][i]);
        }
        lines_step(p, !turning[p], k);
        if (cars[p].root) {
            uint16_t u[3] = {(uint16_t)ang[p][0], (uint16_t)ang[p][1], (uint16_t)ang[p][2]};
            Obj_SetAngles(cars[p].root, u);
        }
        wheel_turns(p);
        PENDING_NOTE(0x5091c0, "the stand car's suspension and parts by the set-up");
    }
}

/* 0x500550: a line down from each of the part's four points to y 245, a circle on each (alternate ones
   on layer 4), the line joining their feet and from its middle to the panel (at y 276 to x 229 when
   the middle is right of 228, else down to 255) */
void Sv_PartLines(int p, Tex *circle)
{
    int sw, sh;
    D3d_ScreenSize(&sw, &sh);
    const uint8_t *c = gm(0x526ffc);
    int y = sh * 0xf5 / 0x1e0, lo = 0, hi = 0;
    for (int i = 0; i < 4; i++) {
        int16_t r[4] = {(int16_t)(line[p][i][0] >> 16), (int16_t)(line[p][i][1] >> 16), 1, 0};
        r[3] = (int16_t)((int16_t)y - r[1]);
        int layer = (i & 1) == 0 ? 4 : 1;
        Rect_Fill(r, c, layer);
        int x = r[0];
        if (i == 0) lo = hi = x;
        else {
            int v = x;
            if (lo <= x) {
                v = lo;
                if (hi < x) hi = x;
            }
            lo = v;
        }
        if (circle) {
            Rect16 src = {0, 0, circle->width, circle->height}, dst = {(int16_t)(r[0] - 3), (int16_t)(r[1] - 3), circle->width, circle->height};
            Sprite_Add(&src, &dst, circle, layer, 0, NULL, NULL, c, 8);
        }
    }
    if (hi != lo) {
        int16_t r[4] = {(int16_t)lo, (int16_t)y, (int16_t)(hi - lo + 1), 1};
        Rect_Fill(r, c, 1);
    }
    int mid = (hi - lo) / 2 + lo;
    if (sw * 0xe4 / 0x280 < mid) {
        int16_t r[4] = {(int16_t)mid, (int16_t)y, 1, (int16_t)((int16_t)(sh * 0x114 / 0x1e0) - (int16_t)y)};
        Rect_Fill(r, c, 1);
        int16_t x0 = (int16_t)(sw * 0xe5 / 0x280);
        int16_t h[4] = {x0, (int16_t)(sh * 0x114 / 0x1e0), (int16_t)(mid - x0 + 1), 1};
        Rect_Fill(h, c, 1);
        return;
    }
    int16_t r[4] = {(int16_t)mid, (int16_t)y, 1, (int16_t)((int16_t)(sh * 0xff / 0x1e0) - (int16_t)y)};
    Rect_Fill(r, c, 1);
}

/* 0x505f10 (the at-exit clean-up): the files and archives */
void Sv_Free(void)
{
    for (int p = 0; p < 4; p++) {
        for (int k = 0; k < 3; k++) free(files[p][k]), files[p][k] = NULL;
        Bfl_Unmount(&textures[p]);
        memset(&cars[p], 0, sizeof cars[p]);
    }
}
