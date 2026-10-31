# PP_F models (.c3d, .sky, .gro, .obj, .sht)

All 3D geometry is one format with the magic `PP_F`: car models (`Game/Cars/*.c3d`), the stage mesh
(`temp.c3d` in every stage archive), the sky dome (`temp.sky`), the ground/backdrop (`temp.gro`), the
scenery objects (`temp.obj`) and `temp.sht`. Files are wrapped in the [BFL layers](bfl.md).

A PP_F file is a **memory image**: it is used where it was loaded, and the loader turns the offsets inside
it into pointers by adding the base address of the array the field points into (`C3d_Reloc` 0x4b98f0:
`-1` becomes NULL, anything else gets the base added; the values are byte offsets from the start of
that array).
The port cannot keep 32-bit pointers inside a 64-bit-safe structure, so it keeps offsets and resolves them
at use (or reads the records into native structs). Either way the record sizes below are fixed by the
original layout.

`C3d_Load` 0x4b9380 checks the magic and calls `C3d_Fixup` 0x4b93c0. Its second argument is a parent
object: when given, the model's root objects (`+0x24` = `0xffff`) are appended to that object's children
(`Obj_Attach` 0x4ac200) instead of keeping their own parent field. The third goes to the texture loader.

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

Relocations done by `C3d_Fixup` (each into the named array):

| Record | Field | Into |
|---|---|---|
| object | +0x00 next sibling, +0x04 first child, +0x08 parent, +0x170 | objects |
| object | +0x0c | meshes |
| mesh (per part, below) | +0x0c vertices, +0x20 the 0x14 array, +0x24 polygons, +0x34 vertex extras | as named |
| mesh | +0x10c | the 0x5c array |
| mesh2 extra (0xa0) | +0x0c | the second-kind meshes; +0x98 the 0xa0 array |
| section (0x88) | +0x10 | meshes |
| 0x5c record | +0x58 | the 0x5c array (a list) |
| polygon | 10 texture indices at +0x04..+0x2b | rebased by the running texture count (`DAT_0065fa20`), `-1` stays |

A mesh has parts of 0x108 bytes from `+0x0c`, their count in the byte at `+0x113`; the fixup walks them
at that stride even past the 0x120-byte record, but the count is 1 in every file on the disc, so in
practice the part is the mesh. Per part the original then copies the vertices into a shared vertex buffer
(`Mesh_AddToVB` 0x4b1d00: buffers of 2,000 vertices, up to 100, "Failed to add shape to vertex buffer")
and groups the triangles into runs of the same texture (`Mesh_Batches` 0x4b1ac0). The parts are
registered in global tables (objects at 0x67f36c, meshes at 0x67b124 when the model has no sections,
second-kind extras at 0x669364, 0x5c records at 0x66f124, sections at 0x71f608), which the renderer walks.

The port keeps the offsets and resolves them at use; `C3d_Load` (`src/c3d.c`) checks every field above
against its array. All 1,211 PP_F files on the disc pass, and the resolved triangles hash the same in
the C reader and `tools/cmr2fmt.py`.

## Object (0x18c bytes)

