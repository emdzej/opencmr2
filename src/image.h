/* Texture images (docs/formats/media.md#textures): TGA type 2 and DDS (DXT1/DXT5), decoded to RGBA8,
   rows top first. The original hands DDS data to DirectDraw as it is and converts TGA pixels into the
   surface's format while uploading; the port decodes both to RGBA8 and leaves the surface format to the
   renderer. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int bpp;            /* bytes per pixel: 3 or 4 */
    int width, height;
    const uint8_t *pixels;
} TgaInfo;

/* Tga_Header 0x4a6670: true for the TGAs the game accepts (see image.c). size bounds the pixels too. */
bool Tga_Header(const uint8_t *file, size_t size, TgaInfo *out);

/* RGBA8 (malloc'd, width*height*4) or NULL. *w, *h receive the size. */
uint8_t *Tga_Decode(const uint8_t *file, size_t size, int *w, int *h);
uint8_t *Dds_Decode(const uint8_t *file, size_t size, int *w, int *h);
/* Either, by the "DDS " magic, as Tex_Reload 0x4a5da0 picks. */
uint8_t *Image_Decode(const uint8_t *file, size_t size, int *w, int *h);
