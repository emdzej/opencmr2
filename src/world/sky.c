/* The sky's meshes (see sky.h). */
#include "world/sky.h"
#include "c3d.h"
#include "crt.h"
#include "fixmath.h"
#include "game/mem.h"
#include "render/dlight.h"
#include "render/mesh.h"
#include "render/view.h"
#include "world/obj.h"
#include "world/relight.h"
#include "world/stage.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { DOME_MAX = 200 };   /* the heights' table, 0x591c80-0x591fa0 */

/* The meshes: the dome, ground, clouds, cloud top, B, A and C (0x5920c4-0x5920f4, their counts at
   0x59213e, 0x59213c, 0x592138, 0x59213a, 0x592140, 0x592142, 0x592144). */
enum { DOME, GROUND, CLOUD, TOP, FLASH, DISC, HALO, NMESH };
static struct { Obj *o; int n; bool dirty; } mesh[NMESH];   /* dirty: 0x592148, 0x59214c, 0x592154, -, 0x592158, 0x592150, 0x59215c */
static Obj *disc_o, *halo_o;          /* A and C (0x5920b8, 0x5920c0) */
static int32_t height[DOME_MAX];      /* the dome's vertices' heights as loaded (0x591c80) */
static int32_t dome_min, dome_inv;    /* 0x592120, 0x592124: the lowest, 1 / (highest - lowest) */
static int32_t sun[3];                /* 0x592114: the dome's vertex 104 */
static int32_t under_sun[3];          /* 0x592108: the ground's vertex 50 */
static int32_t bolt[3];               /* 0x5920fc: the clouds' vertex the bolt is at */
static int32_t horizon_y;             /* 0x591fac: the ground's height three quarters up */
static int32_t off_sky, off_ground, off_cloud;   /* 0x59212c, 0x592130, 0x592128 */
static int32_t scale;                 /* 0x592134 */
static uint8_t fog_ground, fog_sky;   /* 0x592146, 0x592147: the fog factor the meshes' vertices take */

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
static float rdf(const uint8_t *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }
static void wrf(uint8_t *p, float f) { uint32_t u; memcpy(&u, &f, 4); wr32(p, u); }
/* a vertex coordinate in 16.16, as the original rounds it (x87, to nearest) */
static int32_t fx(const uint8_t *p) { return (int32_t)llrint((double)rdf(p) * 65536.0); }

/* the mesh's vertices (0x30 bytes each), NULL for none */
static uint8_t *verts(int k)
{
    Obj *o = mesh[k].o;
    if (!o || !o->mesh || !o->model) return NULL;
    return (uint8_t *)C3d_Ref(o->model, o->mesh + 0x0c, o->model->verts);
}

static bool at(const uint8_t *v, const int32_t p[3]) { return fx(v) == p[0] && fx(v + 4) == p[1] && fx(v + 8) == p[2]; }
/* R, G, B, A bytes as the vertex's A R G B */
static uint32_t argb(const uint8_t c[4]) { return (uint32_t)c[3] << 24 | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2]; }

/* Vec_Mul3 0x4b9c20: v times the 3 x 3 (row vector on the left) */
static void vmul(int32_t o[3], const int32_t v[3], const int32_t m[16])
{
    for (int k = 0; k < 3; k++) o[k] = fx_mul(m[8 + k], v[2]) + fx_mul(m[4 + k], v[1]) + fx_mul(m[k], v[0]);
}

