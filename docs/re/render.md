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
| 0x4bcaf0, 0x4bcbb0, 0x4bcc20 | `Fog_Set`, `Fog_SetLinear`, `Fog_Off` | Vertex fog: table mode 0, `FOGVERTEXMODE` 3 (linear), start and end, colour |
| 0x4a8890 | `Proj_Set` | The projection (below) |
| 0x4ade00 | `View_Set` | The view from a camera object's matrix (+0xd8) |
| 0x49e1f0 | `EnvMap_RenderCube` | Six views around an object (a cube environment map for the reflections), view, projection and viewport restored after |
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

`Render_SetBlend` 0x4a3e40 caches the blend pair too (starting at 1, 1), and `Render_ForgetBlend` 0x4a3de0
resets that cache without touching the device. The cached setters skip a value equal to their cache, and `Render_InitStates` sets
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

## View and fog

`Proj_Set` 0x4a8890 takes the x and y scale, the far plane and the near plane in 16.16; a 0 keeps the
previous value (initially 2.337, 4.197, 250 and 1). It sets Direct3D's left-handed perspective:
- `m00 = 0.5 xs`, `m11 = 0.5 ys` (the 0.5 is the float at `0x520b80`);
- `m22 = far / (far - near)`, `m32 = -near far / (far - near)`, `m23 = 1`.

It also keeps the matrix rounded to 16.16 at `PTR_DAT_00520b78 + 0x23e4` for the game's own use. The
race cameras (`Camera_Projection` 0x422d40) compute the scales from a per-camera field-of-view value:
they double one scale for a split screen, negate x in one mode, and in another raise the far plane to
at least 150. With the default scales a 640-wide viewport has square pixels only at about 356 lines (a
16:9 band), so the views that use them are presumably letterboxed; open.

`Fog_Set` 0x4bcaf0 converts the fog start and end (16.16; two more values are stored beside them) and
sets the colour from R, G, B bytes, choosing linear vertex fog when the card has vertex fog.
`Fog_SetLinear` sets the distances; `Fog_Off` sets both fog modes to none. The port's card reports table
and vertex fog (`src/render/view.c`).

## 2D

Two queue systems, each with four layers, are drawn layer by layer as pre-transformed `0x1c4`
vertices. The front end, the HUD and the fonts use them.

`Sprite_Add` 0x4a3290 queues a sprite on layer 1-4, up to 1,023 a layer. A 0x38-byte record holds:
- the source rectangle in the texture and the screen rectangle;
- a second, unused rectangle;
- the texture and the colour bytes;
- a rotation, converted from degrees to 4,096 units with a multiply-shift divide by 360;
- a centre: none means (0, 0), the screen corner; (-1, -1, -1) means the sprite's middle;
- flip bits: 1 or 4 mirror u, 2 or 4 mirror v.

`Sprite_Flush` 0x4a3650 builds a strip of four vertices per sprite. The UVs are the source rectangle
over the texture size (record +0x120, +0x122) plus half a texel; the flag that turns the half texel off
(0x663b1c) is never set. A rotated sprite turns its corners in whole pixels through the 16.16 matrix
helpers. It draws with clipping off and point filtering, binding each sprite's texture (and so its
blend type).

A sprite without a texture gets no vertices, but the draw loop still walks every record. Every later
sprite in that layer is then drawn with the next sprite's vertices, and the last with whatever the
array held.

`Tri_Add` 0x4bb650 queues a flat-coloured triangle (16.16 corners) on layer 2, 3 or 4, or any other
value for the first queue. Only x, y and the colour are written. z, rhw, specular and the UVs keep the
arrays' zero start, so the specular alpha of 0 means full fog if fog is on. The limit test leaves out
the layer-3 count, which can then overrun in the original; the port stops it at 1,024. `Tri_Flush`
0x4bb850 draws a layer untextured with source-alpha blending.

