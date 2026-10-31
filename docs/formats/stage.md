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
| `<stage>.tre` | 3.2 MB | `Stage_LoadTrackSplines` 0x41f930 -> 0x4b5620 -> `Tre_Parse` 0x4b4aa0 | Renderer-side scenery tree, one node per stage-mesh section (below). Not loaded at low scenery detail (0x405ba0) |
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

Parsed in place by `Hpc_Parse` 0x490d50 (layout confirmed: it accounts for the whole of `fra02.hpc`
except the last four bytes, which the parser addresses as one more `u16`):

| Offset | Type | Content |
|---|---|---|
| 0x00 | 2 x i32 | 0xffe00000 twice in `fra02` (-32.0 in 16.16: probably the grid origin) |
| 0x08 | 5 x i16 | Node counts of five grid levels (`fra02`: 25, 288, 2112, 0, 0) |
| 0x12 | 2 bytes | Padding (`fc fc`) |
| 0x14 | per level, count x 8 bytes | Grid nodes, four `u16` each (the first is 0xffff in empty nodes) |
| | `u16` + 2 pad, then n x 12 bytes | Record array A, six `u16` each (`fra02`: 6,954) |
| | `u16` + 2 pad, then n x 8 bytes | Record array B, four `u16` each (12,768) |
| | `u32`, then n x 2 bytes | Index list (26,463 `u16`) |
| | `u16` | Terminator |

Reading A as triangles (vertex indices and flags), B as quantised vertices and the index list as the
triangles of each grid cell fits the counts; this is inferred, to be confirmed when the collision query
(0x490e00 onwards) is ported.

## .tre

`Tre_Parse` 0x4b4aa0 reads three `i32` (scaled to floats by the constant at 0x511310), three 16.16
vectors, then a `u16` node count followed by the nodes, 0x14 bytes each. A node is matched to a section of
the stage mesh (the list at 0x71f608) by two `i32` keys at `+0x04` and `+0x08` and stores that section's
index at `+0x00` (0xffff when there is none). Each node owns arrays the parser turns into pointers: items
of 0x30 bytes (count at `+0x02`), a 0x14-byte block, and in that block arrays of 0x30, 0x4c and 4 bytes.
The name and the renderer side (0x4b0000-0x4b6000) suggest the stage's trees and other instanced scenery
per section. Inferred.

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
