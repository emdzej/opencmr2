/* The stage's sections (see section.h). Not yet: the second-kind meshes' lists (+0x14), the 0x30-byte
   records' lists (+0x28), the edge heights (FUN_004b8b90) and the billboard and overlay draws. */
#include "world/section.h"
#include "render/d3d7.h"
#include "render/mesh.h"
#include "render/view.h"
#include <math.h>
#include <string.h>

Section g_sec[SEC_MAX];
int g_nsec, g_sec_row, g_sec_rows;
int32_t g_sec_size, g_sec_half;
uint16_t g_visible[SEC_MAX];
int g_nvisible;

static const C3d *model;                 /* the stage mesh the sections belong to */
static int32_t recip;                    /* DAT_0072d55c: 2^32 / size */
static int32_t xmin, zmax;               /* DAT_006ef5f0, DAT_006ef5f4 */
static uint32_t vis_mask[SEC_MAX / 32];  /* 0x72d258 */
static bool all_visible;                 /* DAT_0072d578 */

static int32_t rd32(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }

void Section_Register(const C3d *m)
{
    model = m;
    g_nsec = m->nsec < SEC_MAX ? m->nsec : SEC_MAX;
    memset(g_sec, 0, sizeof g_sec);
    for (int i = 0; i < g_nsec; i++) {
        Section *s = &g_sec[i];
        s->rec = m->data + m->sections + (size_t)i * C3D_SECTION;
        s->x = rd32(s->rec);
        s->y = rd32(s->rec + 4);
        s->z = rd32(s->rec + 8);
        s->mesh = (uint8_t *)C3d_Ref(m, s->rec + 0x10, m->meshes);
    }
}

int Section_Find(const int32_t p[3])
{
    int32_t dz = p[2] - g_sec_half - zmax;
    if (dz < 0) dz = g_sec_half - p[2] + zmax;
    int32_t dx = p[0] - xmin + g_sec_half;
    if (dx < 0) dx = xmin - p[0] - g_sec_half;
    int i = g_sec_row * (fx_mul(dz, recip) >> 16) + (fx_mul(dx, recip) >> 16);
    if ((int16_t)i < 0 || (int16_t)i >= (int16_t)g_nsec) i = 0;
    return (int16_t)i;
}

bool Section_Visible(int i)
{
    if (all_visible || g_nvisible == 0) return true;
    return (vis_mask[(i >> 5) & (SEC_MAX / 32 - 1)] >> (i & 31) & 1) != 0;
}

/* The scenery under the world root (its children, by their local translation) into their cells; type
   byte 0xff stays out. */
static void link_objects(Obj *world)
{
    for (int i = 0; i < g_nobjs; i++)
        if (g_objs[i]) g_objs[i]->section_next = NULL;
    for (int i = 0; i < g_nsec; i++) {
        g_sec[i].objs = NULL;
        g_sec[i].nobjs = 0;
    }
    for (Obj *o = world ? world->child : NULL; o; o = o->next) {
        if ((o->type & 0xff) == 0xff) continue;
        int32_t pos[3] = {o->local.m[12], o->local.m[13], o->local.m[14]};
        int i = Section_Find(pos);
        o->section = (int16_t)i;
        Obj **tail = &g_sec[i].objs;
        while (*tail) tail = &(*tail)->section_next;
        *tail = o;
        g_sec[i].nobjs++;
    }
}

/* The mesh's bounding rectangle in whole units (each vertex rounded as it is compared), in 16.16; a cell
   without a mesh takes its square. Corners: (min x, max z), (max x, max z), (max x, min z), (min x,
   min z). */
