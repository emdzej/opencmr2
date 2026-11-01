/* The glows (0x4ae170-0x4af120, docs/re/world.md#glows): a pool of lights drawn each frame as a
   camera-facing sprite, with an optional shaft down to a plane and a pool of light on it. The start
   lights and the cars' lamps are glows. */
#pragma once
#include "render/tex.h"
#include "world/obj.h"
#include <stdbool.h>
#include <stdint.h>

/* A glow (0x5c bytes at 0x6a2a98 + 0x5c n). Kind 2 is seen only from in front (its direction towards
   the camera), 3 from both sides, others from anywhere. */
typedef struct {
    uint32_t kind;          /* +0x00: 0 a free slot */
    int32_t pos[3];         /* +0x04: in its parent's frame if it has one */
    int32_t dir[3];         /* +0x10 */
    int32_t at[3];          /* +0x1c: a point of the plane under it */
    int32_t normal[3];      /* +0x28: the plane's normal */
    int32_t w, h;           /* +0x34, +0x38: the sprite's half sizes */
    int32_t level;          /* +0x3c: its brightness; 0 not drawn */
    int32_t size;           /* +0x40: the pool's size, times w */
    int32_t shaft;          /* +0x44: the shaft's brightness (0: none) */
    Tex *tex;               /* +0x48: the sprite's and the pool's texture */
    Tex *shaft_tex;         /* +0x4c */
    uint8_t on;             /* +0x50 */
    uint8_t pool;           /* +0x51: the pool's brightness (0: none) */
    Obj *parent;            /* +0x54 */
} Glow;

void Glow_Alloc(int n);    /* 0x4ae170: the pool of n glows */
void Glow_Free(void);      /* 0x4ae200 */
void Glow_Clear(void);     /* 0x4ae260: every slot free */
/* 0x4ae2f0: a glow into the first free slot (NULL when the pool is used up), on. */
Glow *Glow_Add(uint32_t kind, const int32_t pos[3], const int32_t *dir, int32_t w, int32_t h, Tex *tex,
               Tex *shaft_tex, int32_t level, Obj *parent, uint8_t pool, int32_t size);
void Glow_Show(Glow *g, bool on);                                       /* 0x4ae3d0 */
void Glow_Level(Glow *g, int32_t level);                                /* 0x4ae3f0 */
void Glow_Move(Glow *g, const int32_t *pos, const int32_t *dir);        /* 0x4ae2a0 */
void Glow_Plane(Glow *g, const int32_t at[3], const int32_t n[3], int32_t shaft);   /* 0x4ae420 */
/* 0x4af120: every glow on with a level, its parent drawn in this pass: the sprite (Sprite3d), the
   shaft and the pool (the triangle queues). */
void Glow_Draw(const Obj *camera, uint32_t pass);
int Glow_Used(void);       /* DAT_006a2bc8 */
