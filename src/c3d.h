/* PP_F models (docs/formats/c3d.md): cars, stage meshes, sky, ground, scenery. The original relocates the
   file's offsets into 32-bit pointers in place; the port keeps the offsets, checks every one at load, and
   resolves them at use with C3d_Ref (the C3d_Reloc 0x4b98f0 rule: -1 is NULL, else base + offset). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    C3D_OBJECT = 0x18c, C3D_MESH = 0x120, C3D_MESH2X = 0xa0, C3D_SECTION = 0x88, C3D_R1C = 0x1c,
    C3D_POLY = 0x4c, C3D_VERTEX = 0x30, C3D_VX = 4, C3D_R14 = 0x14, C3D_R5C = 0x5c, C3D_SLOT = 24,
    C3D_PATH = 260,
};

typedef struct {
    uint8_t *data;
    size_t size;
    uint32_t kind;                       /* header +0x04 */
    uint32_t n10, nvert, npoly;          /* +0x10, +0x14, +0x20 */
    uint16_t nobj, nmesh1, nmesh2, nsec; /* +0x18, +0x1a, +0x1c, +0x1e */
    uint16_t ntex, n26, n28;             /* +0x24, +0x26, +0x28 */
    /* array starts (file offsets), in file order */
    uint32_t objects, meshes, mesh2x, sections, r1c, polys, verts, vx, r14, r5c, slots, paths;
} C3d;

/* C3d_Load 0x4b9380 and the checks and rebasing of C3d_Fixup 0x4b93c0: the magic, the array sizes
   (their sum must reach the texture-slot table at +0x08), every relocated field inside its array, and the
   polygons' 10 texture indices rebased by tex_base (the running texture count, DAT_0065fa20; -1 stays).
   data is modified in place and must outlive m. Attaching root objects to a parent (the original's second
   argument), the vertex buffers and the global part tables belong to the renderer (steps 3 and 5). */
bool C3d_Load(C3d *m, uint8_t *data, size_t size, int32_t tex_base);

/* The record a relocated field points to: NULL for -1, else data + base + offset (checked at load). */
const uint8_t *C3d_Ref(const C3d *m, const uint8_t *field, uint32_t base);

const uint8_t *C3d_Object(const C3d *m, int i);
const uint8_t *C3d_Mesh(const C3d *m, int i);   /* first kind, then second kind */
const uint8_t *C3d_Poly(const C3d *m, int i);
const uint8_t *C3d_Vertex(const C3d *m, int i);
const char *C3d_TexPath(const C3d *m, int i);   /* the artist's path (260 bytes, NUL-terminated by the port) */
