# Architecture of CMR2.exe

How the original program is built, as far as the first pass of reverse engineering shows. Addresses are
in `CMR2.exe` v1.11 ([the disc](../formats/index.md#the-disc)); names are the ones in
`tools/ghidra/names.tsv`. The function map by address range is in [inventory.md](inventory.md).

## Overview

- **Language and build.** C-style code (global state, free functions, no game vtables found so far); the
  C++ runtime imports (`_CxxThrowException`, `type_info`) serve at least the D3DX library. MSVC runtime
  `MSVCRT.dll`. One executable, no game DLLs; `binkw32.dll` for movies.
- **Statically linked libraries.** zlib 1.1.3 (0x4bdf00-0x4c4800), Microsoft D3DX 7 (the `CD3duContext`
  and `CD3duDeviceNode` classes, `d3dx.dat`; 0x4c4800-0x4d0000), the MSVC CRT pieces.
- **Platform.** Win32 window and message loop, DirectDraw 7 + Direct3D 7 (hardware or software T&L),
  DirectSound (+ DirectSound3D), DirectInput 7, DirectPlay 4 + DirectPlayLobby 3 (network), the ACM
  (MS ADPCM music), Bink. No threads of its own (`CreateThread` is not imported).
- **Numerics.** The simulation is 16.16 fixed point throughout (`(int64)a * b >> 16`; about 10,000 shifts
  by 16 in the decompiled code), the renderer is float (D3D vertices and matrices). Model vertices are
  float on disc; car and stage parameters (`.cin`, stage tables) are 16.16.
- **Globals.** Almost all state is in fixed global arrays (`.data` is 3.2 MB virtual); there is no heap of
  game objects. Each subsystem has getter/setter accessors (hundreds of 2-5 instruction functions at
  0x405000-0x408000, 0x4a9000, ...).

## Start-up and the main loop

`entry` 0x405626 (CRT) calls `WinMain` 0x4a9720:

1. `FindWindowA("Colin McRae Rally 2")`: a second instance exits.
2. Opens the debug log `c:\error.txt`.
3. Registers shutdown callbacks (`AtExit_Register` 0x49c0a0 is called by every subsystem that needs
   cleanup), initialises memory, timers and the config, then `Window_Create` 0x4a8140.
4. The loop: `PeekMessageA` while active (`GetMessageA` while minimised); when there is no message,
   `Input_Poll` 0x4b7a40 and then **`Game_Frame` 0x4d0780** run one frame. `WM_QUIT` (0x12) ends it.

So the original is already one-frame-per-call: nothing blocks inside a frame except loading, which runs
synchronously from a state's update. This maps directly onto gasm's per-frame entry point.

## Modes and processes

`Game_Frame` dispatches on the top-level mode (`Game_GetMode` 0x4057d0, global 0x52ea4c):

| Mode | Handler | What |
|---|---|---|
| 0 | `Game_Frame` itself | Front end: process group 0x817da0, state table 0x523bc8 (10 states) |
| 2 | `World_Frame` 0x4ff450 | Between front end and race: rally/stage screens, service area, loading (state table 0x526ee0, 7 states) |
| 3 | `Race_Frame` 0x41b060 | The race: state table 0x5190b0 (14 states), one process per player |

Each group is run by a small **process framework** (0x49c0a0-0x49c400):

- `Proc_Init` 0x49c190 sets up a group: number of processes, the process slots (8 bytes each: a state word
  and a frame counter), a state table of `{update, draw}` function pairs, and a transition table of
  `(state, event, next state)` byte triples.
- Each frame: `Proc_UpdateAll` 0x49c2c0 calls every process's state update, `Proc_DrawAll` 0x49c310 every
  draw (skipped for one frame after a switch), `Proc_Advance` 0x49c370 applies a pending transition (state
  word bits 24-25 = pending, bits 16-23 = next state) or counts frames in the state.

`Race_Frame` creates one process per player (the count from `FUN_004074f0`, up to 4 for split screen /
alternate play), all in the same state table, so each player's view runs its own state machine (start
countdown, driving, finished, results ...). The race states (update / draw):

| # | Update | Draw | # | Update | Draw |
|---|---|---|---|---|---|
| 0 | 0x41bf80 | | 7 | 0x41d2b0 | 0x410e20 |
| 1 | 0x41f420 | 0x411070 | 8 | 0x41d7a0 | 0x410e70 |
| 2 | 0x41c0e0 | 0x410c80 | 9 | 0x41db10 | 0x410ee0 |
| 3 | 0x401000 | | 10 | 0x41e350 | 0x411020 |
| 4 | 0x4010a0 | | 11 | 0x41e8d0 | 0x501680 |
| 5 | 0x401150 | 0x410de0 | 12 | 0x41f4e0 | |
| 6 | 0x472e00 | 0x4759d0 | 13 | 0x41e5c0 | 0x410fa0 |

`0x501680` (a shared "draw the current screen" function) also appears in the front-end and world tables.

The port keeps the framework as it is (`src/game/proc.c`): the state tables are code addresses, so the
port has its own C tables in the same order, and transitions stay data-driven (the race's table is read
from 0x519120 at run time). A slot's word holds the state (byte 0), the kind (byte 1: for the race 0 one
player, 1 split screen, 2 and 3 the championship cases, 4 alternate players), the next state (byte 2)
and a pending switch's priority (bits 24-25). `Proc_Event` 0x49c1c0 takes the first transition whose
state and kind match (0xff any) for the event, unless a switch of the same or higher priority is pending.

