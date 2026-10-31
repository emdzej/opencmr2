/* Loading a stage (docs/re/world.md#loading-a-stage), as far as ported: the archive slots and the stage
   mesh with its sections. */
#pragma once
#include "bfl.h"
#include "c3d.h"
#include "world/obj.h"
#include <stdbool.h>

extern BflSlot g_stage_bfl;   /* 0x541f38: <stage>Hi.bfl (FUN_004552c0) */
extern BflSlot g_track_tex;   /* 0x538858: <ccc>hit.bfl (FUN_0041f500 gives it when loaded) */
extern Obj g_world;           /* 0x536be0's object: the parent the stage models hang under */
extern C3d g_stage_mesh;      /* TEMP.C3D */

/* The port: mount the two archives (the front end does this before Stage_Load in the original). */
bool Stage_Mount(const char *stage_bfl, const char *tex_bfl);
/* Stage_LoadC3d 0x41fc50: TEMP.C3D from the stage archive through C3d_Load (with C3d_Textures from the
   track textures and the port's C3d_Build), its objects under the world, its sections registered. */
bool Stage_LoadC3d(void);
void Stage_Unload(void);   /* the port */
