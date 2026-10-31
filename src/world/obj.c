/* Scene objects (see obj.h). */
#include "world/obj.h"
#include "render/mesh.h"
#include "world/section.h"
#include <stdlib.h>
#include <string.h>

Obj *g_objs[OBJ_MAX];
int g_nobjs;
static Obj *blocks[256];   /* the port's allocations, one per model */
static int nblocks;
static FxMat view_fixed;   /* PTR_DAT_00520b78 + 0x2424 */
static int32_t view_hold;  /* DAT_006838d0: View_Set works while this is below 5 (0 so far) */

static int32_t rd32(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static int16_t rd16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

static void read_mat(FxMat *m, const uint8_t *p)
{
    for (int i = 0; i < 16; i++) m->m[i] = rd32(p + 4 * i);
}

/* An appended child: the parent's first child, or the last of its first child's siblings. A root that
   still has siblings of its own brings them along (its next link is kept). */
void Obj_Attach(Obj *o, Obj *parent)
{
    if (!parent) {
        Mat_Identity(&o->world);
        Mat_Identity(&o->local);
        return;
    }
    Obj *c = parent->child;
    if (!c) {
        parent->child = o;
        o->parent = parent;
        return;
    }
    while (c->next) c = c->next;
    c->next = o;
    o->parent = parent;
}

Obj *C3d_Objects(const C3d *m, Obj *parent)
{
    if (!m->nobj || nblocks == 256 || g_nobjs + m->nobj > OBJ_MAX) return NULL;
    Obj *o = calloc(m->nobj, sizeof *o);
    if (!o) return NULL;
    blocks[nblocks++] = o;
    const uint8_t *base = m->data + m->objects;
#define LINK(f) (rd32(r + (f)) == -1 ? NULL : &o[rd32(r + (f)) / C3D_OBJECT])
    for (int i = 0; i < m->nobj; i++) {
        const uint8_t *r = base + (size_t)i * C3D_OBJECT;
        Obj *x = &o[i];
        x->rec = r;
        x->model = m;
        x->next = LINK(0);
        x->child = LINK(4);
        x->mesh = (uint8_t *)C3d_Ref(m, r + 0x0c, m->meshes);
        x->section = rd16(r + 0x24);
        for (int k = 0; k < 3; k++) x->near[k] = rd16(r + 0x28 + 2 * k);
        x->type = (uint32_t)rd32(r + 0x30);
        read_mat(&x->rest, r + 0x58);
        read_mat(&x->local, r + 0x98);
        read_mat(&x->world, r + 0xd8);
        x->dirty = rd32(r + 0x174);
        x->kind = rd32(r + 0x178);
        x->mask = r[0x17c];
        x->sorted = rd32(r + 0x180);
        x->inherit = rd32(r + 0x184);
        if (!parent || x->section != -1) {
            x->parent = LINK(8);
            x->section = -1;
        } else {
            Obj_Attach(x, parent);
        }
        x->slot = (int16_t)(g_nobjs + i);
        g_objs[g_nobjs + i] = x;
    }
#undef LINK
    g_nobjs += m->nobj;
    return o;
}

void Obj_FreeAll(void)
{
    for (int i = 0; i < nblocks; i++) free(blocks[i]);
    nblocks = 0;
    memset(g_objs, 0, sizeof g_objs);
    g_nobjs = 0;
}

/* Objects in a stage section with +0x180 set and type bit 12 clear ask the section (FUN_004b7da0, step
   5); all others are active. */
bool Obj_Active(const Obj *o)
{
    (void)o;   /* the section test (o->section >= 0, o->sorted, !(o->type & 0x1000)) comes with step 5 */
    return true;
}

#include "world/section.h"

/* Only objects with a parent and a kind of 0-3 are updated: a root keeps its world matrix as given. A
   parent of type 0xfd takes the child's local matrix as its world, an inheriting child takes its
   parent's; the rest multiply. The object and all its ancestors are then marked dirty. */
void Obj_UpdateTree(Obj *o)
{
    for (; o; o = o->next) {
        if (!Obj_Active(o)) continue;
        Obj *p = o->parent;
        if (p && o->kind >= 0 && o->kind < 4 && (o->dirty == 1 || p->dirty == 1)) {
            if ((p->type & 0xff) == 0xfd) o->world = o->local;
            else if (o->inherit) o->world = p->world;
            else Mat_Mul(&o->world, &o->local, &p->world);
            if (o->section != -1 && (o->type & 0xff) != 0xfd) Obj_UpdateSection(o);
            for (Obj *a = o; a; a = a->parent) a->dirty = 1;
        }
        if (o->child) Obj_UpdateTree(o->child);
    }
}

static void to_d3d(D3DMATRIX *d, const FxMat *m)
{
    float f[16];
    Mat_ToFloat(f, m);
    memcpy(d->m, f, sizeof f);
}

/* Kinds 0 and 3: the float world matrix; kind 1: a point or spot light placed at the world position (a
   spot pointing along the world x axis) and set, a directional one left as it is; kind 2 nothing. */
void Obj_Commit(Obj *o)
{
    for (; o; o = o->next) {
        if (!Obj_Active(o)) continue;
        if (o->kind == 0 || o->kind == 3) {
            if (o->dirty == 1) {
                to_d3d(&o->worldf, &o->world);
                o->dirty = 0;
            }
        } else if (o->kind == 1 && o->dirty == 1) {
            D3DLIGHT7 *l = o->light;
            if (l && (l->type == D3DLIGHT_POINT || l->type == D3DLIGHT_SPOT)) {
                float f[16];
                Mat_ToFloat(f, &o->world);
                if (l->type == D3DLIGHT_SPOT) l->direction = (D3DVECTOR){f[0], f[1], f[2]};
                l->position = (D3DVECTOR){f[12], f[13], f[14]};
                D3d_SetLight(o->light_index, l);
            }
            o->dirty = 0;
        }
        if (o->child) Obj_Commit(o->child);
    }
}

/* The pass bit is a byte (passes 0-7). Children are only walked under kind-0 objects. */
void Obj_DrawList(Obj *o, uint32_t pass)
{
    uint8_t bit = (uint8_t)(1u << (pass & 31));
    for (; o; o = o->next) {
        if (o->kind != 0 || !(bit & o->mask)) continue;
        if (o->mesh) {
            D3d_SetTransform(D3DTS_WORLD, &o->worldf);
            Mesh_Draw(o->model, o->mesh, 0, 0, 0);
        }
        if (o->child) Obj_DrawList(o->child, pass);
    }
}

/* One object (any kind for its children: a kind-3 root draws what hangs under it). */
void Obj_Draw(Obj *o, uint32_t pass)
{
    uint8_t bit = (uint8_t)(1u << (pass & 31));
    if (o->kind == 0 && (o->mask & bit) && o->mesh) {
        D3d_SetTransform(D3DTS_WORLD, &o->worldf);
        Mesh_Draw(o->model, o->mesh, 0, 0, 0);
    }
    if ((bit & o->mask) && o->child) Obj_DrawList(o->child, pass);
}

/* Obj_SetPosition 0x4ac480: the local translation (and for kinds 1 and 2 the fields at +0x10), the
   inherit flag off, the object and its ancestors dirty. */
void Obj_SetPosition(Obj *o, const int32_t pos[3])
{
    if (!o) return;
    o->local.m[12] = pos[0];
    o->local.m[13] = pos[1];
    o->local.m[14] = pos[2];
    o->inherit = 0;
    for (; o; o = o->parent) o->dirty = 1;
}

/* One step of 0x4ac820: rows r1 and r2 of the 3 x 3 turned by angle a about row ax (the axis-angle
   matrix with the sine of -a), then each made unit by the table square root. */
static void turn(int32_t *m, int ax, int r1, int r2, uint16_t a)
{
    int32_t s = g_sin[-(uint32_t)a & 0xfff], c = g_sin[(uint16_t)(0x400 - a) & 0xfff], k = 0x10000 - c;
    int32_t x = m[4 * ax], y = m[4 * ax + 1], z = m[4 * ax + 2];
    int32_t xx = fx_mul(x, x), yy = fx_mul(y, y), zz = fx_mul(z, z);
    int32_t xy = fx_mul(fx_mul(x, y), k), xz = fx_mul(fx_mul(x, z), k), yz = fx_mul(fx_mul(z, y), k);
    int32_t r[9] = {
        fx_mul(c, 0x10000 - xx) + xx, fx_mul(z, s) + xy, xz - fx_mul(y, s),
        xy - fx_mul(z, s), fx_mul(c, 0x10000 - yy) + yy, fx_mul(x, s) + yz,
        fx_mul(y, s) + xz, yz - fx_mul(x, s), fx_mul(c, 0x10000 - zz) + zz,
    };
    int rows[2] = {r1, r2};
    for (int i = 0; i < 2; i++) {
        int32_t *v = m + 4 * rows[i], o[3];
        for (int j = 0; j < 3; j++) o[j] = fx_mul(r[3 * j], v[0]) + fx_mul(r[3 * j + 1], v[1]) + fx_mul(r[3 * j + 2], v[2]);
        uint32_t l = Fx_Sqrt((uint32_t)fx_mul(o[0], o[0]) + (uint32_t)fx_mul(o[1], o[1]) + (uint32_t)fx_mul(o[2], o[2]));
        int32_t inv = l ? (int32_t)(0x100000000LL / (int32_t)l) : 0;
        for (int j = 0; j < 3; j++) v[j] = l ? fx_mul(o[j], inv) : 0;
    }
}

/* 0x4ac820: angle [1] about the local y axis (rows 0, 2), [2] about z (rows 0, 1), [0] about x
   (rows 1, 2), each about the row as already turned; then the move added and the ancestors dirty. */
void Obj_Rotate(Obj *o, const int32_t move[3], const uint16_t a[3])
{
    int32_t *m = o->local.m;
    if (a[1]) turn(m, 1, 0, 2, a[1]);
    if (a[2]) turn(m, 2, 0, 1, a[2]);
    if (a[0]) turn(m, 0, 1, 2, a[0]);
    for (int i = 0; i < 3; i++) m[12 + i] += move[i];
    for (Obj *p = o; p; p = p->parent) p->dirty = 1;
}

/* 0x4ac4f0: kinds 0-3 get the rest matrix's 3 x 3 back and the turn; any kind loses inherit and is
   marked dirty with its ancestors. */
void Obj_SetAngles(Obj *o, const uint16_t a[3])
{
    if (!o) return;
    if (o->kind >= 0 && o->kind <= 3) {
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) o->local.m[4 * r + c] = o->rest.m[4 * r + c];
        static const int32_t zero[3];
        Obj_Rotate(o, zero, a);
    }
    o->inherit = 0;
    for (; o; o = o->parent) o->dirty = 1;
}

void View_Set(Obj *cam)
{
    if (view_hold >= 5 || !cam->dirty) return;
    Mat_Inverse(&view_fixed, &cam->world);
    D3DMATRIX v;
    to_d3d(&v, &view_fixed);
    D3d_SetTransform(D3DTS_VIEW, &v);
}

void Obj_Frame(Obj *list, Obj *camera)
{
    Obj_UpdateTree(list);
    Obj_Commit(list);
    if (camera) View_Set(camera);
}
