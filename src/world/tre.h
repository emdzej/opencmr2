/* The stage's tree shadows (<stage>.tre, docs/formats/stage.md#tre): one node per stage cell that has
   trees, each with an item per tree (matched to its scenery record) and a shadow mesh, drawn over the
   terrain by Stage_DrawOverlay. */
#pragma once
#include "c3d.h"
#include "world/section.h"
#include <stdbool.h>
#include <stdint.h>

extern uint8_t *g_tre_mesh[SEC_MAX];    /* 0x6dfd98: a cell's shadow mesh (0x120 record), or NULL */
extern int16_t g_tre_node[SEC_MAX];     /* 0x6e0204: a cell's node, -1 none */
extern C3d g_tre;                       /* the port: the file as a model (vertex and polygon bases 0) */
extern int g_tre_nodes, g_tre_items, g_tre_matched;
extern const uint8_t *g_tre_xhi;   /* DAT_006dfe10: the .xhi, kept for the relighting */
extern bool g_tre_lit;

/* Tre_Load 0x4b5620: the parse of data (NULL: no shadows, as at low scenery detail), the .xhi kept
   (0x6dfe10, not used yet). Needs the sections and the scenery. */
void Tre_Load(uint8_t *data, uint32_t size, const uint8_t *xhi);
/* Tre_Parse 0x4b4aa0 (see the format page). */
bool Tre_Parse(uint8_t *data, uint32_t size);
void Tre_Free(void);   /* the port; 0x4b5510 frees the original's */
/* Stage_DrawOverlay 0x49cf80: the listed cells' shadow meshes, identity world, z writes off. */
void Stage_DrawOverlay(void);
