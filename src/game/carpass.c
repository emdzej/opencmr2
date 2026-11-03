/* The cars' passes (see carpass.h). */
#include "game/carpass.h"
#include "game/camera.h"
#include "game/cockpit.h"
#include "game/gstate.h"
#include "game/hud.h"
#include "game/race.h"
#include "net/records.h"
#include "render/d3d7.h"
#include "fixmath.h"
#include "game/mem.h"
#include "sim/sim.h"
#include "c3d.h"
#include "sim/car.h"
#include <math.h>
#include <stdlib.h>

#define VIEW_KIND(car, view) (0x588cd4u + 4u * (uint32_t)((view) + (car) * 2))

/* 0x4add90 / 0x4addd0: the pass mask of every mesh of a tree (the nodes keep theirs) */
static void mask_list(Obj *o, uint8_t m);
static void mask_tree(Obj *o, uint8_t m)
{
    if (!o) return;
    if (o->kind == 0) o->mask = m;
    if (o->child) mask_list(o->child, m);
}
static void mask_list(Obj *o, uint8_t m)
{
    for (; o; o = o->next) mask_tree(o, m);
}

void Obj_MaskTree(Obj *o, uint8_t m) { mask_tree(o, m); }   /* 0x4addd0 */

/* 0x46b6b0 / 0x46b6e0: a mesh whose first polygon's alpha (+0x37) is at most a hidden from every pass */
static void hide_clear(Obj *o, uint8_t a)
{
    if (o->kind != 0 || !o->mesh || !o->model) return;
    const uint8_t *p = C3d_Ref(o->model, o->mesh + 0x24, o->model->polys);
    if (p && p[0x37] <= a) o->mask = 0;
}
static void hide_clear_list(Obj *o, uint8_t a)
{
    for (; o; o = o->next) {
        hide_clear(o, a);
        if (o->child) hide_clear_list(o->child, a);
    }
}

/* 0x46bb40 (Race_DrawView, before the cars): per car and view the kind of drawing (0x588cd4): the
   view's own car by its camera (inside 6, 5 the bonnet, 8 the cockpit, else 1), outside the car 1;
   1 becomes 2-4 by the far model (0x588bb4, set by 0x428bf0) and the second model's mesh flag bit 18;
   another car with two players and no contact between the cars is 7, not drawn. In the network game
   the car of a player who has left (the record no longer in use, its car still loaded: 0x409cb0,
   0x40b1e0) is 7 in both views. */
void Cars_ViewKinds(void)
{
    int own[2];
    for (int v = 0; v < 2; v++) {
        switch (Camera_Kind(v)) {
        case 1: own[v] = 6; break;
        case 2: own[v] = 5; break;
        case 3: own[v] = 8; break;
        case 4: case 5: case 6: case 7: case 8: case 9: case 10: own[v] = 1; break;
        default: own[v] = 0; break;
        }
    }
    for (int k = g_all.n - 1; k >= 0; k--) {
        const Car *c = Car_Get(g_all.car[k]);
        const uint8_t *sm = c->second ? c->second->mesh : NULL;
        uint32_t lit = sm ? (uint32_t)(sm[0x30] | sm[0x31] << 8 | sm[0x32] << 16 | (uint32_t)sm[0x33] << 24) >> 0x12 & 1 : 0;
        int n = CI8(c, 0xb1a);
        bool far = gm32(0x588bb4 + 4u * (uint32_t)n) != 0;
        for (int v = 0; v < 2; v++) {
            int kind;
            if ((Camera_Car(v) & 0xff) == n) {
                kind = own[v];
                if (kind == 1) kind = !far ? (lit ? 2 : 1) : lit ? 4 : 3;
            } else if (Gs_SelBits14() < 2 || Gs_CarsCollide())
                kind = !far ? (lit ? 2 : 1) : lit ? 4 : 3;
            else
                kind = 7;
            gw32(VIEW_KIND(n, v), (uint32_t)kind);
        }
    }
    if (Gs_ModeBit19())   /* 0x405e00 */
        for (int i = 0; i < 7; i++)
            if (!NetRec_Used(i) && NetRec_CarLoaded(i))
                for (int v = 0; v < 2; v++) gw32(VIEW_KIND((int)NetRec_Slot(i), v), 7);   /* 0x40b010 */
}

