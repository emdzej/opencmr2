/* The stage's collision mesh (<stage>.hpc, docs/formats/stage.md#hpc): triangles over a grid of
   256-unit cells, each either a list of triangles or 4 x 4 children, five levels deep. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool Hpc_Parse(const uint8_t *data, uint32_t size);   /* 0x490d50 */
void Hpc_Free(void);
const uint8_t *Hpc_Image(uint32_t *size);   /* for the differential tests: the file image parsed */
extern int g_hpc_verts, g_hpc_tris;
void Hpc_Triangle(int32_t out[9], int tri);              /* 0x490e00: three x, y, z */
uint32_t Hpc_Surface(int tri);
void Hpc_TriVerts(int tri, uint16_t idx[3]);             /* 0x491790's three vertex indices */                           /* the triangle's byte +6, low 7 bits */
/* 0x490f20: (x, z) inside the triangle (its x and z range, then the three edges, inclusive). */
bool Hpc_Inside(const int32_t p[3], int tri, const int32_t *t9);
/* 0x491550: the triangle under (x, z) whose mean y is nearest y (the first of equals); false off the
   grid or over no triangle. */
bool Hpc_Find(const int32_t p[3], int16_t *tri, int32_t y);
/* 0x4910f0: the height at (x, z) of the triangle's plane, its unit normal and surface; y as given for
   a vertical one. */
int32_t Hpc_Height(const int32_t p[3], int tri, int32_t y, int32_t normal[3], uint16_t *surface);
/* 0x490c90: the ground height under p, keeping *tri between calls (-1: search); y as given when
   there is none. */
int32_t Hpc_Ground(const int32_t p[3], int32_t normal[3], int16_t *tri, uint16_t *surface, int32_t y);
