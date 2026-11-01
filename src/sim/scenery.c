/* The cars against the scenery objects (see scenery.h). The original's work globals at 0x591490-
   0x5915f8 (the object's centre, radius and height, the push, the normal, the thresholds) are the
   fields of S here. */
#include "sim/scenery.h"
#include "sim/walls.h"
#include "world/section.h"
#include "world/stage.h"
#include "fixmath.h"
#include "game/gstate.h"
#include "pending.h"
#include <stdlib.h>
#include <string.h>

/* A collision volume (0x58ca70, 8 bytes): its reach, and for a box (the 0x5c record's type 0) the box
   in 7.9 fixed point (0x58c944, 12 bytes): the flat forward axis (x, z), the highest and lowest y,
   the half lengths along the forward and side axes. */
typedef struct {
    int32_t r;
    bool has_box;
    int16_t box[6];
} Vol;

/* An entry of a cell's list (8 bytes): the scenery record and its volume */
typedef struct {
    Scenery *sc;
    Vol *v;
} Entry;

static Vol *vols;                       /* 0x58ca70 */
static int nvols;
static Entry *ent_s, *ent_m;            /* 0x58c948, 0x58c94c: the fixed and the movable */
static int n_s, n_m;                    /* DAT_0058c95c, DAT_0058ca6c */
static int16_t *cell_s, *cell_m;        /* 0x58c940, 0x58c93c: a cell's list, -1 none */
static Entry **list_s, **list_m;        /* 0x58ca78, 0x58c954 */
static uint16_t *cnt_s, *cnt_m;         /* 0x58c960, 0x58c964 */
static uint8_t *standing;               /* 0x58c938: a movable volume still up, a bit per car */
static int32_t *up_flag;                /* 0x58c958 */

static struct {
    int32_t pos[3];        /* 0x591498 */
    int32_t r, h;          /* 0x591490, 0x591494 */
    bool box;              /* DAT_005914d8 */
    int32_t push[3];       /* 0x5914a8 */
    int32_t n[3];          /* 0x5915e8 */
    int32_t soft, hard;    /* DAT_005915dc, DAT_00591468 */
} S;

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static int32_t fdiv(int32_t a, int32_t b) { return (int32_t)(((int64_t)a << 16) / b); }
static uint32_t rflags(const Scenery *sc) { return (uint32_t)rd32(sc->rec + 0x10); }
static int nvol_of(const Scenery *sc) { return sc->mesh ? sc->mesh[0x110] : 0; }
static const uint8_t *vol_first(const Scenery *sc) { return C3d_Ref(sc->model, sc->mesh + 0x10c, sc->model->r5c); }
static const uint8_t *vol_next(const Scenery *sc, const uint8_t *v) { return C3d_Ref(sc->model, v + 0x58, sc->model->r5c); }

/* 0x4b9c20: v times the 3 x 3 of a matrix (row vector on the left) */
static void vmul(int32_t o[3], const int32_t v[3], const int32_t m[16])
{
    for (int k = 0; k < 3; k++) o[k] = fx_mul(m[8 + k], v[2]) + fx_mul(m[4 + k], v[1]) + fx_mul(m[k], v[0]);
}

void Scenery_CollFree(void)
{
    free(vols), free(ent_s), free(ent_m), free(cell_s), free(cell_m);
    free(list_s), free(list_m), free(cnt_s), free(cnt_m), free(standing), free(up_flag);
    vols = NULL, ent_s = ent_m = NULL, cell_s = cell_m = NULL, list_s = list_m = NULL;
    cnt_s = cnt_m = NULL, standing = NULL, up_flag = NULL;
    nvols = n_s = n_m = 0;
}

/* 0x4873f0: a box volume from its eight corners (relative to the object) and the placed matrix's
   rows: the flat forward axis from row 0 (row 1 when row 0 is within 8 degrees of vertical), the
   corners' furthest positive reach along it and across it, their y range */