The race's transitions (0x519120): 0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 7 -> 8 -> 9 -> 10 -> 11 -> 0 on event 0;
from 5, event 5 to 6 (the intro camera), 6 to 13, 1 to 10; 1 to 12 on event 1; 11 to 4 on events 3 and
7 (a restart); any state to 5 on event 4 and to 11 on event 1 (a failed load).

| State | What |
|---|---|
| 0 | The race's variables cleared; for the first player the selection, the common archives, the car tables |
| 1 | The loading screen, until it is up (0x41f410) |
| 2 | The stage archive (`Bfl_MountStage` 0x455260), the countdown's first frame flagged (0x537f08) |
| 3 | `Stage_Load` 0x41f930; on a failure every process to 11 and the front end |
| 4 | The cars (0x420150: `Cars_Load` 0x456d90, the car lists 0x42b660), then the cars flagged to be placed |
| 5 | The cars on the line (0x424ed0, 0x424c50), the cameras (0x422fe0), one round of steps |
| 7 | The countdown: on its first frame the step clock (0x537f24) and the stage timers restart; 500 1/100 s of steps on the line (`Car_StartLineControls` 0x47b640: the handbrake held), then the timer restarts at the go |
| 8 | The stage: steps with `Car_RaceControls` 0x47b000 (the launch 0x43f570 when the automatic box first takes over) until the player is past the finish (0x4582b0), whose time (`Timer_Centis`) is noted |
| 9 | After the finish: the car brakes to a stop (0x47b870, 0x47b7b0); four seconds later the finish counts as shown; once every player's is and no fade runs, on to 10 |
| 10 | The replay, until the key (0x4d0580); 0x41f2b0 sends every process on |
| 11 | The results; in a rally on to the next stage through the world mode (2) |

The stage time is the step count: `Race_Clock` 0x41d290 is the steps since the countdown's start times 4
(1/100 s); the timers 0x478130-0x4781c0 (start 0x58da10, elapsed 0x58da30) run on it. The countdown
and the timing are therefore exact in steps, whatever the frame rate.

## Game state

The original keeps its game state in globals and tables in its data section: modes, options, the
stage selection, championship and team tables, and car set-ups. The port maps that section (0x516000
to 0x831c7c) into a run-time image (`src/game/mem.c`):
- the first 0x14000 bytes come from the exe at start;
- the rest is BSS, which is zero.

Ported code reads and writes these variables by their addresses (`gm32(0x52f2ac)`), with the
one-line accessors of 0x405c00-0x409000 in `src/game/gstate.h`. Nothing is copied into the source, and
a table the game fills at run time (the weather at 0x52f100, the set-ups at 0x52f0e0) behaves as it
does there. The port's own structures (objects, meshes, the device) are not kept in the image.

The car state (0xc24 bytes per car) is kept as the original's block and addressed by offset
(`src/sim/car.h`). The pointers the original stores inside it are kept beside it.

## Timing

