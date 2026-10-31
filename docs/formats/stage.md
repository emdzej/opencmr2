# Stages

A stage is one [archive](bfl.md) `Game/Tracks/<Country>/<ccc><nn><Hi|Lo>.bfl` plus the country's texture
archives `<ccc>hit.bfl` / `<ccc>lot.bfl` (DXT5 only, about 250 textures each) and the shared cloud
archives `Game/Tracks/Clouds/pc/cloud_{light,med,heavy}.bfl`.

| Directory | Prefix | Stages | Rally |
|---|---|---|---|
| `Finland` | `fin` | 10 | Finland |
| `Greece` | `gre` | 11 | Greece (Acropolis) |
| `France` | `fra` | 10 | France (Corsica: the artist paths say `P:\Tracks\Corsica`) |
| `Sweden` | `swe` | 11 | Sweden |
| `Aus` | `aus` | 10 | Australia |
| `Kenya` | `ken` | 11 | Kenya (Safari) |
| `Italy` | `ita` | 10 | Italy (San Remo) |
| `Uk` | `uk` | 11 | Great Britain |
| `Japan` | `jap` | 2 (11, 12) | Challenge mode's Super Special Stages ("a country unique to Challenge mode", ReadMe). `CPU_runs/champend.rpl` is used by the championship-end sequence |
| `Chall` | `cha` | 8 | Arcade mode circuits (lap races against up to five AI cars; the only stages with AI racing lines) |

94 stages, each in a `Hi` and a `Lo` archive. The rallies with 11 stages (Greece, Sweden, Kenya, UK) are
the four whose last day is a Super Special Stage (the ReadMe: every second rally), so stage 11 is probably
the head-to-head circuit. The two differ only in `temp.obj` and `<stage>.tre` (the
scenery), so `Hi`/`Lo` is the scenery detail level.

## Members

Every stage archive has the same 25 members plus 48 horizon files. Names use the lower-case stage id
(`fra02`). Loader addresses are where the member is looked up in the mounted stage slot.

| Member | Size (fra02) | Loader | What it is (status) |
|---|---|---|---|
| `temp.c3d` | 2.2 MB | `Stage_LoadC3d` 0x41fc50 | The stage mesh, [PP_F](c3d.md) kind 2: 148 meshes, 360 sections, 54 textures |
| `temp.obj` | 1.0 MB | `Stage_LoadObj` 0x472830 | Scenery objects, PP_F kind 0 (`OBJOBSHP0` names) |
| `temp.sht` | 1.2 MB | `Stage_LoadSht` 0x472870 | PP_F kind 4 (`shtShp0` names); likely shadow or shaded scenery shapes |
| `temp.sky` | 15 KB | `Stage_LoadSkyGround` 0x46f060 | Sky dome, PP_F kind 5 |
| `temp.gro` | 7 KB | `Stage_LoadSkyGround` 0x46f060 | Ground/backdrop, PP_F kind 5 |
| `<stage>.tre` | 3.2 MB | `Stage_Load` 0x41f930 -> 0x4b5620 -> `Tre_Parse` 0x4b4aa0 | Tree shadows: one node per cell with trees, a shadow mesh each (below). Not loaded at low scenery detail (0x405ba0) |
| `<stage>.tsc` | 5 KB | 0x41f930 | Starts with a count (0x2f) and 16.16 vectors |
| `<stage>.xhi` | 28 KB | 0x41f930 | 16.16 values (0x00010000 runs) |
| `<stage>.hpc` | 258 KB | `Stage_LoadHpc` 0x41fc90 -> `Hpc_Parse` 0x490d50 | Collision geometry: a five-level grid with record arrays (below) |
| `<stage>fin.dat` | 106 KB | `Stage_LoadFinDat` 0x490c30 | Array of 0x30-byte records (count = size / 0x30) |
| `<stage>.cat` | 22 KB | `Stage_LoadCat` 0x420630 | Array of 0x2c-byte records; byte `+0x18` bit 0 marks the last active one |
| `<stage>.csp` | 80 B | `Stage_LoadCsp` 0x456c10 | Header + 16.16 positions; read at `+0x18`. Likely start/split/finish points |
| `<stage>.cod` | 224 B | `Codriver_LoadCod` 0x416720 | Co-driver pace notes: distance + call codes, spoken with the speech table at 0x416770 |
| `<stage>.srf` | 54 B | `Stage_LoadSrf` 0x41fcd0 -> 0x41afe0 | Surface table: one byte per surface slot (ids into the surface list) |
| `<stage>.bsp` | 48 B | `Stage_LoadBsp` 0x420020 -> 0x463290 | Small record of 16.16 values |
| `<stage>.tm0..2` | 1,360 B each | `Stage_LoadTm` 0x455300 | Three tables of the same size, packed bytes |
| `<stage>.ai0..2` | 0 in fra02 | `Ai_LoadLines` 0x47c2f0 | AI racing lines (three sets). Non-empty only on the eight arcade circuits (49-80 KB) and `swe11`; rally stages have none, since rally AI times are not driven |
| `<stage>.cfl` | 0 | | Empty on every stage checked |
| `<stage>h.grp`, `l.grp` | 34,702 B | 0x41c260 | Same format as the `.rpl` replays (below) |
| `NNNN[bli\|clo\|sto].hor` | 80 B each | `Stage_LoadHorizons` 0x41fd30 | Horizon/fog colours for 12 times of day (0500 ... 2400) x 4 weather variants (clear, blizzard, cloudy, storm) |

