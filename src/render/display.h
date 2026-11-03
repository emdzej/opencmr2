/* The display adapter (docs/re/render.md#display): the modes DirectDraw enumerates for the chosen
   device, kept at their original addresses (0x6631f8: width, height and bits, 12 bytes each; the count
   0x663b28, the current one 0x663b2c), and the device made on one of them. */
#pragma once
#include <stdbool.h>

/* 0x4a78a0: the modes listed again, the device made on the one asked for (640 x 480 at 32 bits when it
   is not listed), the render states and the lights set; false without a device. */
bool Disp_Open(int width, int height, int bpp);
int Disp_Modes(void);                                  /* 0x4a8f10 */
int Disp_CurrentMode(void);                            /* 0x4a8eb0 */
void Disp_Mode(int i, int *width, int *height, int *bpp);   /* 0x4a8f20 */
bool Disp_HasMode(int width, int height, int bpp);     /* 0x4a8f60 */
/* 0x4b7560, 0x4b7590: a texture of n pixels across (down) within the device's caps */
bool Caps_TexWidth(int n);
bool Caps_TexHeight(int n);
/* the original's test for its 1,024 art: the settings' width over 1,023 (0x405c10) and a 1,024 texture
   in the caps both ways */
bool Disp_HiRes(void);