static void corners(void)
{
    for (int i = 0; i < g_nsec; i++) {
        Section *s = &g_sec[i];
        int32_t x0, x1, z0, z1;
        const uint8_t *part = s->mesh ? s->mesh + (size_t)s->mesh[0x112] * 0x108 : NULL;
        const uint8_t *v = part ? C3d_Ref(model, part + 0x0c, model->verts) : NULL;
        if (!v) {
            s->corner[0] = s->x - g_sec_half, s->corner[1] = s->z - g_sec_half;
            s->corner[2] = g_sec_half + s->x, s->corner[3] = s->z - g_sec_half;
            s->corner[6] = s->x - g_sec_half, s->corner[7] = s->z + g_sec_half;
            s->corner[4] = g_sec_half + s->x, s->corner[5] = s->z + g_sec_half;
            continue;
        }
        float f[3];
        memcpy(f, v, 12);
        x0 = x1 = (int32_t)lrintf(f[0]);
        z0 = z1 = (int32_t)lrintf(f[2]);
        int32_t n = rd32(part + 0x10);
        for (int32_t k = 1; k < n; k++) {
            memcpy(f, v + (size_t)k * C3D_VERTEX, 12);
            if (f[0] < (float)x0) x0 = (int32_t)lrintf(f[0]);
            if ((float)x1 < f[0]) x1 = (int32_t)lrintf(f[0]);
            if (f[2] < (float)z0) z0 = (int32_t)lrintf(f[2]);
            if ((float)z1 < f[2]) z1 = (int32_t)lrintf(f[2]);
        }
        x0 = (int32_t)nearbyint((double)x0 * 65536.0);
        x1 = (int32_t)nearbyint((double)x1 * 65536.0);
        z0 = (int32_t)nearbyint((double)z0 * 65536.0);
        z1 = (int32_t)nearbyint((double)z1 * 65536.0);
        s->corner[0] = x0, s->corner[1] = z1;
        s->corner[2] = x1, s->corner[3] = z1;
        s->corner[4] = x1, s->corner[5] = z0;
        s->corner[6] = x0, s->corner[7] = z0;
    }
}

void Section_Init(Obj *world)
{
    if (g_nsec < 2) return;
    int32_t d = g_sec[1].x - g_sec[0].x;
    g_sec_size = d < 0 ? -d : d;
    g_sec_half = fx_mul(g_sec_size, 0x8000);
    recip = (int32_t)(0x100000000LL / g_sec_size);
    xmin = g_sec[0].x;
    zmax = g_sec[0].z;
    for (int i = 1; i < g_nsec; i++) {
        if (g_sec[i].x < xmin) xmin = g_sec[i].x;
        if (zmax < g_sec[i].z) zmax = g_sec[i].z;
    }
    /* the same bounds half a cell further out are kept at 0x6ed5e8 / 0x6ed5ec (their readers: open) */
    g_sec_row = 1;
    if (g_sec[0].z == g_sec[1].z) {
        bool same;
        do {
            if (g_nsec - 1 <= g_sec_row) break;
            same = g_sec[g_sec_row].z == g_sec[g_sec_row + 1].z;
            g_sec_row++;
        } while (same);
    }
    g_sec_rows = g_nsec / g_sec_row;
    link_objects(world);
    corners();
}

/* Point_InTriangle 0x49da50: a bounding test, then the three edge sides with the coordinates scaled by
   0x28f (0.01) so that the products stay in range. t: three x, z pairs. */
static bool in_triangle(const int32_t p[2], const int32_t t[6])
{
    if (!((t[0] <= p[0] || t[2] <= p[0] || t[4] <= p[0]) && (p[0] <= t[0] || p[0] <= t[2] || p[0] <= t[4])))
        return false;
    if (!((t[1] <= p[1] || t[3] <= p[1] || t[5] <= p[1]) && (p[1] <= t[1] || p[1] <= t[3] || p[1] <= t[5])))
        return false;
#define S(v) fx_mul((v), 0x28f)
    int32_t a = fx_mul(S(p[0] - t[0]), S(t[3] - t[1])) + fx_mul(S(p[1] - t[1]), -S(t[2] - t[0]));
    int32_t b = fx_mul(S(p[0] - t[2]), S(t[5] - t[3])) + fx_mul(S(p[1] - t[3]), -S(t[4] - t[2]));
    int32_t c = fx_mul(S(p[0] - t[4]), S(t[1] - t[5])) + fx_mul(S(p[1] - t[5]), -S(t[0] - t[4]));
#undef S
    return a >= 0 && b >= 0 && c >= 0;
}

