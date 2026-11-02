/* The stage's sections (docs/re/world.md#sections): the stage mesh's 0x88-byte section records form a
   grid of square cells, each with its mesh, the objects in it and its bounding rectangle; each frame the
   cells in the camera's view are listed and drawn. */
#pragma once
#include "c3d.h"
#include "world/obj.h"
#include <stdbool.h>
#include <stdint.h>

enum { SEC_MAX = 0x1000 };

/* A scenery record (a second-kind mesh's 0xa0-byte extra: TEMP.OBJ, TEMP.SHT). */
typedef struct Scenery {
    uint8_t *rec;
    const C3d *model;
    int32_t pos[3];            /* +0x00 */
    uint8_t *mesh;             /* +0x0c (mesh flag 2: a billboard turned to the camera) */
    FxMat m;                   /* +0x18: its placement (rotation and position, 16.16) */
    D3DMATRIX f;               /* +0x58: a billboard's world matrix, made each frame */
    struct Scenery *next;      /* +0x98: the next in its cell */
} Scenery;

enum { SCENERY_MAX = 0x4000 };
extern Scenery *g_scenery[SCENERY_MAX];   /* 0x669364 */
extern int g_nscenery;                     /* DAT_0067f228 */

typedef struct {
    uint8_t *rec;              /* the file record */
    int32_t x, y, z;           /* +0x00: the cell's centre (16.16) */
    uint8_t *mesh;             /* +0x10: its stage mesh, or NULL */
    Scenery *scenery;          /* +0x14: the scenery in it */
    int32_t nscenery;          /* +0x18 */
    Obj *objs;                 /* +0x1c: the objects in it (linked through section_next) */
    int32_t nobjs;             /* +0x20 */
    int32_t nwalls;            /* +0x24: the fin.dat walls in it */
    struct Wall *walls;        /* +0x28 (linked through next) */
    int32_t corner[8];         /* +0x5c: x, z of four corners of the mesh's bounding rectangle */
    uint32_t dist2;            /* +0x7c: squared distance to the camera, when listed */
    int32_t x80, x84;          /* +0x80, +0x84: set to 0 and 1 when listed */
} Section;

extern Section g_sec[SEC_MAX];    /* 0x71f608 (pointers there) */
extern int g_nsec;                /* DAT_0072d568 */
extern bool g_sec_ready;          /* DAT_0072d56c: set by Section_Init, cleared when they are freed */
extern int g_sec_row;             /* DAT_0071f600: cells in a row */
extern int g_sec_rows;            /* DAT_0072d458 */
extern int32_t g_sec_size, g_sec_half;   /* DAT_0072d24c, DAT_0072d248 */
extern uint16_t g_visible[SEC_MAX];      /* 0x6ed5f0 */
extern int g_nvisible;                   /* DAT_0072d570 */

/* The section part of C3d_Fixup 0x4b93c0 (it registers the records and their meshes). */
void Section_Register(const C3d *m);
/* Section_Init 0x4b8270: the grid (cell size from the first two cells, its half and reciprocal, the
   bounds, the row length from the first cells sharing a z), the objects under world placed in their
   cells (Section_LinkObjects 0x4b8450), the corners (Section_Corners 0x4b9170). */
void Section_Init(Obj *world);
int Section_Find(const int32_t pos[3]);    /* 0x4b85f0 */
bool Section_Visible(int i);               /* 0x4b7da0 */
/* Section_BuildVisible 0x4b7de0: the cells whose corners fall in the camera's view wedge, and the nine
   around the camera's cell, within the far plane (Proj_Far). */
void Section_BuildVisible(const Obj *camera);
/* Stage_DrawTerrain 0x49cec0: every listed cell's mesh with the identity world matrix. */
void Stage_DrawTerrain(const C3d *m);

/* The scenery part of C3d_Fixup: a model's second-kind meshes' records into the table. */
void Scenery_Register(const C3d *m);
void Scenery_FreeAll(void);   /* the port */
/* The billboard turn (0x597cc0, made in World_Render 0x49d3f0 from the camera's forward x and z). */
void Billboard_SetCamera(const Obj *camera);
/* Section_DrawBillboards 0x49d040: the listed cells' scenery; billboards get their matrix and distance
   and are queued (FUN_0049cb50), the rest drawn as they are (world space, the current world matrix). */
void Section_DrawBillboards(const Obj *camera);
/* 0x49cc50: the queued billboards far to near (qsort on the distance in their mesh, +0x114), blended
   within 200 units. */
void Billboard_DrawQueued(void);
/* Obj_UpdateSection 0x4b8690: the cell of the object's world position (searched again only when it
   has left its cell), up to three neighbouring cells within 4.5 units of an edge, and the move between
   the cells' lists. */
void Obj_UpdateSection(Obj *o);
void Section_Unlink(Obj *o);   /* 0x4b8aa0 */
void Section_Link(Obj *o);     /* 0x4b8b10 */
/* Section_DrawObjects 0x49ce40: the listed cells' objects (those with +0x180 clear), z-buffer off. */
void Section_DrawObjects(uint32_t pass);