The simulation reads these in place; most are fixed-size record arrays in 16.16 fixed point. Their field
layouts are the main reverse-engineering task of the stage step of the [port plan](../re/port-plan.md).

## .hpc

The collision mesh, read in place by `Hpc_Parse` 0x490d50. Confirmed by the ported query, which finds
the road under fra02's start line:

| Offset | Type | Content |
|---|---|---|
| 0x00 | 2 x i32 | The grid origin, x and z (fra02: -32.0, -32.0) |
| 0x08 | 5 x i16 | Node counts of the five levels (fra02: 25, 288, 2112, 0, 0) |
| 0x12 | 2 bytes | Padding |
| 0x14 | per level, count x 8 | Nodes: i16 triangle count, or -1 for a node split into 4 x 4 children; 2 bytes; i32, the first list entry or the first child in the next level |
| | u16, 2 pad, n x 12 | Vertices: x, y, z (i32, 16.16; fra02: 6,954) |
| | u16, 2 pad, n x 8 | Triangles: three u16 vertex indices, the surface (low 7 bits), a byte (fra02: 12,768) |
| | u32, n x 2 | The triangle list (u16) |
| | 2 x i16 | The top grid's width and height in cells |

The query works like this:
- **The top cell** (`Hpc_Find` 0x491550): the offset from the origin's top byte, signed, so cells are
  256 units. Each level down takes two more bits of x and z, as child x + 4 z.
- **The triangle** (0x4916a0): among a node's triangles, the one containing (x, z) whose mean y is
  nearest the caller's y. Containing means inside the x and z range and on the inner side of all three
  edges, inclusive (0x490f20).
- **The height** (0x4910f0): the plane through the first vertex, with the unit normal. That normal is
  the cross product of the two unit edges from the first vertex, made unit again.
- **The ground query** (0x490c90): keeps the last triangle between calls and searches again when the
  point leaves it.

## .csp

The start positions, read by `Stage_LoadCsp` 0x456c10:
- **Records:** 12-byte x, y, z records. In a rally the car starts at the first record's x and z; the
  head-to-head and arcade cases use other entries (0x4583d0). The y is not taken.
- **Angles:** three u16 at +0x18 (0x456c90), set on the car's objects by 0x4ac4f0. This restores the
  rest matrix, then 0x4ac820 turns it by [1] about its y axis, [2] about z and [0] about x. Each turn is
  about the row as already turned, and the turned rows are made unit with the table square root.
- **fra02:** the car starts at (185.0, 15.5), 32/4096 of a turn from +z, on a road triangle at y 53.3.

## .tre

The tree shadows: one node per stage cell with trees, each tree's shadow as polygons that take the
tree's texture. `Stage_Load` reads it after `Section_Init`, at high scenery detail only (0x405ba0: bit 4
of 0x52afd0), through `Tre_Load` 0x4b5620 and `Tre_Parse` 0x4b4aa0. The original turns the arrays into
pointers in place. Confirmed on fra02 (3,270,594 bytes, which the layout below accounts for exactly):

```
+0x00  3 x i32           (0, 34365, 55802 in fra02), also kept as floats (times 1/65536)
+0x0c  3 x 3 x i32       16.16 vectors (fra02: (0,0,1), (0,1,0), (-1,0,0))
+0x30  u16               node count n
+0x32  n x 0x14          nodes
then, for all nodes in turn:
       items      0x30 bytes, the node's +0x02 of them
       blocks     0x14 bytes, one per node
       vertices   0x30 bytes (the PP_F vertex), block +0x04 of them
       polygons   0x4c bytes (the PP_F polygon), block +0x00 of them
       colours    u32, one per vertex
```