static void box_make(Vol *v, const int32_t rows[9], const int32_t c[8][3])
{
    int16_t *b = v->box;
    b[2] = b[3] = 0;
    int32_t fx, fz, ef = 0, es = 0;
    int32_t x = iabs(rows[1]) < 0xfd71 ? rows[0] : rows[3], z = iabs(rows[1]) < 0xfd71 ? rows[2] : rows[5];
    uint32_t l = Fx_Sqrt((uint32_t)fx_mul(x, x) + (uint32_t)fx_mul(z, z));
    if (!l) fx = fz = 0;
    else {
        int32_t r = (int32_t)(0x100000000LL / (int32_t)l);
        fx = fx_mul(x, r), fz = fx_mul(z, r);
    }
    for (int i = 0; i < 8; i++) {
        int32_t e1 = fx_mul(fz, c[i][2]) + fx_mul(fx, c[i][0]);
        int32_t e2 = fx_mul(fz, c[i][0]) + fx_mul(-fx, c[i][2]);
        if (0 < e1 && ef < e1) ef = e1;
        if (0 < e2 && es < e2) es = e2;
        if (b[2] * 0x200 < c[i][1]) b[2] = (int16_t)(c[i][1] >> 9);
        if (c[i][1] < b[3] * 0x200) b[3] = (int16_t)(c[i][1] >> 9);
    }
    b[0] = (int16_t)(fx >> 9), b[1] = (int16_t)(fz >> 9), b[4] = (int16_t)(ef >> 9), b[5] = (int16_t)(es >> 9);
}

/* 0x471dd0's volume: a box (type 0) gets its corners through the volume's matrix times the
   placement, its reach half their diagonal; any other type is upright with the radius +0x44 */
static void vol_make(Vol *v, const Scenery *sc, const uint8_t *r)
{
    int32_t hx = rd32(r + 0x44), hz = rd32(r + 0x48), h = rd32(r + 0x4c);
    if (r[0] != 0) {
        v->has_box = false;
        v->r = hx;
        return;
    }
    v->has_box = true;
    hx = fx_mul(hx, 0x8000), hz = fx_mul(hz, 0x8000);
    int32_t c[8][3] = {{hx, 0, hz}, {hx, 0, -hz}, {-hx, 0, -hz}, {-hx, 0, hz},
                       {hx, h, hz}, {hx, h, -hz}, {-hx, h, -hz}, {-hx, h, hz}};
    FxMat a, m;
    for (int k = 0; k < 16; k++) a.m[k] = rd32(r + 4 + 4 * (uint32_t)k);
    Mat_Mul(&m, &a, &sc->m);
    for (int i = 0; i < 8; i++) {
        int32_t o[3];
        vmul(o, c[i], m.m);
        memcpy(c[i], o, sizeof o);
    }
    int32_t d[3] = {c[0][0] - c[6][0], c[0][1] - c[6][1], c[0][2] - c[6][2]};
    v->r = fx_mul((int32_t)Fx_Sqrt((uint32_t)fx_mul(d[0], d[0]) + (uint32_t)fx_mul(d[1], d[1]) + (uint32_t)fx_mul(d[2], d[2])), 0x8000);
    int32_t rows[9] = {m.m[0], m.m[1], m.m[2], m.m[4], m.m[5], m.m[6], m.m[8], m.m[9], m.m[10]};
    box_make(v, rows, c);
}