| Offset | Type | Field |
|---|---|---|
| 0x00, 0x04, 0x08 | offset | next sibling, first child, parent (objects; `-1` none) |
| 0x0c | offset | mesh (`-1`: a pure transform node, such as a car's root) |
| 0x24 | u16 | `0xffff` on root objects (see `C3d_Load`'s parent); set to `0xffff` by the fixup |
| 0x26 | u16 | the object's slot in the global table, written at load |
| 0x30 | u32 | an id (`0x1000`, `0x1001`, `0x64001002` seen) |
| 0x58 | i32[16] | matrix, 16.16 fixed point, row vectors (`v' = v M`, translation in the fourth row): object to parent |
| 0x98 | i32[16] | matrix, same form (equal to the first in the cars checked; probably the world matrix at run time) |
| 0xd8 | i32[16] | matrix, same form (identity in the cars checked) |
| 0x170 | offset | an object |

Car wheels show the convention: the left wheels have `-1.0` on the diagonal (mirrored) and their
translation is the hub position. Vertices are in the object's space; chaining the first matrix up the
parents places the parts (`tools/cmr2fmt.py obj`).

## Mesh (0x120 bytes)

| Offset | Type | Field |
|---|---|---|
| 0x00 | char[12] | part name (`01WhlLF01`, `05BonCam`, `06UnlitSim`, `07SemiTrans`; `c3dShp<n>` in stages) |
| 0x0c | offset | first vertex (and its 0x30-byte records follow) |
| 0x10 | u32 | vertex count |
| 0x14, 0x18 | u32 | vertex buffer number and first index in it (written at load) |
| 0x20 | offset | into the 0x14 array (`-1` in stages) |
| 0x24 | offset | first polygon |
| 0x28 | u32 | polygon count |
| 0x2c | u32 | 1 seen |
| 0x30 | u32 | flags (below; bit 18 cleared at load) |
| 0x34 | offset | first vertex extra |
| 0x38 | ptr[] | texture batches (built at load, `Mesh_Batches`), count at 0x100 |
| 0x10c | offset | into the 0x5c array |
| 0x112 | u8 | the part drawn (`Mesh_Draw` 0x49c940 takes part `+0x112` of `+0x113`) |
| 0x113 | u8 | part count (1) |

Flags, as `Mesh_Draw` reads them: `0x1` two-sided (cull off), `0x8` alpha-blended (when `0x5207b4` is
set), `0x20` keep the current lighting mode (else mode 2, or 3 with `0x40000`), `0x200` reflective (a
second pass with the reflection option), `0x2000` drawn polygon by polygon. Over all 468,590 meshes on
the disc: `0x20` is set on all but 1,540, `0x40` on all, `0x800` on nearly all; `0x200` on 721, `0x2000`
on 951; `0x40000` on none (it is set at run time). Every mesh has one part, at most 32 texture runs (the
record has room for 50 batches) and at most 306 triangles in a run.

## Polygon (0x4c bytes)

Every polygon is a triangle.

| Offset | Type | Field |
|---|---|---|
| 0x00 | u32 | flags (`0x8002`, `0x8004` in cars); low 7 bits `0x70`, `0x71`, `0x73`, `0x74` make one draw variant wrap stage 0 instead of clamping |
| 0x04 | i32[10] | texture indices: +0x04 stage 0, +0x08 stage 1 (the batching uses these two), `-1` none |
| 0x2c | u32 | frame: which of the 10 texture indices is current (the polygon-by-polygon draw reads it; 0 on every polygon on the disc) |
| 0x30 | u32 | unknown (1) |
| 0x34 | u32[3] | ARGB colours, one per corner (`ff808080`) |
| 0x40 | u16[3] | vertex indices, relative to the mesh's first vertex |
| 0x46 | u16 | unknown |
| 0x48 | u32 | unknown (1) |

## Texture slots and paths

`C3d_Textures` 0x4b9910 loads the model's textures from the path table: it upper-cases each path in place
(`Text_ToFontCase`), keeps the part after the **first** backslash and formats a file name with a directory
prefix and `.tga` (`"%s%s.tga"`); kind 6 (cars) take another route first. The texture member is then
found by its base name as `<name>.dds`, then `<name>.tga` ([bfl.md](bfl.md#texture-lookup)).

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

- The remaining object fields (0x10-0x57, 0x118-0x16f, 0x174-0x18b), polygon flags, the second-kind
  meshes and their 0xa0 extras, the section record (0x88).
- Meaning of the 0x14, 0x1c and 0x5c arrays.
- How a car's kind-6 texture route differs.
- How the simulation's 16.16 fixed-point world maps onto the float model space (scale and axes).