void Section_BuildVisible(const Obj *cam)
{
    int32_t far = Proj_Far();
    memset(vis_mask, 0, sizeof vis_mask);
    g_nvisible = 0;
    if (g_nsec < 2) return;
    int32_t cx = cam->world.m[12], cz = cam->world.m[14];
    int r = fx_mul(far, recip) >> 16;
    int32_t pos[3] = {cx, 0, cz};
    int c = Section_Find(pos);
    int x0 = c % g_sec_row - r - 1, x1 = c % g_sec_row + 2 + r;
    int y0 = c / g_sec_row - r - 1, y1 = c / g_sec_row + 2 + r;
    if (x0 < 0) x0 = 0;
    if (x1 > g_sec_row) x1 = g_sec_row;
    if (y0 < 0) y0 = 0;
    if (y1 > g_sec_rows) y1 = g_sec_rows;
    /* the forward direction's x and z, normalised; the view wedge reaches the far distance on either side
       of it (the original also works out a point 10 back along it and does not use it) */
    int32_t fx = cam->world.m[8], fz = cam->world.m[10];
    uint32_t len = Fx_Sqrt((uint32_t)fx_mul(fx, fx) + (uint32_t)fx_mul(fz, fz));
    int32_t dx = 0, dz = 0;
    if (len) {
        int32_t inv = (int32_t)(0x100000000LL / (int32_t)len);
        dx = fx_mul(fx, inv);
        dz = fx_mul(fz, inv);
    }
    int32_t ax = fx_mul(dx, far), az = fx_mul(dz, far);
    int32_t bz = fx_mul(dz, fx_mul(far, 0x10000)), bx = fx_mul(dx, fx_mul(far, 0x10000));
    int32_t tri[6] = {cx, cz, ax - bz + cx, az + bx + cz, bz + ax + cx, -bx + az + cz};
    int row = g_sec_row;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            int i = y * row + x;
            const Section *s = &g_sec[i];
            bool in = in_triangle(&s->corner[4], tri) || in_triangle(&s->corner[6], tri) ||
                      in_triangle(&s->corner[2], tri) || in_triangle(&s->corner[0], tri);
            for (int ny = -1; !in && ny <= 1; ny++)
                for (int nx = -1; !in && nx <= 1; nx++) in = i == c + ny * row + nx;
            if (!in || i < 0 || i >= g_nsec) continue;
            Section *w = &g_sec[i];
            int32_t ddx = w->x - cx, ddz = w->z - cz;
            w->dist2 = (uint32_t)fx_mul(ddz, ddz) + (uint32_t)fx_mul(ddx, ddx);
            g_visible[g_nvisible++] = (uint16_t)i;
            w->x80 = 0;
            w->x84 = 1;
            vis_mask[i >> 5] |= 1u << (i & 31);
        }
}

/* The texture animation step before it (FUN_004bcc60, by the time since the last frame) is not
   ported yet. A mesh with flag 0x1000 goes polygon by polygon with its polygons' wrap flags. */
void Stage_DrawTerrain(const C3d *m)
{
    D3DMATRIX id;
    memset(&id, 0, sizeof id);
    for (int i = 0; i < 4; i++) id.m[i][i] = 1.0f;   /* 0x5207b8 */
    D3d_SetTransform(D3DTS_WORLD, &id);
    for (int k = 0; k < g_nvisible; k++) {
        uint8_t *me = g_sec[g_visible[k]].mesh;
        if (!me) continue;
        uint32_t f = (uint32_t)rd32(me + 0x30);
        bool poly = (f & 0x1000) != 0;
        Mesh_Draw(m, me, !poly, poly, 0);
    }
}