void Scenery_CollInit(void)
{
    Scenery_CollFree();
    /* the volumes are numbered by TEMP.OBJ's records, in order (a record outside that range, which
       the original would place past the end, gets slots after them here) */
    int first = g_obj_scenery, end = g_sht_scenery;
    int *vbase = calloc((size_t)(g_nscenery + 1), sizeof *vbase);
    if (!vbase) return;
    for (int i = 0; i < g_nscenery; i++) {
        vbase[i] = nvols;
        if (i >= first && i < end) nvols += nvol_of(g_scenery[i]);
    }
    for (int i = 0; i < g_nscenery; i++)
        if (i < first || i >= end) vbase[i] = nvols, nvols += nvol_of(g_scenery[i]);
    cell_s = malloc(sizeof *cell_s * (size_t)(g_nsec ? g_nsec : 1));
    cell_m = malloc(sizeof *cell_m * (size_t)(g_nsec ? g_nsec : 1));
    vols = calloc((size_t)(nvols ? nvols : 1), sizeof *vols);
    if (!cell_s || !cell_m || !vols) { free(vbase); Scenery_CollFree(); return; }
    for (int i = 0; i < g_nsec; i++) cell_s[i] = cell_m[i] = -1;
    /* the cells' lists: each cell with a volume gets the next list number, the volumes counted */
    int ls = 0, lm = 0;
    for (int i = 0; i < g_nsec; i++)
        for (Scenery *sc = g_sec[i].scenery; sc; sc = sc->next) {
            int k = nvol_of(sc);
            if (!k) continue;
            if (!(rflags(sc) & 0x2001000)) {
                n_s += k;
                if (cell_s[i] == -1) cell_s[i] = (int16_t)ls++;
            } else {
                n_m += k;
                if (cell_m[i] == -1) cell_m[i] = (int16_t)lm++;
            }
        }
    list_s = calloc((size_t)(ls ? ls : 1), sizeof *list_s), cnt_s = calloc((size_t)(ls ? ls : 1), sizeof *cnt_s);
    list_m = calloc((size_t)(lm ? lm : 1), sizeof *list_m), cnt_m = calloc((size_t)(lm ? lm : 1), sizeof *cnt_m);
    ent_s = calloc((size_t)(n_s ? n_s : 1), sizeof *ent_s), ent_m = calloc((size_t)(n_m ? n_m : 1), sizeof *ent_m);
    standing = malloc((size_t)(n_m ? n_m : 1)), up_flag = malloc(sizeof *up_flag * (size_t)(n_m ? n_m : 1));
    if (!list_s || !cnt_s || !list_m || !cnt_m || !ent_s || !ent_m || !standing || !up_flag) { free(vbase); Scenery_CollFree(); return; }
    for (int i = 0; i < g_nsec; i++)
        for (Scenery *sc = g_sec[i].scenery; sc; sc = sc->next) {
            int k = nvol_of(sc);
            if (!k) continue;
            if (!(rflags(sc) & 0x2001000)) cnt_s[cell_s[i]] += (uint16_t)k;
            else cnt_m[cell_m[i]] += (uint16_t)k;
        }
    for (int i = 0, at = 0; i < ls; i++) list_s[i] = ent_s + at, at += cnt_s[i];
    for (int i = 0, at = 0; i < lm; i++) list_m[i] = ent_m + at, at += cnt_m[i];
    for (int i = 0; i < n_m; i++) standing[i] = 0xff, up_flag[i] = 1;
    /* the entries, each volume made; in a two-player game the records without flag 0x8000 are hidden */
    int *fill_s = calloc((size_t)(ls ? ls : 1), sizeof *fill_s), *fill_m = calloc((size_t)(lm ? lm : 1), sizeof *fill_m);
    if (!fill_s || !fill_m) { free(fill_s), free(fill_m), free(vbase); Scenery_CollFree(); return; }
    int idx_of = 0;
    for (int i = 0; i < g_nsec; i++)
        for (Scenery *sc = g_sec[i].scenery; sc; sc = sc->next) {
            if (!sc->mesh) continue;
            if (!(rflags(sc) & 0x8000) && Gs_SelBits14() == 2) sc->rec[0x14] = 0;
            if (!nvol_of(sc)) continue;
            for (idx_of = 0; idx_of < g_nscenery && g_scenery[idx_of] != sc; idx_of++) {}
            int j = vbase[idx_of < g_nscenery ? idx_of : g_nscenery], left = nvol_of(sc);
            /* (the list is walked; the port stops it at the count the lists were sized by) */
            for (const uint8_t *r = vol_first(sc); r && left; r = vol_next(sc, r), j++, left--) {
                Entry *e = !(rflags(sc) & 0x2001000) ? &list_s[cell_s[i]][fill_s[cell_s[i]]++] : &list_m[cell_m[i]][fill_m[cell_m[i]]++];
                e->sc = sc;
                e->v = &vols[j < nvols ? j : nvols - 1];
                vol_make(e->v, sc, r);
            }
        }
    free(fill_s), free(fill_m), free(vbase);
    PENDING_NOTE(0x46f550, "Scenery_KnockPool (0x46f550, 0x46f7e0)");
}

