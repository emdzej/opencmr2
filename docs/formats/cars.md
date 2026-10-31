# Cars

`Game/Cars/` holds 22 cars. Each has a three-character code, one `.cin` file and about 22 model and
texture files. The display names are lines 195-216 of `g<language>.txt` (full names) and 217-238 (short names). The
code-to-name pairing below is by name; the order of the exe's own car table is still to be read.

| Code | Car | Code | Car |
|---|---|---|---|
| `foc` | Ford Focus | `int` | Lancia Integrale |
| `f99` | Ford Focus 1999 | `ia1` | Lancia Integrale Alternate 1 |
| `fa1` | Ford Focus Alternate 1 | `ia2` | Lancia Integrale Alternate 2 |
| `fa2` | Ford Focus Alternate 2 | `sie` | Ford Sierra Cosworth |
| `mit` | Mitsubishi Lancer | `min` | Mini Cooper S |
| `ma1` | Mitsubishi Lancer Alternate 1 | `6r4` | MG Metro 6R4 |
| `ma2` | Mitsubishi Lancer Alternate 2 | `str` | Lancia Stratos |
| `ma3` | Mitsubishi Lancer Road Car | `205` | Peugeot 205 Turbo 16 |
| `cor` | Toyota Corolla | `pum` | Ford Racing Puma |
| `sub` | Subaru Impreza | `esc` | Ford Escort Mk1 |
| `206` | Peugeot 206 | `sea` | Seat Cordoba |

The disc also has three `aud*` files (an Audi, no `.cin`), not referenced by the car table.

Front-end pictures: `FrontEnd/res*.bfl` members `<fe-code>b01.tga` / `f01.tga` (back/front), with the
front-end codes from the exe (`f2000`, `lncer`, `crlla`, `crdba`, `delta`, `serra`, `strts`, `escort`, ...).

## Files per car

The name pattern is `<code><set><n>`, built by `Car_Load` 0x457000 (`%s%c%d.bfl`, `A1N.c3d`, `L.c3d`,
`S.c3d`) and 0x4760a0 (`%sa5`, `%sc5`). For the Focus 1999:

| File | Content |
|---|---|
| `f99.cin` | Car info (below) |
| `f99a1.bfl` + `f99a1N.c3d`, `f99a1L.c3d`, `f99a1S.c3d` | Set `a`, exterior: DXT5 textures (37), the full model (15 objects, 1,166 vertices) and two 344-vertex models (`L` and `S` are byte-identical for this car) |
| `f99a5.bfl` + `f99a5.c3d` | Set `a`, interior (cockpit view): `affint01.dds`, `revct.dds` (rev counter) |
| `f99c1.bfl` + `.c3d` | Set `c`, exterior: the same textures as TGA (`cffdbobu.tga`) |
| `f99c3.bfl` + `.c3d` | Set `c`, a lower level of detail (9 objects, 887 vertices) |
| `f99c5.bfl` + `.c3d` | Set `c`, interior |
| `f99d1`, `f99d3`, `f99e3` | Further levels of detail (550, 505, 400 vertices) |
| `f99f3`, `f99f4` | The lowest level (2 meshes, 189 vertices); `f3` and `f4` are identical here |

So the letter is a detail level (`a` highest, with DXT textures; `c`, `d`, `e`, `f` lower, TGA) and the
digit a part (1 exterior, 3 exterior at lower detail, 4 far, 5 interior). This reading comes from the file
sizes and member names; confirm it in `Car_Load`, which picks the sets from the game mode (cases 5, 6, 7,
11, 12 change the choice) and the number of players (split screen uses lower sets).

Models are [PP_F](c3d.md) kind 6. Object/mesh names give the parts: `01LFWhlLowL` (left front wheel, low),
`05BonCamDay` (bonnet camera), `06IntArc`, `08BodyArc`, `14LigEffect` (light effects). Damage, wheels and
lights are driven per part.

## .cin (car info)

676 to 1,068 bytes, no magic, loaded whole by `Car_Load` 0x457000 (`%s.cin`, kept per car at 0x5429c8).
It starts with a **directory of six `u16` offsets** from the start of the file (`f99.cin`: 0x00c, 0x1a0,
0x204, 0x258, 0x2a8, 0x2e4; the first is always 0x00c, the size of the directory). `Car_CinSection`
0x457e10 returns section *n* of a car (confirmed). The sections are almost entirely 16.16 fixed point
(`0x00003333` = 0.2, `0x0000cccc` = 0.8, `0x00010000` = 1.0, negatives as two's complement). Readers:

| Section | Read by | Content (inferred) |
|---|---|---|
| 0 | 0x463fe0 | 0x2c-byte records with a 16.16 position, two 16.16 factors (0.2, 0.8) and a byte code: light and effect emitters (headlights, brake lights, exhaust) |
| 3 | 0x4809e0 | 16.16 vectors, used when the cars are placed for the race |
| 5 | 0x476540 | |
| any | 0x486700, 0x494bb0, 0x498dd0 (section chosen by the caller) | |

Sections 1, 2 and 4 are reached through the callers that pass the index; their contents are read when
those functions are ported.

## Handling

The handling model is not a data file. It is in the simulation code (0x428000-0x445000), all 16.16 fixed
point (`(int64)a * b >> 16`):

- **Base parameters** are immediates in `Car_Init` 0x43c7f0 (confirmed): a `switch` on the car's handling
  class (0 to 13; the third argument, replaced in some modes through 0x40ee90) stores about ten 16.16
  values per class into the car state (`+0x1f8`, `+0x1fc`, `+0x200`, `+0x764` to `+0x774`, `+0x788`,
  `+0x794`) plus one per-class constant (0x3e80000, 0x4000000, ...). As instruction operands they are
  ported as code, with the original's values, not as a table.
- Ported (`src/sim/car.c`, checked by `tests/sim_test.c` on the 205): the class table, the set-up
  bytes (7 per car at 0x52f0e0, or a default per rally at 0x520128-0x520144). These are applied as:
  - suspension: 0x43dff0;
  - gears: 0x43e1f0, five sets of neutral, six forward and reverse, divided by 1.14;
  - brakes: 0x43e530;
  - differential: +0x7b4;
  - the box corners: 0x43eef0, 0x43f280.
- The rest of the car is set up in the same function from the model (wheel positions from the `.c3d`
  parts, `+0x360..0x378`) and the game options, and in 0x43e680 afterwards.
- The car state is 0xc24 bytes per car in the block at `*0x53c9a4` (pointers per car at 0x53aba8, `Car_Get` 0x42b5f0); the simulation addresses the current
  car through `DAT_0053cc18`. Known fields: controls `+0x1d0..0x1dc` (see the replay frame in
  [stages](stage.md#replays-rpl-grp)), handling class `+0xb1a`, step counter `+0xb43`, step fraction
  `+0xa90`, step rate `+0xa98` (float, 25.0), models `+0x71c` / `+0x720`.
- `DAT_0053cc1c` is **not** the handling parameters (corrected): it is the car's damage record, one of
  0x4d0 bytes per car (array at `*0x588b94`, with a second 0x290-byte record per car at `*0x588b98`), set up
  by 0x4667c0 and reached through 0x469680. The functions 0x4669f0-0x469b00 deform and detach the
  model's parts from it.

## Car sounds

`Sounds/Car/<engine>.bfl` (12 engines: `205`, `206`, `corolla`, `cosworth`, `delta`, `focus`, `lancer`,
`metro`, `mini`, `seat`, `stratos`, `subaru`), each a set of engine loops as 16-bit PCM WAVs.
