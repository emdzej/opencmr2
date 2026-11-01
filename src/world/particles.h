/* The particles (0x4af940-0x4b1140, docs/re/world.md#particles): up to 32 types and 400 particles a
   race, each particle a camera-facing sprite (Sprite3d) moved once a step by its type's gravity, drag,
   bounce, fade, growth and spin, drawn at the pose fraction between its last two steps. The effects
   (dust, gravel, exhaust smoke, splashes, sparks, paint and glass chips) are types defined by
   Effects_Types 0x45a170 and emitted by the effects' code. In the original Ghidra project 0x4aff80 was
   called Skids_Reset: these are not the skid marks. */
#pragma once
#include "render/tex.h"
#include "world/obj.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct PType PType;
typedef struct Particle Particle;
/* the types' callbacks: the particle, its type and the caller's argument (the step's camera, the
   draw's camera, the emitter's eighth argument) */
typedef void (*PFunc)(Particle *p, PType *t, void *arg);

/* A type (0x70 bytes at 0x6a2cd8 + 0x70 n). */
struct PType {
    int32_t life;               /* +0x00: the steps it lives (16.16) */
    int32_t spread[3];          /* +0x04: the velocity's random spread on each axis */
    int32_t gravity;            /* +0x10: taken from the y velocity each step */
    int32_t drag;               /* +0x14: towards the wind, times 0x290 a step */
    int32_t bounce;             /* +0x18: the y velocity kept by a bounce */
    int32_t friction;           /* +0x1c: the x and z velocity lost by a bounce */
    int32_t scale0, scale1;     /* +0x20, +0x24: the scale at the start and the end (a random range
                                   with flag 0x10: the start and its spread) */
    int32_t scale_step;         /* +0x28 */
    int16_t spin;               /* +0x2c: added to the turn each step (4,096 a turn) */
    uint8_t alpha0, alpha1;     /* +0x2e, +0x2f: the alpha at the start and the end */
    uint8_t alpha_step;         /* +0x30 */
    uint8_t rgb[3];             /* +0x31 */
    /* +0x35: 1 defined, 2 dies below its ground, 4 dies when its fade ends, 8 grows, 0x10 a random
       scale, 0x20 spins, 0x40 fades, 0x80 bounces on its ground */
    uint8_t flags;
    /* +0x36: 1 the spread along the velocity's frame, 2 lit (the emitter's shade), 4 the animation
       loops, 8 the animation starts at a frame of the particle's address */
    uint8_t flags2;
    Tex *tex;                   /* +0x38 */
    int32_t e[4];               /* +0x3c: the sprite's extents (Sprite3d's e) */
    Tex **anim;                 /* +0x4c: the animation's frames (NULL: tex) */
    int32_t nanim;              /* +0x50 */
    int32_t anim_delay;         /* +0x54: the age the animation starts at */
    int32_t anim_time;          /* +0x58: the age a frame is shown */
    PFunc draw;                 /* +0x5c: in place of the sprite */
    PFunc step;                 /* +0x60: in place of the motion */
    PFunc post;                 /* +0x64: after the step */
    PFunc spawn;                /* +0x68: after the emitter */
    int32_t user;               /* +0x6c: its fifth callback argument, unread by the engine */
};

/* A particle (0x68 bytes at 0x6a2cdc + 0x68 n). */
struct Particle {
    PType *type;                /* +0x00 */
    int32_t vel[3];             /* +0x04 */
    int32_t prev[3];            /* +0x10: the position a step ago */
    int32_t drawn[3];           /* +0x1c: between prev and pos at the pose fraction */
    int32_t pos[3];             /* +0x28 */
    int32_t work[3];            /* +0x34: the position as the step moves it */
    const int32_t *base;        /* +0x40: added to the drawn position (the emitter's fifth argument:
                                   a record whose +0x30 is the vector) */
    int32_t life;               /* +0x44: the steps left */
    int32_t ground;             /* +0x48: the height it dies below or bounces on */
    int32_t scale;              /* +0x4c */
    int16_t spin;               /* +0x50 */
    uint8_t alpha_prev;         /* +0x52 */
    uint8_t alpha_drawn;        /* +0x53 */
    uint8_t shade;              /* +0x54: the sprite's shade when its type is lit */
    uint8_t passes;             /* +0x55: bit n drawn in pass n */
    uint8_t alpha;              /* +0x56 */
    uint8_t alive;              /* +0x57 */
    uint8_t rest;               /* +0x58: bounced to a stop */
    uint8_t rgb[3];             /* +0x59 */
    Tex *tex;                   /* +0x60: the type's when emitted */
};

void Particles_Alloc(int ntypes, int nparts);   /* 0x4b0060 */
void Particles_Free(void);                      /* 0x4b0010 */
void Particles_SetCount(int n);                 /* 0x4b1140: the particles in use (up to the allocation) */
void Particles_KillAll(void);                   /* 0x4aff80 */
void Particle_Kill(Particle *p);                /* 0x4b0100 */

/* Defining a type: begin (its fields reset, but not the scales, the spin and the extents), set,
   end. */
void PType_Begin(int n);                        /* 0x4af940 */
void PType_Copy(int from);                      /* 0x4afb20: all of a defined type's fields */
void PType_End(void);                           /* 0x4aff60: defined */
void PType_Tex(Tex *tex, int32_t e0, int32_t e1, int32_t e2, int32_t e3);   /* 0x4afb70 */
void PType_Anim(Tex **frames, int32_t n, int32_t delay, int32_t time, bool loop, bool random_start,
                int32_t e0, int32_t e1, int32_t e2, int32_t e3);            /* 0x4afbc0 */
void PType_Motion(int32_t life, int32_t gravity, int32_t drag, int32_t bounce, int32_t friction,
                  bool bounces, bool dies_below);                           /* 0x4afc70 */
void PType_Fade(uint8_t a0, uint8_t a1, uint8_t step, bool dies_at_end);    /* 0x4afcf0 */
void PType_Colour(uint8_t r, uint8_t g, uint8_t b, bool lit);               /* 0x4afd60 */
void PType_Grow(int32_t s0, int32_t s1, int32_t step);                      /* 0x4afdb0 */
void PType_RandomScale(int32_t lo, int32_t hi);                             /* 0x4afe10 */
void PType_Spin(int16_t spin);                                              /* 0x4afe80 */
void PType_Spread(int32_t x, int32_t y, int32_t z, bool along);             /* 0x4afec0 */
void PType_Funcs(PFunc draw, PFunc step, PFunc post, PFunc spawn, int32_t user);   /* 0x4aff10 */

/* 0x4b07a0: a particle of type at pos with the velocity vel (and the type's random spread), ground
   its height, base a record whose +0x30 vector is added when drawn, rgb its colour (NULL: the
   type's), shade the lit sprite's shade, arg the spawn callback's argument, passes the drawing
   passes. A free slot from the cursor on, else the oldest particle. */
void Particle_Emit(int type, const int32_t pos[3], const int32_t vel[3], int32_t ground, const int32_t *base,
                   const uint8_t *rgb, uint8_t shade, void *arg, uint8_t passes);
void Particles_Step(void *camera);              /* 0x4b0110 */
void Particles_Interp(int32_t frac);            /* 0x4b06a0 */
void Particles_Draw(const Obj *camera, uint32_t pass);   /* 0x4b0480 */

int Particles_Alive(void);              /* the port: the particles alive */
PType *PType_Get(int n);                /* the port: type n (NULL out of range) */
Particle *Particle_Get(int n);          /* the port */