/* 0x471b30, 0x471b80: a cell's fixed or movable entries */
static Entry *cell_list(int cell, bool movable, int *n)
{
    *n = 0;
    int16_t *t = movable ? cell_m : cell_s;
    if (!t || cell < 0 || cell >= g_nsec || t[cell] == -1) return NULL;
    *n = movable ? cnt_m[t[cell]] : cnt_s[t[cell]];
    return movable ? list_m[t[cell]] : list_s[t[cell]];
}

/* 0x471cc0: a movable entry's centre, 1,000 units up once it is down (for one player its first car's
   bit, else the flag) */
static void moved_pos(int32_t p[3], const Entry *e)
{
    memcpy(p, e->sc->pos, sizeof e->sc->pos);
    int i = (int)(e - ent_m);
    if (i < n_m) {
        if (Gs_SelBits14() < 2) { if (!(standing[i] & 1)) p[1] += 0x3e80000; }
        else if (!up_flag[i]) p[1] += 0x3e80000;
    }
}

/* 0x471d40: the movable entry still up for car n */
static bool up_for(const Entry *e, int n)
{
    int i = (int)(e - ent_m);
    return i < n_m && (standing[i] & (1u << (n & 31))) != 0;
}

/* 0x487b80: the two spheres (the reaches summed) overlap */
static bool near(int32_t ra, int32_t rb, const int32_t a[3], const int32_t b[3])
{
    int32_t r = ra + rb, d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    if (iabs(d[0]) > r || iabs(d[1]) > r || iabs(d[2]) > r) return false;
    return fx_mul(d[2], d[2]) + fx_mul(d[0], d[0]) + fx_mul(d[1], d[1]) < fx_mul(r, r);
}

static void car_pos(const Car *c, int32_t p[3]) { p[0] = CI32(c, 0x2d0), p[1] = CI32(c, 0x2d4), p[2] = CI32(c, 0x2d8); }

/* The car, its eight box corners and its flat box's four corners moved by m (y too when all3) */
static void move_by(Car *c, int32_t *b, const int32_t m[3], bool all3)
{
    for (uint32_t k = 0; k < 3; k++)
        if (all3 || k != 1) CW32(c, 0x2d0 + 4 * k, CI32(c, 0x2d0 + 4 * k) + m[k]);
    for (uint32_t i = 0; i < 8; i++)
        for (uint32_t k = 0; k < 3; k++)
            if (all3 || k != 1) CW32(c, 0x270 + 12 * i + 4 * k, CI32(c, 0x270 + 12 * i + 4 * k) + m[k]);
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 3; k++)
            if (all3 || k != 1) b[B_CORNER + 3 * i + k] += m[k];
}

/* 0x489b20: an upright cylinder (S.pos, S.r) against the car's flat box b: kind 1 or 2 it is against
   the forward or the side face, 3 against a corner, 0 apart. The car is pushed out (x and z) along
   the line from its centre to the cylinder's by the time to the face or the corner, times k (0 for a
   movable object); pf and ps the contact along the box's axes, scaled into its extents. */
