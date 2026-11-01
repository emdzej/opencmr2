/* The particles (see particles.h). */
#include "world/particles.h"
#include "render/sprite3d.h"
#include "crt.h"
#include "fixmath.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static PType *types;            /* DAT_006a2cd8 */
static Particle *parts;         /* DAT_006a2cdc */
static int ntypes, nparts;      /* DAT_006a2cd0, DAT_006a2cd4 */
static int nalloc;              /* the port: the slots allocated (the cursor may point past nparts) */
static PType *cur;              /* DAT_006a2ce0: the type being defined */
static int cursor;              /* DAT_006a2ce4 */
/* DAT_006a2ce8: the wind the drag pulls towards (times 0x1999); nothing in the game writes it, so it
   stays zero */
static const int32_t wind[3];

static void clear_types(void)   /* 0x4affe0 */
{
    for (int i = 0; i < ntypes; i++) types[i].flags &= 0xfe;
}

static void clear_parts(void)   /* 0x4affb0 */
{
    for (int i = 0; i < nparts; i++) parts[i].alive = 0;
}

void Particles_Alloc(int nt, int np)
{
    Particles_Free();   /* (the original allocates again each race without freeing) */
    types = calloc((size_t)(nt > 0 ? nt : 1), sizeof *types);
    parts = calloc((size_t)(np > 0 ? np : 1), sizeof *parts);
    if (types && parts) {
        ntypes = nt;
        nparts = nalloc = np;
        clear_types();
        clear_parts();
        return;   /* (Particles_Free registered at exit) */
    }
    free(types);
    free(parts);
    types = NULL;
    parts = NULL;
    ntypes = nparts = nalloc = 0;
}

void Particles_Free(void)
{
    clear_types();
    clear_parts();
    free(types);
    free(parts);
    types = NULL;
    parts = NULL;
    ntypes = nparts = nalloc = 0;
}

void Particles_SetCount(int n) { nparts = n < nalloc ? n : nalloc; }   /* (the original does not cap it) */
void Particle_Kill(Particle *p) { p->alive = 0; }

void Particles_KillAll(void)
{
    for (int i = 0; i < nparts; i++)
        if (parts[i].alive) Particle_Kill(&parts[i]);
}

int Particles_Alive(void)
{
    int n = 0;
    for (int i = 0; i < nparts; i++) n += parts[i].alive != 0;
    return n;
}

PType *PType_Get(int n) { return n >= 0 && n < ntypes ? &types[n] : NULL; }
Particle *Particle_Get(int n) { return n >= 0 && n < nalloc ? &parts[n] : NULL; }
int Particle_Index(const Particle *p) { return (int)(p - parts); }

/* ---- the types ---- */

void PType_Begin(int n)
{
    cur = PType_Get(n);
    if (!cur) return;
    /* the scales, the spin and the extents keep what the slot had */
    cur->life = 0;
    cur->gravity = cur->drag = cur->bounce = cur->friction = 0;
    cur->alpha0 = 0x80;
    cur->alpha1 = 0;
    cur->alpha_step = 0;
    cur->rgb[0] = cur->rgb[1] = cur->rgb[2] = 0xff;
    cur->anim = NULL;
    cur->nanim = cur->anim_delay = cur->anim_time = 0;
    cur->draw = cur->step = cur->post = cur->spawn = NULL;
    cur->user = 0;
    cur->flags = 0;
    cur->flags2 &= 0xf0;
    cur->spread[0] = cur->spread[1] = cur->spread[2] = 0;
    cur->tex = NULL;
}

void PType_Copy(int from)
{
    PType *t = PType_Get(from);
    if (cur && t && (t->flags & 1)) *cur = *t;
}

void PType_End(void)
{
    if (!cur) return;
    cur->flags |= 1;
    cur = NULL;
}

void PType_Tex(Tex *tex, int32_t e0, int32_t e1, int32_t e2, int32_t e3)
{
    if (!cur) return;
    cur->tex = tex;
    cur->e[0] = e0, cur->e[1] = e1, cur->e[2] = e2, cur->e[3] = e3;
}

void PType_Anim(Tex **frames, int32_t n, int32_t delay, int32_t time, bool loop, bool random_start,
                int32_t e0, int32_t e1, int32_t e2, int32_t e3)
{
    if (!cur) return;
    cur->anim = frames;
    cur->nanim = n;
    cur->anim_delay = delay;
    cur->anim_time = time;
    cur->flags2 = (uint8_t)((cur->flags2 & 0xf3) | (loop ? 4 : 0) | (random_start ? 8 : 0));
    cur->e[0] = e0, cur->e[1] = e1, cur->e[2] = e2, cur->e[3] = e3;
}

