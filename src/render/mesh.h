/* Model meshes on the device (docs/re/render.md#meshes): the shared vertex buffers, the texture batches
   and the draw functions. A mesh part is the 0x108-byte part of a PP_F mesh record (docs/formats/c3d.md),
   addressed from its record start: +0x0c vertices, +0x10 count, +0x14 vertex buffer, +0x18 first vertex
   in it, +0x24 polygons, +0x28 count, +0x30 flags, +0x38 batches (50), +0x100 batch count. The original
   stores batch pointers at +0x38; the port stores indices into its batch pool there. */
#pragma once
#include "c3d.h"
#include "render/d3d7.h"
#include <stdint.h>

enum { VB_POOL = 100, VB_VERTS = 2000, MESH_BATCHES = 50 };

typedef struct {
    int32_t tex, tex1;       /* +0x00, +0x04: the polygons' texture indices (stage 0 and 1) */
    uint32_t min, max;       /* +0x08, +0x0c: their vertex range */
    uint32_t count;          /* +0x10: indices */
    uint16_t *idx;           /* +0x14: rebased by min */
} Batch;

extern D3dVB *g_vb[VB_POOL];       /* PTR_DAT_00520b78 + 0x18 */
extern uint32_t g_vb_fill[VB_POOL];/* PTR_DAT_00520b78 + 0x1a8 */
extern uint32_t g_polys_drawn;     /* DAT_0059ce18 */

void VB_CreatePool(void);                                   /* the mesh part of 0x4b1980 */
void VB_FreePool(void);
/* Mesh_AddToVB 0x4b1d00 and Mesh_Batches 0x4b1ac0 for every part of every mesh, with C3d_Fixup's
   clearing of flag bit 18: the part of the fixup the port's C3d_Load leaves to the renderer. */
void C3d_Build(const C3d *m);
void Mesh_AddToVB(const C3d *m, uint8_t *part);             /* 0x4b1d00 */
void Mesh_Batches(const C3d *m, uint8_t *part);             /* 0x4b1ac0 */
void Mesh_UploadVerts(const C3d *m, uint8_t *part);         /* 0x4b2020 */
void Mesh_SetColour(const C3d *m, uint8_t *part, uint32_t argb);   /* 0x4b2090 */
/* 0x4b1ea0: every vertex's colour (+0x18) back from its polygons' corner colours (+0x34-+0x3c, red and
   blue swapped), then the upload */
void Mesh_RestoreColours(const C3d *m, uint8_t *part);
/* 0x49c440 / 0x49c4b0: the frame index (+0x2c, stage 1's +0x30) of the polygons whose flags match
   mask (its low 7 bits against flag bits 0-6, its bits 7-13 against flag bits 9-15); the first does
   nothing for a NULL part */
void Mesh_SetFrame(const C3d *m, uint8_t *part, uint32_t mask, uint32_t frame);
void Mesh_SetFrame1(const C3d *m, uint8_t *part, uint32_t mask, uint32_t frame);
/* Mesh_Draw 0x49c940: the part selected by +0x112, with the flags' cull, lighting and blending, by
   batches (batched; batched_tex10 also sets each texture's blend type to 10) or polygon by polygon,
   each polygon's texture from its frame index at +0x2c (clamp_flags: stage 0 addressing from the
   polygon flags too). */
void Mesh_Draw(const C3d *m, uint8_t *mesh, uint32_t batched, uint32_t clamp_flags, uint32_t batched_tex10);
