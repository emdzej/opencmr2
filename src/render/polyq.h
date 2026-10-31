/* The world-space triangle queues (docs/re/render.md#triangle-queues): textured triangles in four
   buckets chosen by a flag bit (8, 0x10, 0x20, 0x40), queued by the effects during a frame and drawn by
   World_Render through one shared vertex buffer of 0x1800 FVF 0x2d2 vertices, in runs of the same
   texture and flags. */
#pragma once
#include "render/tex.h"
#include <stdint.h>

/* A queued corner as the callers give it: position and texture coordinates 16.16, colour R, G, B, A. */
typedef struct { int32_t x, y, z; uint8_t rgba[4]; int32_t u, v; } PolyVert;

/* Flag bits besides the bucket: 1 no z test, 2 no z write, 4 no culling. */
enum { POLY_NOZTEST = 1, POLY_NOZWRITE = 2, POLY_NOCULL = 4 };

void Poly_Init(void);   /* the queue part of Tri_Init 0x4bb610: only the 8 and 0x10 buckets emptied */
/* 0x4bba00: one triangle from three corners (colours made D3D ARGB, specular 0xff000000; the normals and
   second coordinates keep what the record held). */
void Poly_Add(const PolyVert *a, const PolyVert *b, const PolyVert *c, Tex *tex, uint32_t flags);
/* 0x4bbc60: one triangle of three ready 48-byte vertices. */
void Poly_AddRaw(const void *verts, Tex *tex, uint32_t flags);
/* 0x4bbd80: the bucket of the flags (8, 0x10, 0x20, else 0x40), emptied; then texture 0 off, z on,
   the default cull, and the cached blends forgotten. */
void Poly_Flush(uint32_t bucket);
void Poly_CreateVB(void);   /* the 0x338 buffer of 0x4b1980 */
void Poly_FreeVB(void);
