/* The original's render-state layer (0x49d940-0x49e100): cached setters over the device and the start-up
   states. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The device capabilities the game reads at start (FUN_004b7210 from GetCaps) and branches on. The port
   emulates one card; the values are a hardware T&L card's (docs/re/render.md#caps). */
typedef struct {
    bool alpha_ref_switch;  /* DAT_006e0c14: blending off sets the alpha reference instead (Render_AlphaBlend) */
    bool zless_hsr;         /* DAT_006e0c34: the triangle raster caps' ZBUFFERLESSHSR (0x8000, a tiler
                               without a z-buffer: Render_ClearZ then does nothing) */
    bool fog_vertex;        /* DAT_006e0c40: vertex fog (raster caps FOGVERTEX) */
    bool fog_table;         /* DAT_006e0c44: table fog */
} RenderCaps;
extern RenderCaps g_caps;

void Render_SetClearColour(uint8_t r, uint8_t g, uint8_t b);   /* 0x49d940 */
void Render_ClearTarget(void);                                  /* 0x49d960 */
void Render_ClearZ(bool stencil);                               /* 0x49d9d0 */
void Render_SetCull(uint32_t mode);                             /* 0x49dc70 */
void Render_SetDefaultCull(uint32_t mode);                      /* 0x49dca0 */
uint32_t Render_DefaultCull(void);                              /* 0x49dcb0 */
void Render_AlphaBlend(uint32_t on);                            /* 0x49dcc0 */
void Render_ZEnable(uint32_t on);                               /* 0x49dd40 */
void Render_ClampStage0(uint32_t on);                           /* 0x49dd70 */
void Render_ZWrite(uint32_t on);                                /* 0x49ddc0 */
void Render_TexCoordIndex(uint32_t stage, uint32_t tci);        /* 0x49ddf0 */
void Render_InitStates(void);                                   /* 0x49df90 */
void Render_SetBlend(uint32_t src, uint32_t dst);               /* 0x4a3e40 */
void Render_ForgetBlend(void);                                  /* 0x4a3de0 */
void Render_Reset(void);    /* the port: forget the cached values (a new device) */