void PType_Motion(int32_t life, int32_t gravity, int32_t drag, int32_t bounce, int32_t friction,
                  bool bounces, bool dies_below)
{
    if (!cur) return;
    cur->life = life;
    cur->gravity = gravity;
    cur->drag = drag;
    cur->bounce = bounce;
    cur->friction = friction;
    cur->flags = (uint8_t)((cur->flags & 0x7d) | (bounces ? 0x80 : 0) | (dies_below ? 2 : 0));
}

void PType_Fade(uint8_t a0, uint8_t a1, uint8_t step, bool dies_at_end)
{
    if (!cur) return;
    cur->alpha0 = a0;
    cur->alpha1 = a1;
    cur->alpha_step = step;
    cur->flags = (uint8_t)((cur->flags & 0xbb) | (step ? 0x40 : 0) | (dies_at_end ? 4 : 0));
}

void PType_Colour(uint8_t r, uint8_t g, uint8_t b, bool lit)
{
    if (!cur) return;
    cur->flags2 = (uint8_t)((cur->flags2 & 0xfd) | (lit ? 2 : 0));
    cur->rgb[0] = r, cur->rgb[1] = g, cur->rgb[2] = b;
}

void PType_Grow(int32_t s0, int32_t s1, int32_t step)
{
    if (!cur) return;
    cur->scale0 = s0;
    cur->scale1 = s1;
    cur->scale_step = step;
    cur->flags = (uint8_t)((cur->flags & 0xe7) | (step ? 8 : 0));
}

void PType_RandomScale(int32_t lo, int32_t hi)
{
    if (!cur) return;
    cur->flags &= 0xf7;
    cur->scale_step = 0;
    cur->scale0 = lo;
    if (lo < hi) {
        cur->scale1 = hi - lo;
        cur->flags |= 0x10;
    } else {
        cur->scale1 = 0;
        cur->flags &= 0xef;
    }
}

void PType_Spin(int16_t spin)
{
    if (!cur) return;
    cur->spin = spin;
    cur->flags = (uint8_t)((cur->flags & 0xdf) | (spin ? 0x20 : 0));
}

void PType_Spread(int32_t x, int32_t y, int32_t z, bool along)
{
    if (!cur) return;
    cur->spread[0] = x, cur->spread[1] = y, cur->spread[2] = z;
    cur->flags2 = (uint8_t)((cur->flags2 & 0xfe) | (along ? 1 : 0));
}

void PType_Funcs(PFunc draw, PFunc step, PFunc post, PFunc spawn, int32_t user)
{
    if (!cur) return;
    cur->post = post;
    cur->spawn = spawn;
    cur->user = user;
    cur->step = step;
    cur->draw = draw;
}

/* ---- the emitter ---- */

/* rand() / 32767 (a float constant, 0x5112f0) times 65536 (0x5112e0) rounded by fistp; the products
   are exact in a double */
static int32_t rand_unit(void)
{
    return (int32_t)nearbyint((double)crt_rand() * (double)(float)(1.0 / 32767.0) * 65536.0);
}

/* -0x8000 less the same times -65536 (0x5112e8): from -0x8000 to 0x8000 */
static int32_t rand_spread(void)
{
    return -0x8000 - (int32_t)nearbyint((double)crt_rand() * (double)(float)(1.0 / 32767.0) * -65536.0);
}

static void add_scaled(int32_t v[3], const int32_t axis[3], int32_t k)
{
    for (int i = 0; i < 3; i++) v[i] += fx_mul(axis[i], k);
}

