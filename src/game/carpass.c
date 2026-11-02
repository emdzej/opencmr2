/* The cars' passes (see carpass.h). */
#include "game/carpass.h"
#include "game/camera.h"
#include "game/cockpit.h"
#include "game/gstate.h"
#include "game/mem.h"
#include "sim/sim.h"
#include "c3d.h"
#include "pending.h"
#include "sim/car.h"

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
   another car with two players and no contact between the cars is 7, not drawn. (The network game's
   ghosts, 0x40b1e0, not ported.) */
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
    if (Gs_ModeBit19()) PENDING_NOTE(0x46bb40, "the network game's ghosts (0x40b1e0)");
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
   drawn. The far models (+0x724-+0x734) are not loaded by the port. */
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
    Cockpit_Masks(n, grp[3], grp[0]);   /* 0x58d49c's tree, the 0x58d340 and 0x58d47c objects */
    if (c->main) hide_clear(c->main, 10), hide_clear_list(c->main->child, 10);
    if (c->second) hide_clear(c->second, 10), hide_clear_list(c->second->child, 10);
}

uint8_t Car_Shown(int n) { return n >= 0 && n < CAR_MAX ? shown[n] : 0; }   /* 0x46bd40 */