void Sky_Setup(void)
{
    Obj *a = NULL, *b = NULL, *c = NULL;
    if (g_sky && (a = g_sky->child) && (b = a->next)) c = b->next;
    /* 0x591fa0-0x591fa8, the bolt's three textures, are cleared here and never loaded. The original
       also makes an object of kind 2 under A (0x4adfa0, kept at 0x5920bc and never read): not ported. */
    disc_o = a, halo_o = c;
    Obj *objs[NMESH] = {g_sky, g_ground, g_cloud, g_cloud_top, b, a, c};
    for (int k = 0; k < NMESH; k++) {
        mesh[k].o = objs[k] && objs[k]->mesh ? objs[k] : NULL;
        mesh[k].n = mesh[k].o ? (int)rd32(mesh[k].o->mesh + 0x10) : 0;   /* 0x4ab710 */
        mesh[k].dirty = false;
    }
    uint8_t *v = verts(DOME);
    if (v && mesh[DOME].n) {
        int32_t lo = 0, hi = 0;
        for (int i = mesh[DOME].n - 1; i >= 0; i--) {
            int32_t h = fx(v + i * C3D_VERTEX + 4);
            if (i == mesh[DOME].n - 1) lo = hi = h;
            else {
                if (h > hi) hi = h;
                if (h < lo) lo = h;
            }
            if (i == 0x68) sun[0] = fx(v + i * C3D_VERTEX), sun[1] = h, sun[2] = fx(v + i * C3D_VERTEX + 8);
            if (i < DOME_MAX) height[i] = h;   /* (the original's table has no bound) */
        }
        if (hi != lo) dome_inv = (int32_t)(0x100000000LL / (hi - lo)), dome_min = lo;
    }
    v = verts(GROUND);
    if (v && mesh[GROUND].n) {
        if (mesh[GROUND].n > 50)
            for (int k = 0; k < 3; k++) under_sun[k] = fx(v + 50 * C3D_VERTEX + 4 * k);
        int32_t lo = fx(v + 4), hi = lo;
        for (int i = mesh[GROUND].n - 1; i >= 0; i--) {
            int32_t h = fx(v + i * C3D_VERTEX + 4);
            if (h > hi) hi = h;
            if (h < lo) lo = h;
        }
        horizon_y = fx_mul(hi - lo, 0xc000) + lo;
    }
    v = verts(FLASH);
    for (int i = 0; v && i < mesh[FLASH].n; i++) wr32(v + i * C3D_VERTEX + 0x18, 0);
    mesh[FLASH].dirty = true;
}

void Sky_Offsets(int32_t *s, int32_t *g, int32_t *c) { *s = off_sky, *g = off_ground, *c = off_cloud; }
void Sky_SetScale(int32_t v) { scale = v; }
int32_t Sky_Scale(void) { return scale; }

void Sky_SunPoint(int32_t out[3])
{
    if (!g_sky) { out[0] = out[1] = out[2] = 0; return; }
    vmul(out, sun, g_sky->local.m);
}

void Sky_Cut(int32_t s, int32_t g, int32_t c)
{
    int32_t h = horizon_y - s + g;
    uint8_t *v = verts(DOME);
    for (int i = mesh[DOME].n - 1; v && i >= 0; i--) {
        uint8_t *p = v + i * C3D_VERTEX;
        if (i < DOME_MAX && height[i] < h && !at(p, sun)) wrf(p + 4, (float)h * (1.0f / 65536.0f));
    }
    mesh[DOME].dirty = true;
    off_cloud = c, off_ground = g, off_sky = s;
    int32_t p[3];
    Sky_SunPoint(p);
    p[1] += off_sky;
    Dlight_SetPosition(g_sun, p);   /* 0x4b35b0 */
}