/* 0x46b790: a view's bit into the groups its kind draws: the lamps' and the cockpit's objects (+0),
   the wheels (+1), the second model (+2), the cockpit (+3); the far models (+4), the type 6 and
   type 0xe objects (+5, +6); the car shown in the view (0x588ba4) */
static uint8_t grp[7], shown[CAR_MAX];   /* 0x588bac-0x588bb2, 0x588ba4 */
static void add_view(int kind, int v, int car)
{
    uint8_t b = (uint8_t)(1u << v);
    switch (kind) {
    case 0: grp[0] |= b, grp[1] |= b, grp[2] |= b, grp[3] |= b; /* fall through */
    case 3: grp[4] |= b, grp[5] |= b, grp[6] |= b; break;
    case 1: grp[0] |= b, grp[1] |= b, grp[2] |= b, grp[5] |= b, grp[6] |= b; break;
    case 2: grp[0] |= b, grp[1] |= b, grp[2] |= b; return;
    case 4: grp[4] |= b; return;
    case 5: grp[0] |= b; break;
    case 8: grp[0] |= b, grp[3] |= b; return;
    default: return;
    }
    shown[car] |= b;
}

/* 0x46b8f0 (Race_DrawView, per car): the masks of the car's objects from its two views' kinds; the
   main model's own always every pass; the meshes nearly clear (alpha 10 or less) of both models never
   drawn. The far model's body and wheels (+0x724-+0x734) take the far group, its type 6 and 0xe
   objects those groups. */
void Car_Masks(const Car *c)
{
    int n = CI8(c, 0xb1a);
    for (int i = 0; i < 7; i++) grp[i] = 0;
    if (n >= 0 && n < CAR_MAX) shown[n] = 0;
    add_view((int)gm32(VIEW_KIND(n, 0)), 0, n);
    add_view((int)gm32(VIEW_KIND(n, 1)), 1, n);
    if (c->main) c->main->mask = 0xff;
    for (int w = 0; w < 4; w++) mask_tree(c->wheel[w], grp[1]);
    mask_tree(c->second, grp[2]);
    Obj *o = Obj_FindType(c->second, 6);
    if (o) o->mask = grp[5];
    o = Obj_FindType(c->second, 0xe);
    if (o) o->mask = grp[6];
    if (c->far[0]) {
        mask_tree(c->far[0], grp[4]);
        if ((o = Obj_FindType(c->far[0], 6))) o->mask = grp[5];
        if ((o = Obj_FindType(c->far[0], 0xe))) o->mask = grp[6];
    }
    for (int w = 1; w < 5; w++)
        if (c->far[w]) mask_tree(c->far[w], grp[4]);
    Cockpit_Masks(n, grp[3], grp[0]);   /* 0x58d49c's tree, the 0x58d340 and 0x58d47c objects */
    if (c->main) hide_clear(c->main, 10), hide_clear_list(c->main->child, 10);
    if (c->second) hide_clear(c->second, 10), hide_clear_list(c->second->child, 10);
}

uint8_t Car_Shown(int n) { return n >= 0 && n < CAR_MAX ? shown[n] : 0; }   /* 0x46bd40 */

/* 0x46b760: a car near (its full models) or far (0x588bb4) */
static void set_near(int car, bool near) { gw32(0x588bb4 + 4u * (uint32_t)car, near ? 0 : 1); }

/* 0x428bf0 (Race_DrawView, for the view's rect v): which cars get their full models. Not with option 16;
   the split screen of selection bit 26 both players' cars; otherwise only with bit 26 or in modes 8-12.
   Each listed car's squared distance from the camera (any axis beyond 150 units: out); but the first
   listed, a car whose point or the point its radius (+0x758) along the camera's up axis is off the
   screen, or whose circle (that radius's size on the screen, by a 640 x 480 measure times 75) misses
   the view, out as well. Then three times: car 0 when it is not picked yet (the nearest found before
   it in the list still taken), else the nearest left within 60 units. The camera's world matrix is its
   local one meanwhile (the original swaps them). */