void Particle_Emit(int type, const int32_t pos[3], const int32_t vel[3], int32_t ground, const int32_t *base,
                   const uint8_t *rgb, uint8_t shade, void *arg, uint8_t passes)
{
    PType *t = PType_Get(type);
    if (!t || !(t->flags & 1)) return;   /* (out of range the original reads through NULL) */
    /* the spread's frame: d along the velocity, u the up (or with a steep d the horizontal) made
       square to it, w = d x u; only with both x and z of the velocity above 0xa0 */
    bool frame = false;
    int32_t d[3], u[3], w[3];
    if ((t->flags2 & 1) && abs(vel[0]) > 0xa0 && abs(vel[2]) > 0xa0) {
        memcpy(d, vel, sizeof d);
        Vec_Unit(d);
        if (!d[0] && !d[1] && !d[2]) {
            u[0] = 0, u[1] = 0x10000, u[2] = 0;
        } else if (d[1] > 0x8000) {
            u[0] = d[0], u[1] = 0, u[2] = d[2];
            Vec_Unit(u);
        } else {
            u[0] = 0, u[1] = 0x10000, u[2] = 0;
        }
        int32_t dot = fx_mul(u[0], d[0]) + fx_mul(u[1], d[1]) + fx_mul(u[2], d[2]);
        for (int i = 0; i < 3; i++) u[i] -= fx_mul(d[i], dot);
        Vec_Unit(u);
        w[0] = fx_mul(d[1], u[2]) - fx_mul(d[2], u[1]);
        w[1] = fx_mul(d[2], u[0]) - fx_mul(d[0], u[2]);
        w[2] = fx_mul(d[0], u[1]) - fx_mul(d[1], u[0]);
        Vec_Unit(w);
        frame = true;
    }
    /* a free slot from the cursor on (the cursor left on it), else the oldest by its type's life */
    int pick = cursor, oldest = 0;
    Particle *p = NULL;
    for (int k = 0; k < nparts; k++, cursor++) {
        if (cursor >= nparts) cursor = 0;
        if (!parts[cursor].alive) {
            p = &parts[cursor];
            break;
        }
        int32_t age = parts[cursor].type->life - parts[cursor].life;
        if (age > oldest) oldest = age, pick = cursor;
    }
    if (!p) {
        if (pick >= nalloc) return;   /* (the original writes past its table) */
        p = &parts[pick];
    }
    int32_t s0 = rand_spread(), s1 = rand_spread(), s2 = rand_spread();
    memcpy(p->vel, vel, sizeof p->vel);
    memcpy(p->work, pos, sizeof p->work);
    memcpy(p->prev, p->work, sizeof p->prev);
    memcpy(p->drawn, p->work, sizeof p->drawn);
    memcpy(p->pos, p->work, sizeof p->pos);
    p->ground = ground;
    p->base = base;
    if (!(t->flags2 & 1)) {
        p->vel[0] += fx_mul(s0, t->spread[0]);
        p->vel[1] += fx_mul(s1, t->spread[1]);
        p->vel[2] += fx_mul(s2, t->spread[2]);
    } else if (frame) {
        if (t->spread[0]) add_scaled(p->vel, w, fx_mul(s0, t->spread[0]));
        if (t->spread[1]) add_scaled(p->vel, u, fx_mul(s1, t->spread[1]));
        if (t->spread[2]) add_scaled(p->vel, d, fx_mul(s2, t->spread[2]));
    } else {
        /* without the frame the original spreads y by z's and z by y's, both with the third draw */
        p->vel[0] += fx_mul(s0, t->spread[0]);
        p->vel[1] += fx_mul(s2, t->spread[2]);
        p->vel[2] += fx_mul(s2, t->spread[1]);
    }
    p->type = t;
    p->life = t->life;
    p->alpha = p->alpha_drawn = p->alpha_prev = t->alpha0;
    if (t->flags & 0x10) p->scale = t->scale0 + fx_mul(t->scale1, rand_unit());
    else p->scale = t->scale0;
    memcpy(p->rgb, rgb ? rgb : t->rgb, 3);
    if (t->flags2 & 2) p->shade = shade;
    p->tex = t->tex;
    p->passes = passes;
    p->alive = 1;
    p->rest = 0;
    p->spin = 0;
    if (t->spawn) t->spawn(p, t, arg);
}

/* ---- the step, the pose, the draw ---- */