| Record | Offset | Field |
|---|---|---|
| node | +0x00 | u16: the matching cell, written by the parser (0xffff none) |
| node | +0x02 | u16: item count |
| node | +0x04, +0x08 | i32: the cell's x and z (16.16), matched exactly against the section records |
| node | +0x0c, +0x10 | the items and the block (pointers after the parse) |
| item | +0x00, +0x04 | i32: the tree's x and z |
| item | +0x20 | 16.16 factor, set to 1.0 by the parser (lowered near a light by 0x4b6ca0) |
| item | +0x28 | its vertex count in the node's mesh (used by the relighting) |
| item | +0x2c | its scenery record (written by the parser) |
| block | +0x00, +0x04 | polygon and vertex counts |
| block | +0x08, +0x0c, +0x10 | the vertices, polygons and colours (pointers after the parse) |
| polygon | +0x04 | in the file the node's item it belongs to; the parser replaces it with the texture of that tree's first polygon |
| colour | | 16.16 intensity (fra02: 0.36, 0.54), the index into a 50-entry colour ramp (`* 49 >> 16`) |

An item is matched to the first scenery record within 0x28f (0.01) of its x and z: cells in order,
then each cell's list. All 1,572 of fra02's items match its 1,572 billboards. The vertex colours in the
file are white. The relighting (`Section_Relight` 0x4b3c00, from the stage's light colours) gives the
shadows their colour and alpha before they are first drawn.

Every node with a cell gets a 0x120-byte mesh record named `SHAD<node>`, set up like this:
- the block's arrays and counts;
- `+0x108..+0x10a` set to 0xff;
- the mesh batched and put in the vertex buffers.

It goes into the cell's slot of a table at 0x6dfd98; a second table at 0x6e0204 maps the cell to its
node. The mesh flags are 0x402d plus whatever bits `0xffffe02d` keeps of a stack slot. The original
leaves the node loop's last offset there, the node count times 0x14 (fra02: 121 x 0x14 = 0x974, so
0x402d).

`Stage_DrawOverlay` 0x49cf80 draws the listed cells' shadow meshes with the identity world matrix and z
writes off, between the terrain and the cars. Before that it steps a time-driven value (0x4bcc60:
0x81680c grows by 0.004 per centisecond; 0x816810 swings between two bounds at a rate that depends on
where it is). Nothing in the release reads either value.

## .hor