void Sky_ColourDome(const uint8_t lo[3], const uint8_t hi[3], const uint8_t sc[3], int32_t mix)
{
    int32_t base[3], d[3];
    for (int k = 0; k < 3; k++) base[k] = lo[k] << 16, d[k] = (hi[k] << 16) - (lo[k] << 16);
    int32_t t = fx_mul(sun[1] - dome_min, dome_inv);
    t = t < 0 ? 0 : t > 0x10000 ? 0x10000 : t;
    /* the sun's colour: its height's, moved toward sc (the bytes not clamped) */
    uint8_t s[3];
    for (int k = 0; k < 3; k++) {
        int32_t at_sun = fx_mul(d[k], t) + base[k];
        s[k] = (uint8_t)((fx_mul((sc[k] << 16) - at_sun, mix) + at_sun) >> 16);
    }
    uint8_t *v = verts(DOME);
    for (int i = mesh[DOME].n - 1; v && i >= 0; i--) {
        uint8_t *p = v + i * C3D_VERTEX;
        int32_t y = fx(p + 4);
        if (y == sun[1] && fx(p) == sun[0] && fx(p + 8) == sun[2]) {
            wr32(p + 0x18, 0xff000000u | (uint32_t)s[0] << 16 | (uint32_t)s[1] << 8 | s[2]);
        } else {
            int32_t u = fx_mul(y - dome_min, dome_inv);
            u = u < 0 ? 0 : u > 0x10000 ? 0x10000 : u;
            uint32_t col = 0xff000000u;
            for (int k = 0; k < 3; k++) {
                int32_t c = (fx_mul(d[k], u) + base[k]) >> 16;
                col |= (uint32_t)(c < 0 ? 0 : c > 0xff ? 0xff : c) << (16 - 8 * k);
            }
            wr32(p + 0x18, col);
        }
        wr32(p + 0x1c, (uint32_t)fog_sky << 24);
    }
    mesh[DOME].dirty = true;
}

/* every vertex coloured c, the one at `special` (when given) c2 */
static void colour(int k, const uint8_t c[4], const int32_t *special, const uint8_t c2[4], uint32_t spec)
{
    uint8_t *v = verts(k);
    for (int i = mesh[k].n - 1; v && i >= 0; i--) {
        uint8_t *p = v + i * C3D_VERTEX;
        wr32(p + 0x18, argb(special && at(p, special) ? c2 : c));
        wr32(p + 0x1c, spec);
    }
}

void Sky_ColourGround(const uint8_t c[4], const uint8_t c2[4])
{
    colour(GROUND, c, under_sun, c2, (uint32_t)fog_ground << 24);
    mesh[GROUND].dirty = true;
}

void Sky_ColourClouds(const uint8_t c[4], const uint8_t c2[4])
{
    colour(CLOUD, c, bolt, c2, (uint32_t)fog_sky << 24);
    if (mesh[TOP].o) colour(TOP, c, NULL, NULL, (uint32_t)fog_sky << 24);
    mesh[CLOUD].dirty = true;
}

void Sky_ColourFlash(const uint8_t c[4])
{
    colour(FLASH, c, NULL, NULL, 0xff000000u);
    mesh[FLASH].dirty = true;
}

void Sky_ColourDisc(const uint8_t c[4])
{
    colour(DISC, c, NULL, NULL, (uint32_t)fog_sky << 24);
    mesh[DISC].dirty = true;
}

void Sky_ColourHalo(const uint8_t c[4])
{
    if (!mesh[HALO].o) return;
    colour(HALO, c, NULL, NULL, (uint32_t)fog_sky << 24);
    mesh[HALO].dirty = true;
}

/* Mat_SetRow0 / Mat_SetRow2 0x4bace0 / 0x4bad20 with a turn about y by deg (16.16 degrees): rows
   (cos, 0, sin) and (-sin, 0, cos) */
static void turn(Obj *o, int32_t deg)
{
    if (!o) return;
    double a = (double)deg * (1.0 / 65536.0) * 3.14159265359 * (1.0 / 180.0);
    int32_t c = (int32_t)llrint(cos(a) * 65536.0), s = (int32_t)llrint(sin(a) * 65536.0);
    int32_t *m = o->local.m;
    m[0] = c, m[1] = 0, m[2] = s;
    m[8] = -s, m[9] = 0, m[10] = c;
}

void Sky_Turn(int32_t s, int32_t c)
{
    if (g_sky) {
        if (s == -0x3e70000) {
            int32_t *m = g_sky->local.m;
            m[0] = 0x10000, m[1] = 0, m[2] = 0;
            m[8] = 0, m[9] = 0, m[10] = 0x10000;
        } else turn(g_sky, s);
    }
    turn(g_cloud, c);
    if (g_cloud_top) turn(g_cloud_top, c);
}