void Particles_Step(void *camera)
{
    for (int i = 0; i < nparts; i++) {
        Particle *p = &parts[i];
        if (!p->alive) continue;
        PType *t = p->type;
        memcpy(p->prev, p->pos, sizeof p->prev);
        p->life -= 0x10000;
        if (p->life < 1) {
            Particle_Kill(p);
            continue;
        }
        if (t->step) {
            t->step(p, t, camera);
        } else {
            if (t->drag > 0) {
                int32_t k = 0x10000 - fx_mul(t->drag, 0x290);
                for (int a = 0; a < 3; a++) {
                    int32_t wv = fx_mul(wind[a], 0x1999);
                    p->vel[a] = fx_mul(p->vel[a] - wv, k) + wv;
                }
            }
            p->vel[1] -= t->gravity;
            p->work[0] += p->vel[0];
            p->work[1] += p->vel[1] + t->gravity / 2;
            p->work[2] += p->vel[2];
            if (t->flags & 0x40) {
                p->alpha_prev = p->alpha;
                uint32_t a = p->alpha;
                if (t->alpha0 < t->alpha1) {
                    a += t->alpha_step;
                    if (a > t->alpha1) a = t->alpha1;
                } else if (t->alpha1 < t->alpha0) {
                    a -= t->alpha_step;
                    if ((int32_t)a < (int32_t)t->alpha1) a = t->alpha1;
                }
                p->alpha = (uint8_t)a;
                if ((t->flags & 4) && a == t->alpha1) {
                    Particle_Kill(p);
                    continue;
                }
            }
            if (t->flags & 8) {
                int32_t s = p->scale;
                if (t->scale0 < t->scale1) {
                    s += t->scale_step;
                    if (s > t->scale1) s = t->scale1;
                } else if (t->scale1 < t->scale0) {
                    s -= t->scale_step;
                    if (s < t->scale1) s = t->scale1;
                }
                p->scale = s;
            }
            if (t->flags & 0x20) {
                int16_t s = (int16_t)(t->spin + p->spin);
                if (s > 0x1000) s -= 0x1000;
                else if (s < 0) s += 0x1000;
                p->spin = s;
            }
            if ((t->flags & 2) && p->work[1] < p->ground) {
                Particle_Kill(p);
                continue;
            }
            if (t->flags & 0x80) {
                int32_t keep = 0x10000 - t->friction;
                if (!p->rest) {
                    if (p->work[1] < p->ground) {
                        p->vel[1] = -fx_mul(p->vel[1], t->bounce);
                        p->work[1] = p->ground;
                        p->vel[0] = fx_mul(p->vel[0], keep);
                        p->vel[2] = fx_mul(p->vel[2], keep);
                        if (p->vel[1] < 0x1999) {
                            p->vel[1] = 0;
                            p->rest = 1;
                        }
                    }
                } else {
                    p->work[1] = p->ground;
                    p->vel[0] = fx_mul(p->vel[0], keep);
                    p->vel[2] = fx_mul(p->vel[2], keep);
                }
            }
            memcpy(p->pos, p->work, sizeof p->pos);
        }
        if (t->post) t->post(p, t, camera);
    }
}

void Particles_Interp(int32_t frac)
{
    for (int i = 0; i < nparts; i++) {
        Particle *p = &parts[i];
        if (!p->alive) continue;
        for (int a = 0; a < 3; a++) p->drawn[a] = p->prev[a] + fx_mul(p->pos[a] - p->prev[a], frac);
        int32_t da = (int32_t)(((uint32_t)p->alpha - p->alpha_prev) * 0x10000u);
        p->alpha_drawn = (uint8_t)((p->alpha_prev * 0x10000 + fx_mul(da, frac)) >> 16);
    }
}

void Particles_Draw(const Obj *camera, uint32_t pass)
{
    for (int i = 0; i < nparts; i++) {
        Particle *p = &parts[i];
        if (!p->alive || !(p->passes & (1u << (pass & 0x1f)))) continue;
        PType *t = p->type;
        if (t->draw) {
            t->draw(p, t, (void *)camera);
            continue;
        }
        Tex *tex;
        if (!t->anim) {
            tex = p->tex;
        } else {
            int f = 0;
            int32_t age = t->life - p->life;
            if (age > t->anim_delay && t->anim_time > 0) {
                f = (int32_t)(((int64_t)(age - t->anim_delay) << 16) / t->anim_time) >> 16;
                /* the low 16 bits of the particle's heap address */
                if (t->flags2 & 8) f += (int)((Particle_Addr(i) & 0xffff) % (uint32_t)t->nanim);
                if (f >= t->nanim && !(t->flags2 & 4)) {
                    tex = t->anim[t->nanim - 1];
                    goto have;
                }
                f %= t->nanim;
            }
            tex = t->anim[f];
        }
    have:
        if (!tex) continue;
        Sprite3d s;
        int32_t k = p->scale;
        for (int e = 0; e < 4; e++) s.e[e] = k == 0x10000 ? t->e[e] : fx_mul(t->e[e], k);
        s.angle = (t->flags & 0x20) ? (uint16_t)p->spin : 0;
        memcpy(s.pos, p->drawn, sizeof s.pos);
        if (p->base)
            for (int a = 0; a < 3; a++) s.pos[a] += p->base[a];
        s.flags = (uint8_t)(t->flags2 & 2);
        s.rgba[0] = p->rgb[0], s.rgba[1] = p->rgb[1], s.rgba[2] = p->rgb[2];
        s.rgba[3] = p->alpha_drawn;
        s.shade = p->shade;
        Sprite3d_Add(&s, tex);
    }
}
