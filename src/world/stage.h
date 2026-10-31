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
/* After Section_Init: the tree shadows (Tre_Load) and the .xhi (stage: the lower-case id, "fra02"). */
void Stage_LoadTre(const char *stage, bool detail);
extern C3d g_stage_obj, g_stage_sht;   /* TEMP.OBJ (scenery), TEMP.SHT */
bool Stage_LoadObj(void);              /* 0x472830 */
bool Stage_LoadSht(void);              /* 0x472870 */
void Stage_LoadScenery(void);          /* 0x471af0 */
/* where TEMP.OBJ's and TEMP.SHT's objects and scenery records start in the tables */
extern int g_obj_first, g_obj_scenery, g_sht_first, g_sht_scenery;
extern Obj *g_sky, *g_cloud, *g_cloud_top, *g_ground;
/* Stage_LoadSkyGround 0x46f060: TEMP.SKY, the clouds for the stage's conditions (Clouds\Pc or PcLow),
   TEMP.GRO, all under the world. */
void Stage_LoadSkyGround(int cond0, int cond1, bool low);
void Sky_Follow(const int32_t pos[3]);   /* 0x46f330 (for a position) */