The text path:
- `Font_Print` 0x40b880 draws a string through `Font_DrawGlyph` 0x40b7c0, one sprite per glyph on the
  font layer (`Font_SetLayer` 0x40bab0) ([media.md](../formats/media.md#fonts-pcf-tga)).
- It first draws three numbers from the C runtime's `rand()`, so the game's random sequence advances
  with every string printed. The port has the runtime's generator (`src/crt.c`).

## Triangle queues

The effects queue world-space textured triangles in four buckets, chosen by a flag bit. World_Render
draws each bucket at its own point in the frame:

| Bit | Records | Size | Drawn |
|---|---|---|---|
| 8 | 0x7dd168 | 0x400 | after the terrain, lighting mode 5 |
| 0x20 | 0x730fd0 | 0x800 | next, mode 4 |
| 0x10 | 0x803168 | 0x200 | after the scenery, mode 5 |
| 0x40 | 0x7acfd0 | 1 | last, mode 4 |

The 0x40 bucket's limit test is against 1, so it holds a single triangle. A record is 0x98 bytes:
- three `0x2d2` vertices (+0x00);
- the texture (+0x90);
- the flags (+0x94): 1 no z test, 2 no z write, 4 no culling.

There are two adders:
- `Poly_Add` 0x4bba00 takes three corners: position and UV in 16.16, colour bytes R, G, B, A. It sets
  the specular to 0xff000000. The normals and second UVs keep what the record held.
- `Poly_AddRaw` 0x4bbc60 copies three ready vertices.

A full bucket sets a flag that nothing reads. Flags without a bucket bit make the original write
through the flags value as an address; the port drops the triangle.

`Poly_Flush` 0x4bbd80 copies one bucket into the vertex buffer at device + 0x338 (0x1800 vertices,
made with the model pool in 0x4b1980). It draws a run for each change of texture or flags. The buffer
is locked once at the start; each change unlocks it, draws, locks it again and sets the new state. The
state starts as texture "1" and flags -1, so the first triangle always sets it. Afterwards texture 0
is unbound, z is turned back on, culling goes back to the default, and the cached blends are
forgotten. `Tri_Init` empties only the 8 and 0x10 buckets.

## Sprites

Camera-facing quads, up to 800 a frame (0x6c9e00, 0x58 bytes each), made at the race start
(0x4b1150, which also fills the index list 0x6db200: per sprite 0 3 2, 0 1 3). `Sprite3d_Add`
0x4b11c0 takes a 0x24-byte sprite:
- the centre (16.16);
- four extents, so the corners are (-e1, e0), (-e1, e2), (-e3, e0), (-e3, e2) along right and up;
- the colour bytes R, G, B, A;
- a turn about the view (+0x20, 4,096 a turn);
- a shade (+0x22) and flags (+0x23): 1 mirrors the texture; 2 lights the colour. A lit colour is the
  colour times the light ramp's full end and times its dark end (0x4b3ae0, the ramp's byte copy at
  0x6e0b34), mixed as (full (256 - shade) + dark shade) / 256.

Runs of the same texture are counted as the sprites come (0x6c8500). `Render_DrawIndexed` 0x4b1530
builds four `0x2d2` vertices per sprite from the camera's first two rows (turned by the sprite's
angle), specular 0xff000000, UVs (0,1) (0,0) (1,1) (1,0), mirrored in u. Then z writes off, a
`DrawIndexedPrimitive` per run with the run's texture bound (no blend set up), z writes on; the
queue is emptied and the cached blends forgotten. Two triangles a sprite go to the 2D count
(0x59ce20).

## Fixed-point math

The simulation and parts of the renderer work in 16.16, with angles in 4,096 units per turn.
`Math_InitTables` 0x4b7b20 fills the tables at start, from double constants in the exe that the port
reproduces exactly (`tests/math_test.c`):

| Table | Address | Contents |
|---|---|---|
| sine | 0x6e2ef4 | 4,096 int32 (cosine at +0x400) |
| tangent | 0x6e93f4 | 4,096 int32, the two poles 0x7fffffff |
| square root | 0x6e0ef4 | 4,096 u16, `sqrt((8 + 16i) / 65536)` |
| arcsine | 0x6e6ef4 | 4,096 u16, `asin(i / 4095)` as an angle |
| arctangent | 0x6e8ff4 | 512 u16, `atan(i / 511)` as an angle |

