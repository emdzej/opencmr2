/* Textures (docs/re/render.md#textures): the original's table of 2,048 texture records
   (PTR_DAT_00520b78 + 900), their creation from archives or disk, binding to a stage and the stage
   blending each texture's blend type selects. Surfaces are the device's RGBA8 textures (d3d7.h). */
#pragma once
#include "bfl.h"
#include "render/d3d7.h"
#include <stdbool.h>
#include <stdint.h>

enum { TEX_MAX = 0x800 };

/* A texture record (0x130 bytes in the original). */
typedef struct Tex {
    uint16_t index;          /* +0x00: its slot (a clone keeps the original's) */
    char name[0x106];        /* +0x0e: the path it was made from (written with sprintf, as a format) */
    D3dTexture *surface;     /* +0x114 */
    int16_t width, height;   /* +0x120, +0x122: the surface's size (Tex_CreateSurface 0x4a7410) */
    uint32_t blend;          /* +0x118: blend type (Tex_SetStageBlend) */
    uint32_t flags;          /* +0x128: 1 alpha, 0x20 bump, 0x1000 from memory, 0x2000 DXT5, 0x4000 DXT1, ... */
    const BflSlot *archive;  /* +0x12c: the archive it came from */
} Tex;

extern Tex *g_tex[TEX_MAX];      /* PTR_DAT_00520b78 + 900 */
extern int g_tex_count;          /* DAT_0065fa20: records made (also C3d_Load's texture base) */

/* The display settings' option flags (PTR_DAT_00520b74 + 0x3bc), from the configuration: bits 0-1 the
   mip filter, 0x08 / 0x10 / 0x80 the reflection technique (0x10: bump environment mapping), 0x20, 0x40.
   Config_DefaultGraphics 0x405990 clears them all. */
extern uint32_t g_render_opts;

void Tex_SetBlend(Tex *t, uint32_t type);                     /* 0x4a3e20 */
void Tex_Bind(uint32_t stage, Tex *t);                        /* 0x4a4850 */
void Tex_SetStageBlend(uint32_t stage, const Tex *t);         /* 0x4a3e90 */
/* 0x4a48c0: a record for an image in memory (a member of an archive); name is the caller's path, whose
   extension a DDS overwrites with ".DDS" in place. */
Tex *Tex_CreateFromMemory(char *name, const uint8_t *data, uint32_t size, uint32_t flags);
Tex *Tex_CreateFromFile(char *path, uint32_t flags);          /* 0x4a49c0 */
int Tex_Clone(const Tex *t);                                  /* 0x4a4bd0: index of the copy, or -1 */
/* Bfl_FindTexture 0x4a9e60: <base>.dds, then <base>.tga in the archive, else the file from disk. */
Tex *Bfl_FindTexture(const BflSlot *archive, char *path, uint8_t *found, bool skip, uint32_t flags);
/* C3d_Textures 0x4b9910: a model's textures from its slot and path tables (kind 6: cars). dir is the
   prefix of the names (FUN_0040ed50's buffer). */
void C3d_Textures(const uint8_t *slots, uint8_t *paths, uint32_t ngroups, uint32_t kind,
                  const BflSlot *archive, const char *dir);
void Tex_FreeAll(void);   /* the port: every record and surface */
uint32_t Tex_LastBlend(void);   /* DAT_00520b18: the last blend type set up (-1: none) */
uint32_t Tex_Binds(void);       /* DAT_0065fa24: texture binds (statistics) */
void Tex_ForgetBlend(void);     /* 0x4a3dd0: the last blend type back to -1 */