`Timer_Sample` 0x4a9b70 latches `timeGetTime()` once per frame; `Timer_Centis` 0x4a9b80 is a monotonic
clock in 1/100 s built on it. What fades per frame rather than per step (the lamps' lenses, the weather)
is scaled by 25 over the measured frame rate ([the frame scale](world.md#lamps), 0x466630). Stage times are per player (0x58da10 start, 0x58da30 elapsed, penalties added
in whole seconds by 0x478190).

**The simulation runs in fixed steps** (confirmed). Each race frame, `Race_Update` 0x41c5a0 asks
`Sim_StepCount` 0x42c890 how many steps are due, runs that many, then interpolates the drawn poses:

- The car array is at `*0x53c9a4`, 0xc24 bytes per car. Each car has a step rate in Hz, a float at
  `+0xa98`, set to 25.0 by `Car_Init` 0x43c7f0 (the only writer). A step is therefore 40 ms.
- `Sim_StepCount` keeps three latched times: the race clock origin (0x53bd64), the last frame (0x53a374)
  and this frame (0x53bd40). For each car it converts both to steps
  (`round(ms * 0.001 * rate)`, the constant at 0x5113a0), takes the difference as the number of steps
  due, **capped at 5**, and stores it in the car's step counter `+0xb43` (byte). The fractional part of
  the current step, times 65536, goes to `+0xa90` (16.16). Cars with the same rate as car 0 copy car 0's
  values. It returns the largest count; car 0's fraction is the frame's interpolation factor (0x537f60).
  On a pause or restart (0x404f30, 0x41f360) the last-frame time is reset, so no steps are owed.
- Per step: every car's controls are produced (player input, AI or replay, by the car's driver type,
  0x47b000 / 0x47b640 / 0x47b7b0 / 0x47b870 / 0x47d330), the replay frame is recorded or read, then
  `Sim_Step` 0x42baf0 advances the whole world once (cars 0x433890, collisions, particles, damage,
  co-driver distance, ...) and `Sim_StepDone` 0x42bc80 decrements each car's counter.
- After the steps, `Race_PoseInterp` 0x41d060 builds the render matrices of the cars and wheels from the
  last two states with the fraction (0x42bf70 and the effects that follow), so drawing is smooth at any
  frame rate.
- Race state 7 (0x41d2b0) and 8 (0x41d7a0) count the start countdown with `0x537fc0`, the time since the
  start in 1/100 s (the 500 there is the five-second countdown, not a stall test).

For the port this means a deterministic simulation for free: drive the three latched times from a clock
advanced by the gasm frame period, and the number of 40 ms steps per frame follows (at 60 Hz: 1 or 0
steps, averaging 25 per second). Replays store one input frame per step (see
[stages: replays](../formats/stage.md#replays-rpl-grp)), so a replay played through the ported `Sim_Step`
must give back the original run exactly.

## Walls

Sim_Step's collision pass 0x4877a0 runs after the car step and before 0x42cd00. It clears each car's
flat box (0x98 bytes per car at 0x590ed0), runs the car-to-car contacts 0x48a1f0 in the modes that
have them (0x46bd50), then per car (the list from its end) the scenery objects 0x4878a0 (with +0xb64
clear) and the walls 0x48e0a0, and keeps the box's four corners for the next step. Ported in
`src/sim/walls.c`, the scenery in `src/sim/scenery.c` and the damage in `src/sim/damage.c`
([below](#damage)), the car-to-car contacts in `src/sim/contact.c` ([below](#car-to-car)); knocking
scenery down is pending.

- **The box** (0x486c30, once a step): the forward row flattened (the up row when the car points
  nearly straight up or down) and made unit, the side axis across it, the half lengths as the furthest
  of the eight box corners (+0x270) along each, the corners' y range; four corners at the top y
  (0x486fc0).
- **The walk** (0x48e0a0): the walls of the car's cell, skipping those with both ends on one side of
  the car beyond 1.2 times its radius (+0x758) in x or in z, and those above or below every box corner
  (0x48f400). Per type (see [fin.dat](../formats/stage.md#fin-dat)).
- **A face** (0x48fb80): the wall's line within reach and, for one-sided walls, the car's last position
  (+0x2e8) not in front of it (0x490640; two-sided 0x490570). The corners between the ends are sorted
  by side (0x490720); the box is pushed out along the normal by the deepest corner's distance. If the
  velocity points into the wall, the impulse is the normal part times -(1 + restitution) plus the
  tangential part times -(friction x dt), applied at that corner.
- **An end** (0x48e580, 0x48e730): an end within the car's radius and inside the box is pushed out
  along the car's approach (last position to the end), through the nearer face. With a cap angle the
  velocity into the cap's direction bounces by the restitution plus 0.3; without one, the velocity
  along that face is taken off with a bounce of 1 plus the restitution.
- **The impulse** (0x48f470): added to the velocity (twice with option 4), and unless the car is
  already loose (+0xc00), a turn (+0x5d0) from the impulse (times 2.5, at most 3.3) about the contact
  point. With +0xb64 clear, an impulse over 0.9 makes the car loose (the free body 0x42eae0 takes it)
  with the point taken four units lower; at 1.2 or more the turn is four times as strong and the car
  is lifted (0.25 upward).
- **The strongest hit** of a wall (impulse, point, normal) goes to the damage 0x466ef0 at most every
  ten steps (+0xb42).

## Scenery collisions

A scenery mesh carries its collision volumes as a list of the C3D's 0x5c records (first at the mesh's
+0x10c, linked through +0x58, the count in the byte at +0x110). A record's type byte +0x00 is 0 for a
box (a 16.16 matrix at +0x04, the width +0x44, depth +0x48 and height +0x4c), anything else for an
upright cylinder of radius +0x44.

At the end of Stage_Load, 0x471dd0 sorts every cell's volumes into two kinds of lists by the record's
flags (+0x10):
- fixed (no flag 0x2001000);
- movable (0x2001000), with a byte of per-car "still up" bits (0x58c938) and a flag (0x58c958).

Each volume (0x58ca70, 8 bytes) has a reach: a cylinder's radius, or half a box's diagonal. A box
volume also gets a 12-byte box (0x4873f0), in 7.9 fixed point:
- the flat forward axis (x, z), from the placed matrix's row 0, or row 1 when row 0 is nearly vertical;
- the eight corners' highest and lowest y;
- their furthest reach along the axis and across it.

In a two-player game, records without flag 0x8000 are hidden (their byte +0x14 cleared) and skipped.
On France stage 2 the fixed volumes are all cylinders:
- 420 with flags 0: tree trunks, of radius 0.14 to 0.25;
- 236 with 0x8000, of radius 0.25;
- 171 with 0x4000, of radius 0.75 to 1.12.

The 17 movable volumes (0x46008300 and 0x6008300) have radius 0.65.

`Car_Scenery` 0x4878a0 (from the collision pass, with +0xb64 clear):
- **What it tests:** the fixed lists, then the movable ones, of the car's cell and its three
  neighbours (+0xb00-+0xb06). With +0xc18 set, only the movable ones.
- **Which volumes:** a volume is tested when the car's sphere (+0x758) and the volume's reach
  overlap in 3D (0x487b80). A movable one must also still be up for this car (0x471d40). One that is
  down is placed 1,000 units higher, so that it never overlaps (0x471cc0).
- **Low objects** (flag 0x4000): with +0xc20 clear, the wheels are bumped when the centre is within
  the flat box widened by the radius (0x487e50, then Car_WallBumps 0x48df50).
- **A cylinder** (0x489b20) is tested against the car's flat box, which takes no account of height.
  The normal depends on where the cylinder lies:
  - beyond both faces: the nearest corner's ray along the centre line meets the circle (kind 3). The
    normal runs from the centre to the contact point.
  - otherwise: the face with the shorter time along that line (kind 1 forward, kind 2 side). The
    normal is the face's axis.
- **The push:** the car (its position, eight corners and flat box corners) is pushed back along the
  line by that time (x and z). For a movable object the scale is zero, so it is not pushed.
  0x489750 then keeps only the push's part along the normal. It stores the contact in the car's
  frame (+0x5dc) and the response's thresholds 0.5 and 1.342.
- **The response** (0x48be20) takes the contact (its y zeroed, clamped to the half size +0x204), the
  velocity there (with the spin +0x420), and that velocity's part along the normal:
  - above 1.342, a hard knock; above 0.5, a knock. Either makes the car loose (+0xc00, +0xc04,
    +0x96c), unless it already is or +0xb64 is set.
  - against a movable object, the impulse is a hundredth, there is no knock, and the object goes
    down (0x46fe70, pending).
- **The impulse:** the velocity along the normal times 0x519c90 goes into +0x5c4; with option 4 it
  goes twice into the velocity instead. The damage (0x466ef0) is called once per object in a row of
  steps, listed at +0xad6/+0xae0 (up to five). For a cylinder it gets the centre and radius (kind 1);
  for a box, the first car corner inside it (kind 0), else the first of its corners inside the car's
  box (kind 2), else no call. The walls call it at the walk's strongest hit, with the impulse over
  (1 - the wall's restitution) put into +0x5c4 for the call. They skip it within 10 steps of the last
  (+0xb42), and pass 0 for a face and 2 for an end. A landing or a roll passes the lowest box corner,
  straight up.
- **The turn:** the impulse is crossed with the contact (times 3 for a cylinder, 1.5 for a box). For a
  knock it is crossed with the contact lowered by one unit along the car's up axis (four for a hard
  knock) and taken four times, and a hard knock also lifts the car by 0.25. The result goes into
  +0x5d0, clamped by 0x48c750.

**Boxes.** A box (0x487c40) is rebuilt for each test from its 7.9 record in the car box's layout,
with its four corners at the lowest y. Over the whole disc there are 6,779 fixed and 3,487 movable box
volumes, against 256,185 cylinders. `0x488640` first rejects boxes that do not overlap in y. It then
takes the motion as the flat line from the car's last position (+0x2e8) to the object, and pushes the
car back along it:
- by the largest time at which an object corner inside the car's box leaves it through the nearer
  face;
- then the same for the car's corners inside the object's box, along the reverse line in the object's
  frame;
- with neither, by the largest time among the crossing edges (0x488de0 and 0x489060). The edges come
  from the corner order {0, 1, 3, 2} at 0x51fadc.

A movable object takes the push itself (0x4894b0), and since its box has no corners to move, the car
goes through. `0x487f60` then builds the contact point and normal:
- the contact point is the mean of the listed corners;
- the normal is the sum of the axes of the corners' faces, made unit;
- for a fixed object with corners listed on one side only, the push is kept only along the normal;
- the response thresholds are 0.5 and 1.34.

The cars the pushed car touches (the contact lists, [below](#car-to-car)) move with it
(Cars_PushGroup 0x48c870), by the normal part of the push (`n (n . p)`), not by the move the car took
(`n (n . p) - p`): a quirk of all three callers that keep the push along the normal (0x487f60,
0x489750, 0x48a5f0), kept. The walls' pushes (0x48e580, 0x48fb80) move the touching cars by the move
itself, without their flat boxes.

## Car to car

Cars_Collide 0x48a1f0 runs in the collision pass when the mode has the cars collide (0x46bd50). Ported
in `src/sim/contact.c`.

- **The pairs:** every pair of the list, when one of the second car's four cells (+0xb00..+0xb06) is
  one of the first's (an unset `-1` matches too) and their spheres (+0x758) overlap (0x487b80). Such
  cars are marked as near. Cars with +0xc08 or +0xc0c, or both with +0xc18, are skipped.
- **The test** (0x48a5f0): both flat boxes made, then 0x488640 with the second car as the object and
  each car taking half of each push (the second car's box record points at its corners and position,
  +0x90 and +0x94, so it moves). The contact point is the mean of the listed corners (each car's
  eight-corner points), relative to the first car; the normal is the sum of the met faces' axes, made
  unit. With corners listed one way only, each car's push is kept along the normal and the cars it
  touches move with it. The contact goes into each car's frame (+0x5dc). The pair is then listed as
  touching (0x5913dc counts, 0x5913f8 eight per car), until the next step's pass.
- **The response** (0x48ae90): the contacts clamped to the half sizes in x and z (0x48c6e0); each
  contact point's velocity (spin across it, turned into the world, plus +0x408) along the normal. The
  common velocity is the two weighted by the masses (+0x75c). The restitution term is the difference
  times 0 (`0x48b0c7`), so the hit is fully inelastic. Each car's change is the common velocity less
  its own.
- **A knock:** a change above 0.6 makes a knock (unless the car is already loose, +0xc00, or both
  cars have +0xb64): the contact is lowered by four units along the car's up column, the car is made
  loose and +0x96c set to 1.0.
- **Not upright:** a car that is not upright (+0xb35 not 1) passes a downward share of its impulse to
  the other.
- **Applying it:** the impulses times 0x519c90 go into +0x5c4 (world). Each one's turn about the
  contact goes into +0x5d0, twice (four times for a knock), clamped (0x48c750).
- **The damage:** each car is damaged at the first listed corner (kind 0 for the car whose corner it
  is, 2 for the other). With option 4 each impulse is turned into velocity (twice, less the impulse).
- **The sparks:** when the sliding speed (the relative velocity less its normal part) is above 0.2,
  sparks fly along it at each car, at 0.8 of the corner's flat offset, with a strength of twice the
  speed (at most 1.0), up to 9 of them, 0.4 of them chips (0x499750). There are none while either car
  is recovering (+0xb70). With no corner listed (the crossing-edges case) the original's locals still
  hold the two impulses and are used as the offsets (kept).
- **After the pairs:** a car's +0xc08 (passing through the others) lasts while +0x970 runs down by the
  step, then until it is near no car; +0x974 counts its time at a quarter. With the option 0 weapon
  (the super special, or selection bits 24 or 27) each car's box is tested against the balls
  (0x47d850, pending).

## Damage

`Car_Damage` 0x466ef0 and its helpers (0x4667c0-0x46b400, `src/sim/damage.c`). Confirmed from the
code unless marked.

**Records.** Each car has a 0x4d0-byte record (base pointer 0x588b94, `Damage_Get` 0x469680) and a
0x290-byte hit log (0x588b98). The record:

| Offset | Contents |
|---|---|
| +0x000, +0x03c, +0x078 | 15 slots: the mesh, its object, its rest vertices (pointers) |
| +0x0b4, +0x168 | per slot the rest box's centre and half size (3 ints each) |
| +0x21c | 9 cells, a 3 x 3 grid over the car (rows by z, columns by x), at most 100 |
| +0x240 | 34 panels, at most 1.0; +0x2c8 their sensitivities; +0x350 the part carried from earlier stages |
| +0x3d8-+0x408 | the handling factors the simulation reads: steering pulls (+0x3d8-+0x3e8), the wheels' drag (+0x3ec-+0x3f8), brakes (+0x3fc, +0x400), engine (+0x404), crumple (+0x408) |
| +0x410-+0x41c | the meshes' extremes: largest x, smallest x, largest z, smallest z |
| +0x420, +0x45c | per slot the vertex count; the slot count |
| +0x460, +0x470, +0x490 | 8 glass panels: an id byte each, cracked and broken flags |
| +0x4b0, +0x4c0 | the fallen-off part flags (`Damage_Flag` 0x469bc0), three part values |

The log holds two lists of 20 entries of 13 bytes, each followed by its count and first entry: +0x000
the hits carried in, +0x106 this stage's. +0x20c and +0x24c are the panels (34 bytes, in 255ths), the
part flags and values, carried and current; +0x28c stops the logging. An entry is the strength (255ths),
the kind, a cylinder's radius (25.5ths), the normal and the up axis (127ths) and the point (12.7ths of a
unit), the last byte the next entry (0xff none).

**Set-up.** `Cars_Load` 0x456d90 clears the records (0x4669b0) and the logs (0x4668d0, 0x466920) and
calls `Damage_Setup` 0x4669f0. Dents are on per car (0x588970, set by `Car_Load` outside modes 5-7, 11
and 12, 0x46b400). The slots are the mesh objects of the second model and its branches, down to a
type-0x14 object, each in slot type - 5 (0x466e90); empty slots are squeezed out. A player's car takes
its carried log from 0x52ebc0 (0x148 bytes per player). The first set-up of a race keeps every slot's
vertices as rest vertices (0x46afe0, 0x20 bytes each): the position and normal in 16.16, and from the
vertex colour the limits: red, green and blue less 0x80 are the most a vertex may move in x, y and z,
in 64ths of a unit, with the sign giving the side; the dent direction is their negation. So the car
models carry their crumple zones in their vertex colours (inferred from the code). Each set-up puts
the meshes back to rest (0x46acb0) and runs
`Damage_Reset` 0x469690, which zeroes the factors, replays the carried hits (each entry decoded,
0x4688b0, then `Car_Damage` as a replay) and takes the carried panels.

**A hit.** After the sound and the shake, with dents on: the strength is |+0x5c4|; at most 0.1 (the
table at 0x51bf94, per kind) nothing more happens; it is then scaled (0x51bfa0, 1.0) and clamped to 1.
The hit is turned into the car's frame and logged (0x468520: a new entry, else the weakest if weaker,
moved to the end); it is then read back from the entry, so the dent is made from the quantised values
and a replay gives the same dent. Then the dent:
- **Flat** (kind 0, a car corner or a wall face; 0x467700): the point is the car-relative hit point
  projected on the normal, then moved along the normal to the farthest of twelve hull points behind it
  (0x4675c0: the box floor's corners at the meshes' extremes, the same raised by the wheel radius, four
  wheel points). Radii r1, r2, r3 are 0.35, 0.6 and 0.7 of the strength. A vertex within r1 of the
  plane moves along the normal, towards the middle, by r1 - d^2/r1; one out to r1 + r2 along its dent
  direction by (d - r1)/r2 x r3/5 times a factor from bits 7-9 of its x + z (-1 to 0.875).
- **Round** (kind 1 a cylinder, else an object corner or a wall end; 0x467e90): the distance is from
  the upright line through the point (the orientation's second column), the radius the cylinder's
  (at least 0.25; 0.25 for kind 2), r1 = r2 = r3 half the strength, at most the radius; the normal is
  turned to face the middle and the point moved r1 along it.

Each move is kept within the vertex's limits from rest (0x508740), and the vertex's normal is moved by
the same amount (as the original does). A changed mesh is uploaded again. Then, unless replaying:
the cells the hit's contact (+0x5dc, +0x5e4) reaches gain 0.2 of the strength (0x468a80); the panels
are the cells times their sensitivity plus the carried part, and give the factors (0x468c10; only with
+0xb50, damage on in the mode); the crumple is the mean over every vertex of five times its distance
from rest (0x469100); glass past its limits (0x51bfac to break, 0x51bfcc to crack) changes the polygons'
texture frames (0x4694a0) and plays 0x418cd0 (0x4692f0).

**Between stages.** At a car's finish `Damage_Finish` 0x469a80 (once; it then sets +0x28c, so later
hits dent but are not logged) keeps the panels in 255ths (clamped above only), the part values and
the fallen-off flags as this stage's. When the replay ends (0x41f2b0) `Damage_Store` 0x469b50 copies
each player's log (+0x106, 0x106 bytes) and panels (+0x24c, 0x40 bytes) to the carried damage at
0x52ebc0 + 0x148 x (the race's first player + n) (0x407610; the panels at +0x108), which the next
set-up reads. The pause's restart zeroes all of it (0x408290) in modes 0-5 and 8-10.

**The misfire.** Each step (0x46b4e0, at the end of the cars' first pass) an engine panel (23, +0x29c)
over 0.5 gives a badness b = 0.8 x clamp(2(p - 0.5), 0, 1). A countdown (+0x40c, 1.0 a step) runs
two phases, +0x4cc set while cutting out: running for (1 - b)(250 r + 50) steps, then cutting out for
15 b r, with r = rand() / 32767 in 16.16 (through the FPU: the float 0x5112f0 and the double 65536
at 0x5112e0). Only this function touches +0x4cc; the countdown's reader is not found yet.

**The loose parts** (0x480870-0x484d30, `src/sim/parts.c`). A car has up to four panels of its
second model that can work loose: object types 9, 10, 12 and 13 (0x51f888), registered by their
damage slots (0x590c24).

- **Loosening** (0x480de0). A part comes loose once its damage panel passes its threshold. It takes
  a 0x1a0-byte record (four arrays at 0x590d7c), with the hinge, pivot, inertia and length set up
  (0x480e50).
- **Each step** (0x480bb0, from `Sim_Step`): first the car's wobble (0x480cb0). The wobble follows
  the speed (+0x778 over 1.5, at most 1) times a value from the position, by 0.06 dt at most. Then
  each loose part runs its type's update:
  - type 0 swings about z by the wobble (0x4814d0);
  - type 1 is the hinged panel (0x4816f0), with its spring, swing, torque and break. Past 0.8 on its
    panel it hangs from one end (0x483050). It scrapes sparks on a sparking surface under its corner
    wheel (0x483100; the sparks are not ported). Once its displacement passes the car's size plus its
    own, it falls off (0x482f30): the damage record's part flag is set (0x469bf0) and the part is
    hidden when drawn (0x46b670).
- **Drawing.** Each frame the drawn matrix is blended between the last two steps (0x484d30).
- **Not ported:** types 2 and 3 (0x484310, 0x483570; their parts stay put), the .cin pieces
  (0x484e00), the glass pieces (0x49a910, 0x49ab20), the relighting marks (0x4b4100) and the
  restart's set-up (0x42b800).

## Cameras

The race cameras (0x421720-0x423ff0; `src/game/camera.c`). Each player has two camera records of 0x64
bytes at 0x539018 (+0x00 the record's index, +0x01 the player, +0x02 the car followed, +0x04 the kind,
+0x08 the matrix with rows side, up, forward and the position, +0x48-+0x60 the projection's terms and a
ground lift), one current (0x538e0c). `Camera_Select` 0x421720 makes the other record the new kind and
either switches at once or blends the two over 12.5 steps (0x538d20). Each step (`Camera_Step`
0x4219b0, from Race_Update) the followed frame is the car's orientation (0x423b20 into 0x538ca0), the
last step's camera is kept (0x538e38) and the current one steps (0x423460); each frame `Camera_Frame`
0x422140 blends the two by the step fraction (0x423de0 through the frame blend 0x4224e0) into the
camera object's matrix.

Kinds: 1, 2 and 10 the interior views (0x486740), 3 (0x476410), 4-6 the chase cameras (0x447530), 7 the
trackside cameras along the `.tsc` splines (0x48cae0). State 5 takes the kind from the player's
profile (+0x44, 0x408800), the chase when it cannot be had. The chase camera (ported) keeps per record a
forward and an up axis, eased each step towards the car's (dt over the distance, times the car's
+0xa74), and sits at the profile's offset in them (+0x5e0: 1.5 up, 6.5 back by default; the height and
the pitch, 0x2d, eased for longer distances by 0x447d20), pitched down by the angle. Kind 5 takes its
offset from the table at 0x519ea0, kind 6 is pitched 45 degrees.

The profiles (0x52fa1c, four of 0x650 bytes) are set at start-up by `Profiles_Init` 0x4eadb0: camera 4,
the automatic box, the chase defaults; the front end gives each player a profile (the player record's
bits 18-21 at 0x531350).

## Recovery

A car is flagged off the stage (+0xbf8) by a wall of type 6, 0x18 or 0x19, a surface of class 0xf for
25 steps, 12 steps with every corner off the collision mesh (+0xa80), 100 steps on its upper corners,
or a roll (a row held steep for 100 counts starts the roll's damage, whose counter then runs down).
Sim_CarsPost's check 0x431c10 starts a fade on the car's channel (0x4283e0: 12.5 steps out, then in;
the fade timers 0x428410-0x4285b0 are ticked by Race_Update after each step for each car with a step
due). At the dark point the callback 0x431c50 takes the centre-line point the car had reached, moves
back past points another car is within reach of (0x431d80, when cars collide) and past points flagged
2, and puts the car there (0x431ff0): heading along the line's tangent, 100 units down and lifted onto
the ground, at rest, in first gear, with its history, drivetrain and loose state cleared.

## Files

All reads go through `File_Load` 0x4aa220 (zlib `gzopen`, `CMPR` check, plain fallback) into memory, and
most data is then used in place. Archives are mounted into slots and members found by name
([bfl.md](../formats/bfl.md)). Paths come from the registry ([formats](../formats/index.md#paths-and-registry)).

## Rendering

Direct3D 7 through the D3DX 7 context (`D3DXCreateContext`-style wrapper `CD3duContext`): the device is
`*(PTR_DAT_00520b78 + 4)`. Device methods used (vtable offset: count of call sites):

| Method | Sites | Method | Sites |
|---|---|---|---|
| `SetTextureStageState` (0x94) | 99 | `SetTransform` (0x2c) | 18 |
| `SetRenderState` (0x50) | 71 | `DrawIndexedPrimitiveVB` (0x80) | 10 |
| `DrawPrimitive` (0x64) | 8 | `LightEnable` (0xb0) | 7 |
| `SetLight` (0x48) | 6 | `GetTransform` (0x30) | 5 |
| `Clear` (0x28) | 4 | `BeginScene` / `EndScene` (0x14 / 0x18) | 3 / 3 |
| `DrawPrimitiveVB` (0x7c) | 3 | `SetViewport` (0x34) | 3 |
| `DrawIndexedPrimitive` (0x68) | 1 | `SetTexture` (0x8c) | 3 (through helpers 0x4a4850, 0x4aac00) |

So: fixed-function T&L with hardware lights, two texture stages (the models carry two UV sets), vertex
buffers built at load (`FUN_004b1d00`), indexed triangle lists for the world and cars, `DrawPrimitive` for
2D (HUD, front end, particles). Render-state code is concentrated in 0x49d3f0-0x49e100, 0x4a3650,
0x4b2980, 0x4b6f00 (with `LightEnable`) and 0x4bb2b0-0x4bdb60.

Resolution: two resource sets, 640x480 (`*640*`) and 1024x768 (`*1024*`); 2D is laid out per set. Texture
memory and vertex-buffer statistics, polygon counts and "Using Hardware/Software T&L" are printed by the
debug overlay (0x40f0c0, 0x410c80).

**Port:** a small Direct3D 7 state emulation on OpenGL ES 3.0 (`gasm:gl`, WebGL 2 rules): render and
texture-stage state into a shader key, the fixed-function lighting and the two-stage combiner in GLSL ES,
vertex buffers in the original's FVF. This is the same approach as OpenNFSPU's THRASH layer, but at the
D3D7 API level ([renderer](render.md)).

## Sound and music

DirectSound buffers for the effects (PCM WAV from the archives), DirectSound3D optional, engine loops
pitched by RPM. Music: one `.adp` (MS ADPCM) streamed through the ACM into a streaming buffer
(`Music_Open` 0x4a28d0, `Music_Update` 0x4a2bd0). Speech: pace-note samples queued by the co-driver code
(0x416710-0x418550). Movies: Bink with its own DirectSound output.

**Port:** one mixer rendering 44,100 Hz stereo per frame (gasm audio push), as OpenRF: `src/audio/dsound.c`
stands in for DirectSound's buffers (create, duplicate, lock and copy, play looped or once, stop,
status, volume in hundredths of a decibel, pan, frequency). On it the original's sound layer
(`src/audio/snd.c`): `Snd_Load` 0x4a1f50 reads a WAV member's 44-byte header at fixed offsets into a
buffer; up to 200 sounds (0x5a23ec) and 32 voices (0x6e0d6c), each named by a handle (slot in the low 6
bits, the sound above); a voice whose sound another voice plays gets a duplicate buffer; its volume
(0..0x10000, times the master 0x5210f4) becomes the attenuation `-10000 - round(-10 ln v) * 10000 /
round(10 ln 65536)`.

The engine sounds (`src/audio/engine.c`): per player car, `Sounds\car\<engine>.bfl` (the engine name
by class at 0x51f044, the suffix at 0x51f15c) gives thirteen WAVs in order (fs, rs, fm, rm, whine, ft,
rt, chatter, det1-4, detidle). Each step (0x47a710) the revs become rpm (6,500 a unit through +0x798)
and 0x479360 mixes three bands (tables at 0x51ea60, 0x51eab8, 0x51ec50: steps over a range of revs, a
u16 frequency and a percent volume per step): the slow pair, the medium pair off the throttle, the fast
pair on it (looped from a per-class offset, 0x51f2a0, 0x51f2d8), the front louder from the interior
views, all faded with the camera's distance (0x427d50: full within 2 units, silent beyond 100) and at a
third of the effects volume option (0x427c10).

The collision sounds (`src/audio/crash.c`, confirmed): the stage load (state 3 at 0x401058) loads the
surfaces' sounds (0x418ff0, not ported) and then 0x4188c0. That loads 21 WAVs from the common archive,
each named `<sounds>\collision\<name>` (0x518a34), in three tables of name pointers:
- 0x5188e4: SWIPE1-4 and COLLIS1-6 (first id at 0x537360);
- 0x51890c: ROLL1-4 and LAND1-3 (0x5373ac);
- 0x518928: HLIGHT1, HLIGHT2, SCREEN1, MIRROR (0x5373a8).

They play on four slots (0x5373b0 handles; 0x537364 player, 0x537374 start time, 0x537384 volume,
0x537398 car). `0x4187d0` takes the first free slot, else the one started longest ago (the first of
equals), and stops its voice. It plays at 44,100 Hz at fade(car, player's camera) (0x427d50) times the
base 0x537394 times the call's volume. Each step, beside the engines, 0x418b00 frees the finished slots
and sets the volumes again. The pause stops them (0x418780). Both 0x418b00 and 0x418780 look at the
first (players) slots only, so with one player only slot 0 follows the camera. The base is two thirds
of the effects option (0x427c10's call to 0x418d20).

The three ways in:
- **0x418ba0 (a hit):** above 0.05, SWIPE1-4 and COLLIS1-6 by tenths of the strength, at full volume.
- **0x418c30 (a roll):** ROLL1-4 at random, or LAND1-3 with its flag, at the strength as the volume.
- **0x418cd0 (glass):** HLIGHT1 or 2, or SCREEN1, when a glass panel cracks or breaks (0x4692f0, [damage](#damage)).

The first two raise the car's +0xb46 to 1 + 7 times the strength, at most 8.

`Car_Damage` 0x466ef0 makes the sound in its first block, unless its last argument is set (as in
`Damage_Reset`'s replay of carried damage):
- the strength is |+0x5c4| clamped to 1;
- for a player's car on its wheels (+0xb74) it plays 0x418ba0, at most every 10 centiseconds per player
  (0x588a88; any hit stamps it, even one too weak to sound);
- off its wheels it plays 0x418c30;
- for every car it then shakes the view (0x4241d0): it rotates the impulse into the car's frame and
  raises the player's record at 0x539200 (14 ints): [9] from the third axis's part (the sign in [12])
  and [10] from the length, each 2.5 times the excess over 0.1, at most 1.

0x539200 is indexed by any car's number, so cars 4-7 write past the four players' records. The port
keeps the record, and the times at 0x588a88, in the game state at their addresses. The shake's readers
are the steering wheels' force feedback (0x423ff0-0x424c00), not a camera:
- **The set-up** (0x423ff0, 0x424120). For each player whose profile has force feedback on (0x40bd30),
  the record gets the controller's device and its settings. Three DirectInput effects are made on it,
  a spring, a damper and a constant force (0x4ab030-0x4ab5c0, `DICONDITION` records at 0x6664a8).
- **Each step** (0x424710). The effects follow the front wheels' loads and the shakes (inferred from 0x4247a0's car fields).
- **Without a device** the effect calls fail and nothing is felt.

gasm has no force feedback, so the port leaves these out.

## Input

DirectInput 7 (`IDirectInput7A`): keyboard and game controllers. `Input_InitDefaults` 0x49f0e0 sets the default
keyboard bindings as DirectInput scan codes for two keyboard players: player 1 arrows (`DIK_LEFT 0xcb`,
`DIK_RIGHT 0xcd`, `DIK_UP 0xc8`, `DIK_DOWN 0xd0`), `DIK_SPACE`, `]`, `[`, `C`, `R`; player 2 on the keypad.
Bindings are saved in `Controller.rcf`.

**Port:** gasm raw keyboard mapped onto `DIK_*` codes, pads from `input_pad(0..3)`.

## Network

`CMR2.exe` already contains the DirectPlay 4 client (`Net_CreateDirectPlay` 0x4aaaf0 with `CLSID_DirectPlay`,
`IID_IDirectPlay4A`; lobby `CLSID_DirectPlayLobby`; 0x4a0000-0x4a2000, "gnNetworkFrame" in 0x425a90).
`CMR2Network.exe` is a separate build of the same program. Network play is the last step of the port
(gasm's `gasm:net` relay); single player, split screen and alternate (hot-seat) play come first.

## Cheats and debug

The original's cheat codes are a table of 20 strings at 0x5250fc (`evilevo`, `minime`, `showmethecossy`,
`wheelybig`, `allthebuttons`, `turnontheice`, ...), matched by 0x4eb290 against an entered name; the index
selects the effect (0x4eac50). They are part of the game and are ported, read from the exe like every
other table. The `.rsrc` has debug dialogs (texture
adjustment, replay portions, "CAR INVISIBLE", "TRACKSIDE CAMERAS", "60 FPS"), not reachable in the release
build as far as known.