static int cyl_box(Car *c, int32_t *b, int32_t *pf, int32_t *ps, int32_t k)
{
    int kind = 0;
    int32_t t = 0, cp[3];
    car_pos(c, cp);
    int32_t dx = S.pos[0] - cp[0], dz = S.pos[2] - cp[2];
    uint32_t l = Fx_Sqrt((uint32_t)fx_mul(dx, dx) + (uint32_t)fx_mul(dz, dz));
    int32_t ux = 0, uz = 0;
    if (l) {
        int32_t r = (int32_t)(0x100000000LL / (int32_t)l);
        ux = fx_mul(dx, r), uz = fx_mul(dz, r);
    }
    int32_t cf = fx_mul(b[B_FWD + 2], uz) + fx_mul(b[B_FWD], ux), cs = fx_mul(b[B_SIDE + 2], uz) + fx_mul(b[B_SIDE], ux);
    int32_t ef = fx_mul(b[B_FWD + 2], dz) + fx_mul(b[B_FWD], dx), es = fx_mul(b[B_SIDE + 2], dz) + fx_mul(b[B_SIDE], dx);
    if (b[B_EXT_F] < iabs(ef) && b[B_EXT_S] < iabs(es)) {
        /* beyond both faces: the nearest corner within the radius, the ray from it along u to the
           circle */
        int q = ef < 1 ? (0 < es) + 2 : (0 < es);
        int32_t r2 = fx_mul(S.r, S.r);
        int32_t qx = S.pos[0] - b[B_CORNER + 3 * q], qz = S.pos[2] - b[B_CORNER + 3 * q + 2];
        int32_t dd = fx_mul(qz, qz) + fx_mul(qx, qx);
        if (r2 < dd) return 0;
        int32_t bb = fx_mul(uz, qz) + fx_mul(ux, qx), aa = fx_mul(uz, uz) + fx_mul(ux, ux);
        int32_t disc = fx_mul(bb, bb) - fx_mul(aa, dd - r2);
        if (-1 < disc) t = fdiv((int32_t)Fx_Sqrt((uint32_t)disc) - bb, aa);
        kind = 3;
    } else {
        int32_t xf = b[B_EXT_F] + S.r, xs = S.r + b[B_EXT_S];
        if (xf < iabs(ef) || xs < iabs(es)) return 0;
        bool h0 = false, h1 = false;
        int32_t t0 = 0, t1 = 0;
        if (cf >= 0x42) t0 = fdiv(xf - ef, cf), h0 = true;
        else if (cf < -0x41) t0 = fdiv(iabs(ef + xf), -cf), h0 = true;
        if (cs >= 0x42) t1 = fdiv(xs - es, cs), h1 = true;
        else if (cs < -0x41) t1 = fdiv(iabs(xs + es), -cs), h1 = true;
        if (h0 && t0 < 0x7d000000) kind = 1;
        else t0 = 0x7d000000;
        if (h1 && t1 < t0) kind = 2, t0 = t1;
        if (0 < t0 && (h1 || h0)) t = t0;
        if (!kind) return 0;
    }
    S.push[0] = S.push[1] = S.push[2] = 0;
    if (0 < t) {
        int32_t m = -fx_mul(t, k);
        S.push[0] = fx_mul(ux, m), S.push[2] = fx_mul(uz, m);
        move_by(c, b, S.push, false);
    }
    *pf = fdiv(fx_mul(ef, b[B_EXT_F]), b[B_EXT_F] + S.r);
    *ps = fdiv(fx_mul(es, b[B_EXT_S]), S.r + b[B_EXT_S]);
    return kind;
}

/* 0x489750: the cylinder hit: the contact point into the car's frame (+0x5dc), the normal (the
   face's axis, or from the cylinder's centre to a corner's contact), the push kept only along the
   normal, the thresholds of the response (0.5, 1.342) */