void Cars_Lod(int p, const int16_t v[4])
{
    if (gm32(0x52e890) >> 0x10 & 1) return;   /* 0x406410 */
    if (Gs_SelBit26() && Hud_SplitScreen()) {
        set_near(0, true), set_near(1, true);
        return;
    }
    uint32_t mode = Gs_Mode();
    if (!Gs_SelBit26() && !(mode >= 8 && mode <= 12)) return;
    Obj *cam = Race_Camera(p);
    if (!cam) return;
    int n = g_all.n;
    uint8_t pick[CAR_MAX] = {0};
    int32_t d2[CAR_MAX] = {0};
    const int32_t *cp = &cam->local.m[12];
    for (int k = 0; k < n; k++) {
        int car = g_all.car[k];
        const int32_t *q = &Car_Get(car)->main->local.m[12];
        int32_t dx = cp[0] - q[0], dy = cp[1] - q[1], dz = cp[2] - q[2];
        if (abs(dx) > 0x960000 || abs(dy) > 0x960000 || abs(dz) > 0x960000) d2[car] = 0x7fbc0000;
        else d2[car] = (int32_t)((uint32_t)fx_mul(dz, dz) + (uint32_t)fx_mul(dx, dx) + (uint32_t)fx_mul(dy, dy));
    }
    int32_t up[3] = {cam->local.m[4], cam->local.m[5], cam->local.m[6]};   /* 0x4bac80 */
    uint32_t len = Fx_Sqrt((uint32_t)fx_mul(up[0], up[0]) + (uint32_t)fx_mul(up[1], up[1]) + (uint32_t)fx_mul(up[2], up[2]));
    if (!len) up[0] = up[1] = up[2] = 0;
    else {
        int32_t inv = (int32_t)(0x100000000LL / (int32_t)len);
        for (int i = 0; i < 3; i++) up[i] = fx_mul(up[i], inv);
    }
    FxMat keep = cam->world;
    cam->world = cam->local;
    int sw, sh;
    D3d_ScreenSize(&sw, &sh);
    for (int k = 1; k < n; k++) {
        int car = g_all.car[k];
        const Car *c = Car_Get(car);
        const int32_t *q = &c->main->local.m[12];
        int32_t a[2], b[2], r = CI32(c, 0x758);
        Obj_ToScreen(a, q, cam);
        int32_t e[3] = {fx_mul(up[0], r) + q[0], fx_mul(up[1], r) + q[1], fx_mul(up[2], r) + q[2]};
        Obj_ToScreen(b, e, cam);
        a[0] >>= 16, a[1] >>= 16, b[0] >>= 16, b[1] >>= 16;
        if ((a[0] == -100 && a[1] == -100) || (b[0] == -100 && b[1] == -100)) {
            d2[car] = 0x7fbc0000;
            continue;
        }
        int32_t fx = (int32_t)lrint((double)(a[0] - b[0]) / (double)sw * 65536.0);
        int32_t fy = (int32_t)lrint((double)(a[1] - b[1]) / (double)sh * 65536.0);
        int32_t ux = fx_mul(fx_mul(fx, 0x2800000), 0x28f), uy = fx_mul(fx_mul(fy, 0x1e00000), 0x28f);
        int32_t size = fx_mul((int32_t)Fx_Sqrt((uint32_t)fx_mul(uy, uy) + (uint32_t)fx_mul(ux, ux)), 0x4b0000) >> 16;
        int16_t rx = (int16_t)(sw * size / 0x280), ry = (int16_t)(sh * size / 0x1e0);
        if (a[0] < (int16_t)(v[0] - rx) || (int16_t)(v[2] + v[0] + rx) < a[0] || a[1] < (int16_t)(v[1] - ry) ||
            (int16_t)(v[3] + v[1] + ry) < a[1])
            d2[car] = 0x7fbc0000;
    }
    cam->world = keep;
    for (int t = 0; t < 3; t++) {
        int best = -1;
        int32_t bd = 0x7fff0000;
        for (int k = 0; k < n; k++) {
            int car = g_all.car[k];
            if (pick[car]) continue;
            if (car == 0) {
                pick[car] = 2;
                break;
            }
            if (d2[car] < bd) best = car, bd = d2[car];
        }
        if (best != -1) {
            if (bd > 0xe100000) break;
            pick[best] = 2;
        }
    }
    for (int k = 0; k < n; k++) set_near(g_all.car[k], pick[g_all.car[k]] == 2);
}