Time-of-day records of 80 bytes, 48 per stage (12 times x 4 weather sets, see
[world.md](../re/world.md#time-of-day)). Known fields:

| Offset | Field |
|---|---|
| +0x00 | 7 x i32 (16.16, blended linearly) |
| +0x1c-+0x47 | colour bytes, blended as bytes |
| +0x2f | changed by the weather (0x461a70) |
| +0x30-+0x32 | the low light colour, R, G, B (fra02 12:00: 106, 131, 164) |
| +0x38-+0x3a | the high light colour (255, 255, 245) |
| +0x48-+0x4d | six bytes, blended; the two records' average becomes the fog colours |

## .cat

The stage's centre line, read by `Stage_LoadCat` 0x420630: records of 0x2c bytes (fra02: 492).

| Offset | Field |
|---|---|
| +0x00, +0x08 | x and z of the point (16.16; y is not used) |
| +0x0c | a direction (fra02's first: (1.0, 0, 0)) |
| +0x18 | flags; bit 0 on the finish point. The last flagged point + 1 is the finish (fra02: 484) |

A stage whose first and finish points are within 50 units of each other is a circuit, and its length is
the laps (bits 16-19 of the stage selection word) times the finish point.

A car's progress (`Car_Progress` 0x420a30, a record of 0x18 bytes per car at 0x538aa8) steps to the next
point once that point is behind the car along the line's tangent, or back to the previous one when that
one is ahead. The tangent is the sum of the unit directions from the previous point and to the next. The
points passed count towards the finish (`Car_StageShare` 0x421470). A human's car also searches 25
points a step for the nearest one, and re-syncs to it every 26 steps.

## fin.dat

`Stage_LoadFinDat` 0x490c30 reads `<stage>fin.dat` (2,213 records in fra02); `Section_Init` 0x4b8270
files each record in front of its cell's list. The walls hold the cars on the stage (the collision in
[architecture](../re/architecture.md#walls)). A record, 0x30 bytes, 16.16 values:

| Offset | Size | Field |
|---|---|---|
| +0x00 | 3 x 4 | First end x, y, z (the y values seen are -1000 and 1000: the wall spans every height) |
| +0x0c | 3 x 4 | Second end |
| +0x18 | 3 x 4 | Normal (y 0, unit length) |
| +0x24 | 4 | Not read by the collision |
| +0x28 | 4 | Cell index; after filing, the next record in the cell (cell +0x28 is the head, +0x24 the count) |
| +0x2c | 1 | Type: 0 and 1 ignored; 7-11 call an empty function; 6 and 0x19 mark the car off the stage (car +0xbf8); 0x17 bumps the wheels and slows the car; 0x18 starts a 25-step timer that then marks it; others are solid. Indexes the restitution table at 0x51fb00 and the friction table at 0x51fb68 |
| +0x2d | 1 | Bit 0: two-sided (the side test is skipped, the ends are not tested separately) |
| +0x2e, +0x2f | 1 each | The ends' cap angles, in 1.8 degree units (200 a turn); 0xff no cap |

## Surfaces

The surface physics are named in the exe (pointer table at 0x519198 used by 0x41b460): `TARMAC`, `LOOSE`,
`MONSOON`, `SAFARI_MUD_MERGE`, `ROUND_GRAVEL`, `HEAVY_GRAVEL`, `MEDIUM_GRAVEL`, `LIGHT_GRAVEL`, `MUD_ROAD`,
`SHALLOW_WATER`, `DEEP_WATER`, `ROCK_SURF`, `SNOW_ON_ICE`, `DEEP_SNOW`, ... and their `WET_` variants, plus
about 110 `TRK_*` track-surface names (`TRK_TARMAC`, `TRK_HEAVY_GRAVEL_1`, `TRK_SNOW_BANK_FRONT`, ...) and
the sound types `SST_NULL`, `SST_BASIC`, `SST_SKID`, `SST_TARMAC`, `SST_COMBINATION`. These tables are
loaded from the exe at run time, never copied.

Surface sounds come from `Sounds/surface.bfl`: `SurfSnd\<nn>_i<nn>.wav` (inside), `_o` (outside), `_ski`,
`_sko` (skid inside/outside).

## Replays (.rpl, .grp)

`Game/DemoRuns/<Stage><H|L>.rpl` (installed from `data1.cab`: Fra01, Ken03, Swe01) are the attract-mode
demo runs, chosen by detail level (`%sH.rpl` / `%sL.rpl`, 0x41c510). `Game/Tracks/Japan/CPU_runs/champend.rpl`
is the championship-end run (`%s\CPU_Runs\champend.rpl`, and `%s\CPU_Runs\%s%d%d%s.rpl` for others,
0x41c1b0). A stage's `h.grp` / `l.grp` are the same format. Player replays are saved under `replays\`.

**Replays are input recordings** (confirmed), so playing one back through the simulation reproduces the
run. Loaded by `Replay_Load` 0x46d2d0, which accepts the file only if its size is one of four:

```
0x110 + cars * 0x114e + cars * frames * 4      (the demo runs: 1 car, 7500 frames = 34,702 bytes)
0x110 + cars * 0x5e   + cars * frames * 4
0x110 + cars * 0x114e + cars * frames * 16
0x110 + cars * 0x5e   + cars * frames * 16
```

with `cars` the `i16` at `+0xfc` and `frames` the `i16` at `+0xfe`. The 0x110-byte header is a memory image
of the replay control block (it holds pointers from the recording machine that the loader replaces,
0x46d470). 7,500 frames at one per 40 ms step is a five-minute buffer; the file always has the full size.

A 4-byte frame is packed by `Replay_PackControls` 0x46bdc0 from the car's controls at `+0x1d0`:

| Byte | Bits | Content |
|---|---|---|
| 0 | 0 | Flag from the car's `+0xb8c` |
| 0 | 1 | Flag from `+0xb90` |
| 0 | 2-7 | Control byte `+0x1d2` (0-63) |
| 1 | 0-1 | Control byte `+0x1d4` + 1 (a -1/0/+1 value: the gear change) |
| 1 | 2-7 | Control byte `+0x1d3` (0-63) |
| 2 | 0-5 | Repeat count: the frame stands for this many steps |
| 2 | 6 | Bit 0 of `+0x1dc` |
| 2 | 7 | `+0x1d8` |
| 3 | 0-5 | Steering amount: `+0x1d0` if non-zero, else `+0x1d1` |
| 3 | 6 | Steering side: set when `+0x1d0` is used |
| 3 | 7 | Flag from `+0xb88` |

The three flags are only stored when the controller in use is of type 3 (0x46c450). `Replay_SameFrame`
0x46cbe0 compares a new frame with the last one: if all fields but the count match and the count is
below 63, the count grows, otherwise a new frame starts. So frames are run-length coded and 7,500 is a
capacity, not a length. Which of `+0x1d2` / `+0x1d3` is throttle and which brake is read from the input
code (0x47b0e0) when it is ported.

Demo runs are the natural end-to-end test of the port: a replayed run must reproduce the original's car
path, which a headless gasm run can check frame by frame.