static bool cyl_hit(Car *c, int32_t *b, int32_t k)
{
    int32_t pf, ps;
    int kind = cyl_box(c, b, &pf, &ps, k);
    if (!kind) return false;
    int32_t w[3], l[3], cp[3];
    for (int i = 0; i < 3; i++) w[i] = fx_mul(b[B_FWD + i], pf) + fx_mul(b[B_SIDE + i], ps);
    Walls_RotT(c, l, w);
    for (uint32_t i = 0; i < 3; i++) CW32(c, 0x5dc + 4 * i, l[i]);
    car_pos(c, cp);
    if (kind == 3) {
        int32_t x = cp[0] + w[0] - S.pos[0], z = cp[2] + w[2] - S.pos[2];
        uint32_t len = Fx_Sqrt((uint32_t)fx_mul(x, x) + (uint32_t)fx_mul(z, z));
        if (!len) S.n[0] = S.n[1] = S.n[2] = 0;
        else {
            int32_t r = (int32_t)(0x100000000LL / (int32_t)len);
            S.n[0] = fx_mul(x, r), S.n[1] = 0, S.n[2] = fx_mul(z, r);
        }
    } else
        for (int i = 0; i < 3; i++) S.n[i] = b[(kind == 1 ? B_FWD : B_SIDE) + i];
    int32_t d = fx_mul(S.n[2], S.push[2]) + fx_mul(S.n[0], S.push[0]) + fx_mul(S.n[1], S.push[1]);
    int32_t m[3] = {fx_mul(S.n[0], d) - S.push[0], fx_mul(S.n[1], d) - S.push[1], fx_mul(S.n[2], d) - S.push[2]};
    move_by(c, b, m, true);
    PENDING_NOTE(0x48c870, "Cars_PushGroup");   /* the cars in contact moved with it: none without the car-to-car pass */
    S.soft = 0x8000, S.hard = 0x1578d;
    return true;
}

/* 0x487e50: a low object (flag 0x4000) under the car's box: bumps under the wheels */
static void low_object(Car *c, const int32_t *b)
{
    int32_t cp[3];
    car_pos(c, cp);
    int32_t x = S.pos[0] - cp[0], z = S.pos[2] - cp[2];
    int32_t ef = fx_mul(b[B_FWD + 2], z) + fx_mul(b[B_FWD], x), es = fx_mul(b[B_SIDE + 2], z) + fx_mul(b[B_SIDE], x);
    if (b[B_EXT_F] < iabs(ef) && b[B_EXT_S] < iabs(es)) return;
    if (iabs(ef) <= b[B_EXT_F] + S.r && iabs(es) <= b[B_EXT_S] + S.r) Walls_Bumps(c);
}

static void cross(int32_t o[3], const int32_t a[3], const int32_t b[3])
{
    o[0] = fx_mul(a[1], b[2]) - fx_mul(a[2], b[1]);
    o[1] = fx_mul(a[2], b[0]) - fx_mul(a[0], b[2]);
    o[2] = fx_mul(a[0], b[1]) - fx_mul(a[1], b[0]);
}

/* 0x48c750: the turn impulse within 8, 4 and 8 times the impulse scale (0x519c90) */
static void clamp_turn(Car *c)
{
    int32_t k = gms32(0x519c90), lim[3] = {fx_mul(k, 0x80000), fx_mul(k, 0x40000), fx_mul(k, 0x80000)};
    for (uint32_t i = 0; i < 3; i++) {
        int32_t v = CI32(c, 0x5d0 + 4 * i);
        if (lim[i] < v) CW32(c, 0x5d0 + 4 * i, lim[i]);
        else if (v < -lim[i]) CW32(c, 0x5d0 + 4 * i, -lim[i]);
    }
}

/* 0x48be20: the response: the contact (+0x5dc, its y 0, within the half size +0x204); the velocity
   there (with the spin +0x420) along the normal: above 1.342 a hard knock, above 0.5 a knock (the car
   loose, +0xc00), unless already loose or +0xb64; for a movable object the impulse is a hundredth
   and the object goes down. The impulse (times 0x519c90) into +0x5c4, the damage once per object
   and step, the turn about the contact (lowered by one or four units for a knock) into +0x5d0. */
