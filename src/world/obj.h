/* Scene objects (docs/re/world.md): the PP_F object records (0x18c bytes, docs/formats/c3d.md) linked
   into trees, their matrices brought up to date each frame and drawn per pass. The original works on the
   records in the loaded files, with 32-bit pointers; the port copies each record into an Obj with real
   pointers (links can cross models: a car's roots hang under another object) and keeps the record for
   the fields not yet understood. Offsets in the comments are the record's. */
#pragma once
#include "c3d.h"
#include "fixmath.h"
#include "render/d3d7.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct Obj {
    struct Obj *next, *child, *parent;   /* +0x00, +0x04, +0x08 */
    uint8_t *mesh;                       /* +0x0c: a mesh record (kind 0) */
    D3DLIGHT7 *light;                    /* +0x0c for kind 1: the light, its index at +0x68 */
    uint32_t light_index;
    const C3d *model;
    int16_t section;                     /* +0x24: the stage section it is in, -1 none */
    int16_t slot;                        /* +0x26: its place in the object table */
    int16_t near[3];                     /* +0x28: neighbouring sections */
    uint32_t type;                       /* +0x30: low byte 0xfd a node whose children keep their own matrix; bit 12 */
    FxMat rest;                          /* +0x58 */
    FxMat local;                         /* +0x98: object to parent */
    FxMat world;                         /* +0xd8: object to world */
    D3DMATRIX worldf;                    /* +0x118: the world matrix as Direct3D draws it */
    int32_t dist;                        /* +0x16c: its distance to the camera in x and z, for the sorted pass */
    struct Obj *section_next;            /* +0x170 */
    int32_t dirty;                       /* +0x174 */
    int32_t kind;                        /* +0x178: 0 mesh, 1 light, 2, 3 node; only 0-3 are updated */
    uint8_t mask;                        /* +0x17c: the passes it is drawn in (bit per pass) */
    int32_t sorted;                      /* +0x180: drawn in the sorted pass, far to near, not with its cell */
    int32_t inherit;                     /* +0x184: take the parent's world as it is */
    const uint8_t *rec;                  /* the file record */
} Obj;

enum { OBJ_MAX = 0x1000 };
extern Obj *g_objs[OBJ_MAX];   /* 0x67f36c */
extern int g_nobjs;            /* DAT_00683370 */

/* The object part of C3d_Fixup 0x4b93c0: the model's records as Objs, registered in the table. With a
   parent, the roots (+0x24 = -1) go under it (Obj_Attach 0x4ac200); every object's +0x24 is then -1.
   Returns the first object (the model's), NULL if it has none. */
Obj *C3d_Objects(const C3d *m, Obj *parent);
void Obj_FreeAll(void);   /* the port */
void Obj_Attach(Obj *o, Obj *parent);   /* 0x4ac200 */
bool Obj_Active(const Obj *o);          /* 0x4ac390 */
/* Obj_UpdateTree 0x4ac280: world matrices of the dirty objects (or those under a dirty parent) of a
   sibling list and below; Obj_Commit 0x4ad8d0: float world matrices and lights, dirty flags cleared. */
void Obj_UpdateTree(Obj *list);
void Obj_Commit(Obj *list);
void Obj_DrawList(Obj *list, uint32_t pass);   /* 0x49ca50 */
void Obj_Draw(Obj *o, uint32_t pass);          /* 0x49cad0 */
/* 0x49ce10: Obj_UpdateTree, Obj_Commit, View_Set(camera) */
void Obj_Frame(Obj *list, Obj *camera);
void Obj_SetPosition(Obj *o, const int32_t pos[3]);   /* 0x4ac480 */
/* View_Set 0x4ade00: the view as the inverse of the camera's world matrix, when the camera is dirty
   (and a counter at 0x6838d0 is below 5). */
void View_Set(Obj *camera);