The original computes with the x87: products rounded to double, `fsin`, `fptan` and `fpatan` in
extended precision, `fistp` to nearest. The port's libm can differ from that only in the last bits.
The matrix helpers (`Mat_Identity` 0x4ba2b0, `Mat_RotZ` 0x4ba320, `Vec_Transform` 0x4baa40,
`Mat_ToFloat` 0x4ba090, `Vec_RotateAbout` 0x4ba3b0) are in `src/fixmath.c`.

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

**Alpha keys** (for the cars' lamp lenses, [world](world.md#lamps)): 0x4a4d30 locks a texture and
replaces three alpha values, one after another on each pixel. On a surface whose alpha mask has 8 bits
it compares the whole byte; with 4 bits, the high nibble. The first call for a car first copies the
surface into the car's record at 0x65ae20 + 0x130 n (64 of them, a format 9 surface). 0x4a5080 blits
that copy back over a texture, and 0x4a5ba0 releases them all.

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

### Display

`render/display.c` is the adapter. The original asks DirectDraw for the chosen device's modes
(`0x4a78a0`). Its callback `0x4a8da0` keeps a mode when:
- it is at least 640 x 480;
- it has 16 or 32 bits (32 only on a hardware device; windowed, any depth, else only the desktop's);
- three buffers of it fit the video memory less the textures' share.

The modes are kept at `0x6631f8` (width, height, bits; 12 bytes each), with the count at `0x663b28`
and the current one at `0x663b2c`. A mode that is not listed falls back to 640 x 480 at the desktop's
depth.

The port's adapter offers a card of the time's 4:3 and 5:4 sizes, from 320 x 240 to 2048 x 1536, in 8, 16
and 32 bits, and applies the same rules with 64 MB, which leaves 640 x 480 to 2048 x 1536 in 16 and 32
bits. The back end draws the back buffer at the mode's own size and scales it to the drawable. The
mode is the player's trade of detail for speed, as on the original's display.

The port adds 16:9 and 16:10 sizes (1280 x 720 to 2560 x 1440). On a mode wider than 4:3:
- `D3d_ScreenSize` reports the 4:3 screen of its height (1280 x 720: 960 x 720). Everything the game
  lays out by the screen's size (menus, HUD, 2D, viewports, `Camera_Projection`'s aspect,
  `Disp_HiRes`) sees an ordinary 4:3 mode. The settings (`0x52afc4`) keep the mode's own width.
- The GL back end puts that screen in the middle of the back buffer, `ox = (W - w) / 2`:
  pre-transformed x, the viewport's x (`u_vp`), scissors, clears and the read-back move by `ox`.
- `D3d_SetWide` (on in the race's draws, off in the front end's, the loading screens' and state
  11's) chooses how the sides are filled:
  - Off: the scissor is clamped to the 4:3 screen and the sides are cleared black before the
    present (pillarbox).
  - On: an edge at 0 or at w (scissors, clears, and pre-transformed vertices within a pixel of
    one) goes to the whole screen's edge. A full-screen viewport is then clipped to the whole width
    while its mapping stays the 4:3 one: the projection is unchanged, the vertical field of view
    stays, and the sides show more of the stage (Hor+). The fades and the pause's dimming stretch
    across. The HUD, the mirror and the 3D-to-2D projections (`Obj_ToScreen`) stay in place,
    because the 3D and the 4:3 screen share one mapping. On a left and right split screen each half reaches out to its own side.

Changing the mode on the display page (`0x4f2430`) or the card (`0x4f2210`) does the following:
- makes the device again (`0x4a78a0`);
- stores what it took in the settings (`0x405c20`-`0x405c60`);
- remounts the resolution's front-end archives (`0x4d21e0`);
- reloads every texture from its archive (`0x4a4c40`) and the four fonts (`0x4f4d80`);
- measures the scrollers again (`0x4ef190`).

`Game_Init` (`0x4d08d0`) opens the settings' mode at start-up.

Over 1,023 pixels across, with 1,024-texel textures in the caps (`0x405c10`, `0x4b7560`,
`0x4b7590`; `Disp_HiRes`), the game takes its 1,024 art:
- the archives Res1024, FERes1024, Com1024 and the service area's 1024 set;
- the dash's sizes and lit table, and the challenge tree's offsets;
- the co-driver arrows' rectangles at `0x517e70`, the car page's at `0x524ad8`;
- the service list's arrow offsets, and four copyright screens.

`--param mode=<w>x<h>x<bits>` sets the settings' mode before start-up (and the viewer's).

### Caps

The game branches on the device caps it reads at start. The port emulates one hardware T&L card. So far
two flags are used:
- `0x6e0c34`: the triangle raster caps' `ZBUFFERLESSHSR` (`dpcTriCaps.dwRasterCaps & 0x8000`, read at
  0x4b7262; a tiler without a z-buffer), off, so `Render_ClearZ` clears. (Read earlier as
  `DRAWPRIMITIVES2EX`; the disassembly takes the dword at +0x44 of the `D3DDEVICEDESC7`.)
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