static bool respond(Car *c, const Entry *e, int id)
{
    bool knock = false, hard = false, down = false;
    CW32(c, 0x5e0, 0);
    int32_t p[3], h[3], spin[3], vc[3], v[3], imp[3], li[3];
    for (uint32_t i = 0; i < 3; i++) {
        p[i] = CI32(c, 0x5dc + 4 * i);
        h[i] = CI32(c, 0x204 + 4 * i);
        if (h[i] < iabs(p[i])) p[i] = p[i] < 0 ? -h[i] : h[i];   /* 0x48c6e0 */
        CW32(c, 0x5dc + 4 * i, p[i]);
        spin[i] = CI32(c, 0x420 + 4 * i);
    }
    int32_t sp[3];
    cross(sp, spin, p);
    int32_t m[16];
    for (int i = 0; i < 16; i++) m[i] = CI32(c, 4 * (uint32_t)i);   /* the orientation (+0x750) */
    vmul(vc, sp, m);
    for (uint32_t i = 0; i < 3; i++) v[i] = vc[i] + CI32(c, 0x408 + 4 * i);
    int32_t vn = fx_mul(S.n[2], v[2]) + fx_mul(S.n[0], v[0]) + fx_mul(S.n[1], v[1]);
    if (!CI32(c, 0xb64) && !CI32(c, 0xc00)) {
        int32_t a = iabs(vn);
        if (S.hard < a) knock = hard = true;
        else if (S.soft < a) knock = true;
    }
    int32_t j = -vn;
    if (rflags(e->sc) & 0x2001000) {
        if (j != 0) down = true, hard = false, j = fx_mul(j, 0x28f);
    }
    int32_t k = gms32(0x519c90);
    for (int i = 0; i < 3; i++) imp[i] = fx_mul(fx_mul(S.n[i], j), k);
    Walls_RotT(c, li, imp);
    for (uint32_t i = 0; i < 3; i++) CW32(c, 0x5c4 + 4 * i, CI32(c, 0x5c4 + 4 * i) + imp[i]);
    bool seen = false;
    for (int i = 0; i < CI8(c, 0xb3e); i++)
        if ((int16_t)(c->s[0xad6 + 2 * i] | c->s[0xad7 + 2 * i] << 8) == id) seen = true;
    if (!seen) {
        if (!S.box) PENDING_NOTE(0x466ef0, "Car_Damage");   /* Car_Damage(c, S.pos, S.n, S.r, 1, 0) */
        else PENDING_NOTE(0x466ef0, "Car_Damage (a box)");
    }
    if (!down && knock) {
        CW32(c, 0x96c, 0x10000);
        CW32(c, 0xc00, 1);
        CW32(c, 0xc04, 1);
    }
    CW32(c, 0x5e0, 0);
    p[1] = 0;
    int32_t t[3];
    if (!knock) {
        cross(t, li, p);
        for (int i = 0; i < 3; i++) t[i] = fx_mul(t[i], S.box ? 0x18000 : 0x30000);
    } else {
        int32_t lift = hard ? 0x40000 : 0x10000;
        int32_t q[3] = {fx_mul(-CI32(c, 0x364), lift) + p[0], fx_mul(-CI32(c, 0x370), lift) + p[1], fx_mul(-CI32(c, 0x37c), lift) + p[2]};
        cross(t, li, q);
        for (int i = 0; i < 3; i++) t[i] = fx_mul(t[i], 0x40000);
        if (hard) CW32(c, 0x40c, CI32(c, 0x40c) + 0x4000);
    }
    for (uint32_t i = 0; i < 3; i++) CW32(c, 0x5d0 + 4 * i, CI32(c, 0x5d0 + 4 * i) + t[i]);
    clamp_turn(c);
    int8_t n = CI8(c, 0xb3f);
    if (n < 5) {
        CW16(c, 0xae0 + 2 * (uint32_t)n, (uint16_t)id);
        CW8(c, 0xb3f, (uint8_t)(n + 1));
    }
    if (Gs_Option(4))
        for (uint32_t i = 0; i < 3; i++) {
            CW32(c, 0x5c4 + 4 * i, CI32(c, 0x5c4 + 4 * i) - imp[i]);
            CW32(c, 0x408 + 4 * i, CI32(c, 0x408 + 4 * i) + imp[i] * 2);
        }
    return down;
}

