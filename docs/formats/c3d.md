# PP_F models (.c3d, .sky, .gro, .obj, .sht)

All 3D geometry is one format with the magic `PP_F`: car models (`Game/Cars/*.c3d`), the stage mesh
(`temp.c3d` in every stage archive), the sky dome (`temp.sky`), the ground/backdrop (`temp.gro`), the
scenery objects (`temp.obj`) and `temp.sht`. Files are wrapped in the [BFL layers](bfl.md).

A PP_F file is a **memory image**: it is used where it was loaded, and the loader turns the offsets inside
it into pointers by adding the base address (`C3d_Reloc` 0x4b98f0: `if (*field != -1) *field += base`).
The port cannot keep 32-bit pointers inside a 64-bit-safe structure, so it keeps offsets and resolves them
at use (or reads the records into native structs). Either way the record sizes below are fixed by the
original layout.

`C3d_Load` 0x4b9380 checks the magic and calls `C3d_Fixup` 0x4b93c0.

## Header (0x30 bytes)

| Offset | Type | Meaning |
|---|---|---|
| 0x00 | char[4] | `PP_F` |
| 0x04 | u32 | Kind/version: 6 for cars, 2 for `temp.c3d`, 5 for `.sky`/`.gro`, 4 for `.sht`, 0 for `.obj`. Passed on to the texture loader |
| 0x08 | u32 | Offset of the texture-slot table (= end of the record arrays) |
| 0x0c | u32 | Offset of the texture path table |
| 0x10 | u32 | Count of 0x14-byte records (equals the 0x4c count in cars) |
| 0x14 | u32 | Vertex count: one 0x30-byte record and one 4-byte record each |
| 0x18 | u16 | Object count (0x18c-byte records) |
| 0x1a | u16 | Mesh count, first kind (0x120-byte records) |
| 0x1c | u16 | Mesh count, second kind (0x120-byte records plus one 0xa0-byte record each) |
| 0x1e | u16 | Count of 0x88-byte records (360 in a stage: track sections; 0 in cars) |
| 0x20 | u32 | Count of 0x4c-byte records (polygons/strips; each has 10 texture indices, below) |
| 0x24 | u16 | Texture count |
| 0x26 | u16 | Count of 0x5c-byte records |
| 0x28 | u16 | Count of 0x1c-byte records |
| 0x2a | u16[3] | unused so far |

## Body

The arrays follow the header in this order; the start of each is the end of the previous one:

```
0x30            objects        n18 x 0x18c
                meshes         (n1a + n1c) x 0x120     each starts with its part name, e.g. "01LFWhlLowL"
                meshes2 extra  n1c x 0xa0
                sections       n1e x 0x88
                (0x1c)         n28 x 0x1c
                polygons       n20 x 0x4c
                vertices       n14 x 0x30
                vertex extra   n14 x 4
                (0x14)         n10 x 0x14
                (0x5c)         n26 x 0x5c
[+0x08]         texture slots  n24 x 24     name[12] ("Texture 0"), u32 flags, u32, u16 index, u16
[+0x0c]         texture paths  n24 x 260    the artist's path, e.g. U:\Cars2\Focus\CRes03\Hier\UsePC\CFFDBoDF
```

The sum of the array sizes from 0x30 equals the value at `+0x08` exactly in every file checked (cars of
every set, stage meshes), which confirms the record sizes. The texture paths are `MAX_PATH` (260) slots;
only the last path component matters: it is looked up in the mounted texture archive as `<name>.dds`, then
`<name>.tga` ([bfl.md](bfl.md#texture-lookup)).

Relocations done by `C3d_Fixup` (offsets into the named arrays):

- objects: four pointer fields into the object array itself, one into the meshes;
- meshes: per sub-record (0x108 bytes, count in the byte at mesh `+0x113`): pointers into the 0x1c array,
  the polygons, the vertices and the 4-byte vertex extras, then `FUN_004b1d00` builds the vertex buffer
  ("Failed to add shape to vertex buffer") and `FUN_004b1ac0` finishes it;
- polygons: 10 texture indices per record, rebased by the current texture base (`DAT_0065fa20`), `-1` = none;
- sections (`0x88`): one pointer into the meshes.

The loaded parts are registered in global tables (objects at 0x67f36c, meshes at 0x67b124, sections at
0x71f608), which the renderer walks.

## Vertex (0x30 bytes)

The vertex is already in the layout Direct3D 7 draws, flexible vertex format
`D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX2`:

| Offset | Type | Field |
|---|---|---|
| 0x00 | f32[3] | position |
| 0x0c | f32[3] | normal |
| 0x18 | u32 | diffuse, ARGB (`ff808080` is typical) |
| 0x1c | u32 | specular, ARGB |
| 0x20 | f32[2] | texture coordinates, stage 0 |
| 0x28 | f32[2] | texture coordinates, stage 1 |

The 4-byte per-vertex extra is a second colour (`00808080` typical), probably the prelit/shadow colour.

## Examples

| File | Kind | Objects | Meshes | Sections | Polygons | Vertices | Textures |
|---|---|---|---|---|---|---|---|
| `f99c3.c3d` (Focus, set c3) | 6 | 9 | 8 | 0 | 669 | 887 | 25 |
| `f99a1N.c3d` (Focus, a1 normal) | 6 | 15 | 14 | 0 | 935 | 1,166 | 27 |
| `f99a1L.c3d` | 6 | 5 | 4 | 0 | 288 | 344 | 12 |
| `fra02` `temp.c3d` | 2 | 0 | 148 | 360 | 12,769 | 23,244 | 54 |

## Open

- Field layout inside the object (0x18c), mesh (0x120) and polygon (0x4c) records. A polygon record
  begins with a flags word and vertex indices padded with `-1`, then three ARGB colours.
- Meaning of the 0x14, 0x1c and 0x5c arrays.
- How the simulation's 16.16 fixed-point world maps onto the float model space (scale and axes).