- Which textures get which blend types at run time.

## The bump environment map (option 0x10)

With option 0x10 a texture whose name has "BU" before its extension gets flag 0x20 and a bump surface
(the card's bump format) made by 0x4a5880 from the decoded image: du and dv the unsigned differences of
the red (0x4a5730) to the texel right and below (wrapping), written into the signed bytes, the alpha
(0x4a57e0) as the luminance. The disk loader 0x4a6370 (for a record given flag 0x20) reads the TGA's
first byte (blue) and uses the height plus 0x80 past the edges; no caller gives it the flag. The
reflection pass then binds the polygon's texture on stage 0 with type 8 (BUMPENVMAPLUMINANCE, the
matrix's diagonal 0x520b24, scale 1, offset 0) and the environment map on stage 1 with type 9. The
port keeps 8-bit du, dv and luminance in RGBA8 and decodes them as signed in the shader.

## The cars' cube maps (option 0x80)

Confirmed from the code; seen in the port with the option forced (the faces drawn and the car's
windows reflecting the sky):

- Tex_LoadEnvMap 0x4b23c0 takes a count and a size: Stage_Load gives one cube of 64 (with 0x52e890
  option 16 one per car of the race, players plus CPU cars, of 128), the service area none. Each cube
  (0x4a4b10) is a record in the 20 slots at `PTR_DAT_00520b78 + 0x2384`, six 0x130-byte face records
  (+0x114 the surface: DDSCAPS2 cubemap with all faces, the primary's pixel format, faces fetched in
  the order +x, -x, +y, -y, +z, -z by 0x4a76d0, each with its own z-buffer). 0x6de95c maps a car index
  (mesh flag bits 15-17, set by Car_Load) to its cube; the texture reset 0x4a5d10 frees them.
- Race_DrawView 0x40f0c0, in its loop over the cars from the last: for the first car only (every car
  with option 16) its main model and its far model (when shown, else its second model) are hidden and
  EnvMap_RenderCube 0x49e1f0 draws from that model's frame: per face the frame turned (right/forward
  -z/+x, +z/-x, x/+y with up -z, x/-y with up +z, as is, -x/-z), the billboard turn from it, the
  target set (0x4b2d70, the device's vtable +0x20), only the depth cleared, the cells' unsorted objects
  with z off (far 120), then far 80 the cells' meshes and their scenery within 20 units (by the
  billboards' last distance, +0x114), the sorted objects; after it the projection (far as given, near
  1), the back buffer, the view and projection. Then those models are shown in pass 1 (or hidden with
  their type 0x14 object shown when World_Mode is set) before 0x46b8f0 and 0x46b670.
- Render_EnvMapped 0x4b2980 (Mesh_Draw's reflective pass): stage 0 the cube with blend type 0xb
  (camera-space reflection, three-component transform), texture matrix 0 the inverse of the world and
  view rotations in 16.16 (translations and +0x3c zeroed, 0x4ba160 rounding each times 65536), each
  run's own texture on stage 1 with type 5 (added by its alpha); NORMALIZENORMALS and LOCALVIEWER on
  for it and left off after. A car without a cube gets 0x4b2610.
- The port draws the other cars into the cube from its own list (the original's object lists hold
  them, not yet traced).
- The 16-bit surface formats (565, 4444, 1555) and their quantisation of the textures.
- The remaining caps.
- Stage brightness: the prelit colours modulated by the texture look dark, and the original's stage draw
  path (blend types, lighting mode) will decide that.
- The 2D path's use by the front end (step 4) and the sky, ground and scenery models (step 5).