int Car_Scenery(Car *c)
{
    int hit = 0;
    CW8(c, 0xb3f, 0);
    int idx = CI8(c, 0xb1a);
    for (uint32_t o = 0xbbc; o < 0xbdc; o += 4) CW32(c, o, 0);
    int16_t cells[4];
    for (uint32_t i = 0; i < 4; i++) cells[i] = (int16_t)(c->s[0xb00 + 2 * i] | c->s[0xb01 + 2 * i] << 8);
    int32_t cp[3];
    for (int pass = CI32(c, 0xc18) != 0; pass < 2; pass++)
        for (int q = 0; q < 4; q++) {
            if (cells[q] < 0) continue;
            int n;
            Entry *list = cell_list(cells[q], pass == 1, &n);
            for (int i = 0; i < n; i++) {
                Entry *e = &list[i];
                if (!e->sc) continue;   /* the port: a list shorter than its mesh's count leaves gaps */
                int32_t pos[3];
                if (pass == 0) memcpy(pos, e->sc->pos, sizeof pos);
                else moved_pos(pos, e);
                uint32_t f = rflags(e->sc);
                if (!(f & 0x8000) && Gs_SelBits14() == 2) continue;
                S.box = e->v->has_box;
                car_pos(c, cp);
                hit = near(CI32(c, 0x758), e->v->r, cp, pos);
                if (!hit) continue;
                hit = 0;
                if (pass == 1 && !up_for(e, idx)) continue;
                int32_t *b = Walls_CarBox(c);
                if (S.box) {
                    PENDING_NOTE(0x487f60, "Car_SceneryBox (0x487c40, 0x487f60)");
                    continue;
                }
                /* 0x487e00: the centre, the radius, the height of the mesh's first volume */
                memcpy(S.pos, pos, sizeof pos);
                S.r = e->v->r;
                S.h = rd32(vol_first(e->sc) + 0x4c);
                if (f & 0x4000) {
                    if (!CI32(c, 0xc20)) low_object(c, b);
                    continue;
                }
                hit = cyl_hit(c, b, f & 0x2001000 ? 0 : 0x10000);
                if (hit && respond(c, e, 0)) PENDING_NOTE(0x46fe70, "Scenery_Knock");
            }
        }
    CW8(c, 0xb3e, (uint8_t)CI8(c, 0xb3f));
    for (int i = 0; i < CI8(c, 0xb3e); i++) {
        c->s[0xad6 + 2 * i] = c->s[0xae0 + 2 * i];
        c->s[0xad7 + 2 * i] = c->s[0xae1 + 2 * i];
    }
    return hit;
}

int Scenery_CollCount(int *boxes)
{
    *boxes = 0;
    for (int i = 0; i < n_s; i++) *boxes += ent_s[i].sc && ent_s[i].v->has_box;
    return n_s;
}

bool Scenery_CollAt(int i, int32_t pos[3], int32_t *r, bool *box, uint32_t *flags)
{
    if (i < 0 || i >= n_s || !ent_s[i].sc) return false;
    memcpy(pos, ent_s[i].sc->pos, sizeof ent_s[i].sc->pos);
    *r = ent_s[i].v->r;
    *box = ent_s[i].v->has_box;
    *flags = rflags(ent_s[i].sc);
    return true;
}
