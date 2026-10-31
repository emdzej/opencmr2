# Renderer

How `CMR2.exe` draws, and how the port does it. The original is fixed-function Direct3D 7 with hardware
transform and lighting; the device is `*(PTR_DAT_00520b78 + 4)`. Methods and call sites are listed in
[architecture](architecture.md#rendering).

## The original's layers

| Address | Name | What |
|---|---|---|
| 0x4b7210 | `Render_ReadCaps` | Reads the device caps (`GetCaps`) into flags at 0x6e0c0c-0x6e0c3c |
| 0x49df90 | `Render_InitStates` | The start-up states (below) |
| 0x49d940-0x49ddf0 | `Render_*` | Cached setters: cull, alpha blend, z enable, z write, stage 0 clamp, texture coordinate index; the clear colour and the two clears |
| 0x4a3e90 | `Tex_SetStageBlend` | A stage's colour and alpha operations from the texture's blend type (texture record `+0x118`) |
| 0x4b6f00 | `Light_SetMode` | Lighting modes: `LIGHTING`, `AMBIENT`, `COLORVERTEX`, `DIFFUSEMATERIALSOURCE`, lights on and off |
| 0x4b2e50, 0x4b2f80, 0x4b34d0-0x4b35b0, 0x4ad8d0 | | `SetMaterial` (once, 0x4b2e50) and `SetLight` / `LightEnable` |
| 0x4b2980 | `Render_EnvMapped` | Reflections: stage coordinates from the camera-space reflection vector (`TEXCOORDINDEX` 0x30001) through texture matrix 0 (3 components), with `NORMALIZENORMALS` and `LOCALVIEWER` on for the draw |
| 0x4bcaf0, 0x4bcbb0, 0x4bcc20 | `Fog_SetLinear`, `Fog_Off` | Vertex fog: table mode 0, `FOGVERTEXMODE` 3 (linear), start and end, colour |
| 0x49e1f0 | `Camera_Apply` | Clear, view and projection transforms, viewport |
| 0x4b1d00, 0x4b1ac0 | `Mesh_AddToVB`, `Mesh_Batches` | Model vertices into shared vertex buffers (2,000 vertices each, up to 100), triangles grouped by texture |
| 0x49c510-0x49c880, 0x4b2610 | | `DrawIndexedPrimitiveVB` of triangle lists |
| 0x4b1530 | `Render_DrawIndexed` | `DrawIndexedPrimitive` of FVF 0x2d2 vertices from arrays |

Vertex formats: models are FVF `0x2d2` (`XYZ|NORMAL|DIFFUSE|SPECULAR|TEX2`, 0x30 bytes, the PP_F
vertex as stored, [c3d.md](../formats/c3d.md#vertex-0x30-bytes)); 2D (HUD, front end, effects) is FVF
`0x1c4` (`XYZRHW|DIFFUSE|SPECULAR|TEX1`, 32 bytes) through `DrawPrimitive`.

### Start-up states

`Render_InitStates` sets fill solid, Gouraud shading, z func less-or-equal, alpha test on with
reference 1 and function greater-or-equal, alpha blending on, fog off, dithering on, specular off,
perspective correction on, clipping on, and the material sources: **colour vertex on, diffuse, specular
and ambient from the material, emissive from the vertex colour**. So the prelit vertex colours of the
stages (`ff808080` typical) are emitted as they are and the lights add to them. Then z enable and z
write on, the default cull mode (`0x5207f8`, initially 3: counter-clockwise), clamp addressing and
linear magnification on stages 0-2, and stage 0 clamped through its cached setter.

The cached setters skip a value equal to their cache, and `Render_InitStates` sets
`ALPHABLENDENABLE` directly while `Render_AlphaBlend`'s cache starts at 0: the first
`Render_AlphaBlend(0)` does nothing and blending stays on. With caps flag `0x6e0c14` set,
`Render_AlphaBlend` also moves the alpha reference: 1 with blending, 0x80 without (cut-outs by the
alpha test); without it, blending is switched straight back on.

### Texture blend types

`Tex_SetStageBlend` switches on the texture's blend type; `0xff` and 0 are plain modulate (texture by
diffuse, alpha from the texture; type 0 also sets the mip filter from the caps), others add
texture-factor, specular or current-colour combinations and set the blend factors. Type 8 is
**bump environment mapping with luminance** (`D3DTOP_BUMPENVMAPLUMINANCE`, the bump matrix from
`0x520b24`, luminance scale 1.0, offset 0). Which textures carry which type, and what each type is
used for, is open.

## Textures

A texture record is 0x130 bytes in a table of 2,048 at `PTR_DAT_00520b78 + 900`:

| Offset | Field |
|---|---|
| 0x00 | u16 index (its slot; a clone keeps the original's) |
| 0x0e | name: the path it was made from, written with `sprintf` as a format |
| 0x114 | the surface |
| 0x118 | blend type (`Tex_SetStageBlend`) |
| 0x128 | flags: 1 alpha, 0x10 mipmapped, 0x20 bump map, 0x1000 from memory, 0x2000 DXT5 kept, 0x4000 DXT1 kept |
| 0x12c | the archive it came from |

Records go into the first free slot and the count `DAT_0065fa20` goes up by one. That count is also the
base `C3d_Load` adds to a model's polygon texture indices, so a model's polygons point at the right
records only while the slots fill in order.

`C3d_Textures` 0x4b9910 makes a model's records. For each path it:
- upper-cases the path in place;
- cuts it after its first backslash;
- prefixes a directory (`"%s\%s.tga"`);
- calls `Bfl_FindTexture` 0x4a9e60: `<base>.dds`, then `<base>.tga` in the archive, else from disk
  (`Tex_CreateFromFile` 0x4a49c0).

The flags come from short tags in the name, read as strings from the exe:
- other models: 0x90, or 0 for kind 10;
- cars: 0x40 for a reflective name (`BR`, `RU`, or `W` five letters from the end), else 0x100.

One quirk for cars: a `BU`, `B2` or `B3` name leaves the flags variable unassigned. It then holds the
previous texture's flags, or for the first texture the group count. Cars also reuse an already loaded
texture of the same name by cloning its record (0x4b9b80).

The loaders add flags of their own:
- `Tex_LoadTga`: 32 bits means alpha.
- `Tex_LoadDds`: DXT1 or DXT5 are kept when the card has them. `BR`, `RU`, `BODF` (unless `0x520b14`,
  which is 1), `DIGIT` and `REVCT` names are marked alpha instead.
- Both: with option 0x10 a `BU` name becomes a bump map.

`Tex_Bind` 0x4a4850 skips a bind equal to the last (one stage and texture pair, not one per stage), calls
`SetTexture` and `Tex_SetStageBlend`. The types:

| Type | Stage | Blend (src, dst) | UV set |
|---|---|---|---|
| 0xff, 0 | texture x diffuse, alpha modulated (0 also sets the mip filter from the options) | source alpha, inverse | 0 |
| 1 | texture x diffuse, alpha off | one, one | 0 |
| 2 | texture x diffuse | source alpha, one | 0 |
| 3 | texture x diffuse | zero, inverse source colour | 0 |
| 4 | texture x diffuse, the diffuse alpha | unchanged | 1 |
| 5 | the current colour, the texture's alpha | source alpha, one | 0 |
| 6 | the texture alone, alpha off | unchanged | 0 |
| 7 | add-signed of the inverse texture to the current colour | destination colour, source colour | 1 |
| 8 | bump environment map with luminance | unchanged | 0 |
| 9 | texture x current, linear filtering, alpha off | one, one | 1 |
| 10 | as 0xff with the texture alpha replicated into the colour | source alpha, inverse | 0 |
| 0xb | texture x diffuse, the diffuse alpha; coordinates from the camera-space reflection vector of set 1, three-component transform | unchanged | (reflection) |

Records start as type 0; `Mesh_DrawBatchesTex10` sets type 10 before binding.

## Meshes

`VB_CreatePool` 0x4b1980 makes 100 vertex buffers of 2,000 FVF-0x2d2 vertices (and three others).
`Mesh_AddToVB` 0x4b1d00 copies a part's vertices into the first buffer with room and records the buffer
and the start in the part. `Mesh_Batches` 0x4b1ac0 groups the part's triangles into runs of the same
stage-0 texture, each with its indices made relative to the run's lowest vertex. The lowest vertex is
searched from 999, so a run that only uses vertices 999 and up keeps 0.

`Mesh_Draw` 0x49c940 draws the part chosen by mesh `+0x112`. It:
- sets the cull from flag 1;
- sets lighting mode 2 or 3 unless flag 0x20;
- sets blending from flag 8;
- then draws either batch by batch (0x49c880; 0x49c7b0 also sets blend type 10), or polygon by polygon
  through a 2,002-index buffer, each polygon's texture chosen by its frame index (0x49c680; 0x49c510 also
  sets stage 0 wrapping from the polygon flags);
- with flag 0x200 and option 8, runs a reflection pass: 0x4b2980 with option 0x80, else 0x4b2610.

`Light_SetMode` 0x4b6f00 has these modes:

| Mode | Lighting | Ambient | Vertex colour | Lights on |
|---|---|---|---|---|
| 0 | on | A (`0x6e0bac`) | on | 1 |
| 1 | on | A | on | 2 |
| 2 | on | B (`0x6e020c`) | off | 0 |
| 3 | on | B | as the diffuse | 0 |
| 4, 5 | off | A | on | none |

When `0x5210c0` is 0, lights 1 and 2 are then switched off again.

## The port

`src/render/` mirrors the layers:

- `d3d7.h` / `d3d7.c`: the device as C calls with Direct3D 7's values and structure layouts
  (`D3d_SetRenderState`, `D3d_SetTextureStageState`, `D3d_SetTransform`, `D3d_SetLight`,
  `D3d_DrawIndexedPrimitiveVB`, vertex buffers). It records the state with Direct3D 7's defaults and
  needs no GPU, so the native tests check it (`tests/d3d7_test.c`).
- `gl_dev.c`: the back end on OpenGL ES 3.0 (WebGL 2 rules), adapted from OpenNFSPU's THRASH device.
  From there it keeps the flip chain, the two-stage combiner (every `D3DTOP` up to `DOTPRODUCT3`
  except the bump ops, every `D3DTA` with its modifiers), fog, the alpha test and the pre-transformed
  path. New here is Direct3D 7's transform and lighting in the vertex shader:
  - world, view and projection with row vectors, and normals by the inverse transpose of world-view;
  - point, spot and directional lights with range, attenuation and the spot cone;
  - the four material sources, local or infinite viewer specular;
  - linear, exp and exp2 vertex fog from eye depth or range;
  - generated texture coordinates (camera-space normal, position, reflection vector) with the texture
    matrices.

  A transformed vertex is mapped by Direct3D's viewport rule to the same screen coordinates as a
  pre-transformed one, in a form linear in clip space, so GL's clipping stays exact. Every texture is
  RGBA8 with one level, which is all the files have.
- `rstate.c`: the original's render-state layer, ported (`Render_InitStates` and the cached setters,
  `Render_SetBlend`).
- `tex.c`: the texture records, `Bfl_FindTexture`, the two creators with the loaders' flags,
  `C3d_Textures`, `Tex_Bind`, `Tex_SetStageBlend`. Surfaces are RGBA8: the 16-bit surface formats of
  the original (and their banding), the bump conversion and mip chains are not reproduced (the last two
  are off with the default options).
- `mesh.c`, `light.c`: the vertex-buffer pool, `Mesh_AddToVB`, `Mesh_Batches`, the four draw variants,
  `Mesh_Draw` (without the reflection pass, off by default), `Light_SetMode`. The batch pointers the
  original keeps in the mesh record are indices into the port's pool there.

### Caps

The game branches on the device caps it reads at start. The port emulates one hardware T&L card. So far
two flags are used:
- `0x6e0c34`: `DRAWPRIMITIVES2EX`, on, so `Render_ClearZ` does nothing.
- `0x6e0c14`: the alpha reference switch, on.

The rest of `Render_ReadCaps` (which caps fields each flag comes from, and the texture formats and
compression it reports) is open.

### Check

`--param car=<model>` turns a car on a stand under one directional light. `--param stage=<name>` flies
along a stage's mesh (unlit vertex colours). Both go through the ported path: `C3d_Load`,
`C3d_Textures`, `C3d_Build`, `Mesh_Draw`. The viewer only adds the camera, the light and the parts'
placement. Both run headless with screenshots. `tests/d3d7_test.c` checks the 205's 27 records, 30
batches and 672 triangles without a GPU.

Verified:
- The Peugeot 205 T16 (`205c1`, TGA textures) and the Focus WRC (`f99a1N`, DXT5) render with their
  parts placed and the alpha-cut wheels.
- France stage 2 and Australia stage 1 render with their textures.

There is no reference capture from the original yet.

## Open

- Which textures get which blend types at run time; the bump environment map (op 22/23 need a signed
  texture format, which RGBA8 lacks) and the reflection passes (0x4b2610, 0x4b2980).
- The 16-bit surface formats (565, 4444, 1555) and their quantisation of the textures.
- The remaining caps.
- Stage brightness: the prelit colours modulated by the texture look dark, and the original's stage draw
  path (blend types, lighting mode) will decide that.
- The 2D path's use by the front end (step 4) and the sky, ground and scenery models (step 5).