/* 0x492bd0's rows: row 0 the camera's row 0, row 2 its row 1, row 1 its row 2 reversed, all x 0xbc6 */
static void face(Obj *o, const Obj *cam)
{
    if (!o) return;
    const int32_t *r = cam->local.m;
    int32_t *m = o->local.m;
    for (int k = 0; k < 3; k++) {
        m[k] = fx_mul(r[k], 0xbc6);
        m[8 + k] = fx_mul(r[4 + k], 0xbc6);
        m[4 + k] = fx_mul(r[8 + k], -0xbc6);
    }
}

void Sky_FaceCamera(const Obj *cam)
{
    if (!cam) return;
    face(disc_o, cam);
    face(halo_o, cam);
}

void Sky_FogOn(uint32_t on) { Fog_SetEnabled(on); }

void Sky_Fog(const uint8_t rgb[3], int32_t near, int32_t far)
{
    Fog_Set(near, far, near, far, (uint32_t)rgb[0] | (uint32_t)rgb[1] << 8 | (uint32_t)rgb[2] << 16);
    int32_t d = (int32_t)(((gm32(0x52afd0) >> 0x15 & 0xf) + 2) * 0x320000);   /* 0x405ca0: the draw distance */
    if (d < near) {
        fog_ground = fog_sky = 0xff;
        return;
    }
    if (d > far) {
        fog_ground = fog_sky = 0;
        return;
    }
    int32_t t = d - near;
    if (far != near) t = (int32_t)(((int64_t)t << 16) / (far - near));
    int32_t f = fx_mul(t, 0xff0000) >> 16;
    f = f < 0 ? 0 : f > 0xff ? 0xff : f;
    fog_ground = fog_sky = (uint8_t)(0xff - f);
}

void Sky_Upload(void)
{
    static const int order[] = {CLOUD, FLASH, GROUND, DOME, DISC, HALO};
    for (size_t i = 0; i < sizeof order / sizeof order[0]; i++) {
        int k = order[i];
        if (!mesh[k].dirty) continue;
        if (mesh[k].o) Mesh_UploadVerts(mesh[k].o->model, mesh[k].o->mesh);
        if (k == CLOUD && mesh[TOP].o) Mesh_UploadVerts(mesh[TOP].o->model, mesh[TOP].o->mesh);
        mesh[k].dirty = false;
    }
}

int Sky_BoltVertex(void)
{
    if (mesh[CLOUD].n <= 0 || !g_cloud) return -1;
    /* Vec_RotateT 0x4b9da0: the sun through the clouds' transposed 3 x 3 */
    const int32_t *m = g_cloud->local.m;
    int32_t s[3];
    for (int k = 0; k < 3; k++) s[k] = fx_mul(m[4 * k], sun[0]) + fx_mul(m[4 * k + 1], sun[1]) + fx_mul(m[4 * k + 2], sun[2]);
    uint8_t *v = verts(CLOUD);
    if (!v) return -1;
    int32_t best = 0x640000;
    int found = -1;
    for (int i = 0; i < mesh[CLOUD].n; i++) {
        const uint8_t *p = v + i * C3D_VERTEX;
        int32_t d[3] = {s[0] - fx(p), s[1] - fx(p + 4), s[2] - fx(p + 8)};
        if (abs(d[0]) > best || abs(d[1]) > best || abs(d[2]) > best) continue;
        uint32_t q = (uint32_t)fx_mul(d[2], d[2]) + (uint32_t)fx_mul(d[0], d[0]) + (uint32_t)fx_mul(d[1], d[1]);
        if ((int32_t)q > 0x27100000) continue;   /* 10,000: within 100 */
        best = (int32_t)Fx_Sqrt(q);
        found = i;
    }
    if (found < 0) return -1;
    for (int k = 0; k < 3; k++) bolt[k] = fx(v + found * C3D_VERTEX + 4 * k);
    return found;
}

void Sky_BoltTexture(void)
{
    crt_rand();   /* rand() % 3 picks one of 0x591fa0's three, all NULL */
}
