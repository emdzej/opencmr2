# The network game

Internal notes (not on the site). The original plays over DirectPlay 4; the port emulates the subset it
uses on gasm:net and gasm-relay (`src/net/dplay.c`, `session.c`), and ports the game's layers on top.

## Race messages

`src/net/race_msg.c`, 0x427620-0x427aa0. Each message is a type byte, then its fields at the offsets
of the original's stack structures (the type byte comes from a byte store the decompiler drops; the
disassembly gives it). Taken each step by `Net_Messages` 0x427890 (from Sim_Step), dispatched by 0x4276c0.

| Type | Sender | Fields | Taken by |
|---|---|---|---|
| 0 | front end | chat text from +1 | 0x4d0620 |
| 6 | 0x40ac40 | +1 the state (2 on the line, 3 finished) | 0x40ac70 |
| 7 | 0x427930 | ready | 0x409e90 |
| 8 | 0x427990 | +1 the split, +4 its time (not guaranteed) | 0x409fd0 |
| 9 | 0x4279d0 | +1 the lap, +4 its time | 0x40a0e0 |
| 10 | 0x427950 | +4 the stage time, +8 the rally total with it | 0x409f80, 0x409f00 |
| 0xb | 0x4278f0 | +2 the 30-byte car state (below), 32 bytes, not guaranteed; taken while 0x539cc8 | 0x409d50 |
| 0xc, 0x10 | 0x427a10 | the results' fade (0x10 also the time 0x537f34) | Results_NetDone 0x449090 |
| 0xd | 0x427a30 | continue | Results_ContinueDone 0x449fe0 |
| 0xe | 0x427a50 | restart | Pause_RestartDone 0x401540 |
| 0xf | 0x427a80 | service | Pause_ServiceDone 0x4014f0 |
| 0x11 | host | +1 which board, +4 the board | 0x40afb0, 0x40ad20 |

System messages (from DPID_SYSMSG): 5 a player gone (out of the players and the records), 0x101 this
game made the host (the session, the pause's pages 0x402c90, the results' 0x44a1b0).

## Cars

`src/net/carsync.c`, 0x424dc0-0x427580 (confirmed against the disassembly where the decompiler merges
products: 0x426810's place and velocity steps, 0x424f20's clamps).

### The state (0x539388, 30 bytes)

Packed by `Net_PackCar` 0x424f20 for each local car (+0xc1c clear) at the end of each step
(`Net_SendCars` 0x4258e0, when 0x539cc8 is set). Quantising: a 16.16 value times a factor, then 1 or
more gives the top code, -1 or less the bottom one, else the nearest of 32,765ths (s16: 0x7ffd, 0x8003)
or 127ths (s8: 0x7f, 0x81). The constants are the floats at 0x511310-0x511354, read from the exe.

| Offset | Field |
|---|---|
| +0x00 u16 | the race's step (0x41d2a0, 25 a second) |
| +0x02, +0x04 s16 | x and z of +0x2dc - +0x2e8, times 0.2 |
| +0x06 u16 | the product of the wheels' averages (x of +0x1a4/+0x88, z of +0x1a8/+0x8c, four wheels, a quarter), in 65,530ths (0xfffa at 1) |
| +0x08, +0x0a s16 | x and z of the place (+0x30) from the cell's centre (0x71f608[+0xb00]), over 128 |
| +0x0c u16 | the cell (+0xb00) |
| +0x0e u8 | the revs (+0x960) over 10, in 255ths (0 at or below 0, 0xff at 1) |
| +0x0f s8 | +0x960 - +0x964, times 0.2 |
| +0x10, +0x12 | rows 0 and 2: low byte the turn about y (the arctangent table on |z| / |x|, quadrant by the signs) in 255ths of 360, high byte the angle from y (the arcsine table on |y|, flipped past 90 when y is not above 0) in 255ths of 180 |
| +0x14-+0x16 s8 | the spin +0x420-+0x428, times 5 |
| +0x17 | bit 0 upright (+0xb35), bit 1 braking (+0xb54), bits 2-7 the segment share (0x4209d0) in 64ths |
| +0x18 | bits 0-9 the sector point (0x4582f0, 0..0x400) |
| +0x1a | bits 0-6 the steering ((+0xb10 / +0xb16 + 1) times 63, plus 0x1999, at most 0x7e8000, the integer part), bit 7 +0x79c, bit 8 loose (+0xc00), bit 9 a snap (+0xb45 counted down), bit 10 paused (0x404f20), bits 11-14 the lap (0x4582d0, at most 15), bit 15 its sign |

+0x1c-+0x1d are never written. The receiving game keeps it at the record's +0x10 (0x409d50; newer by
the step only), the race places (0x40a580) read +0x16-+0x1a.

### The predictor (0x5393d8 + 0xec per car slot)

Started by `Net_CarStart` 0x424dc0 from Car_Place in the network modes (mode bit 19), the record's
state from the car itself with step 0. `Net_CarsReset` 0x424ed0 (state 5) clears the frame estimates
(0x5393ac, a dword per record) and fills 0x539b38 with i (i + 1) / 2 for i < 100.

Fields: +0x00 the frame shown (4 x 4), +0x40/+0x4c/+0x58 the rows, +0x64 place, +0x70 velocity, +0x7c
spin, +0x88 drag, +0x94 last change of velocity (unit), +0xa0 the place then, +0xac drag speed and
+0xb0 its inverse, +0xb4 revs shown, +0xb8 throttle, +0xbc revs, +0xc0 their change, +0xc4 steering
(u16), +0xc6 snap count down (u16), +0xc8 frame predicted to (u16), +0xca state's frame (u16), +0xcc
upright (byte), +0xd0 braking, +0xd4 loose, +0xd8 snap, +0xdc change seen, +0xe0 state taken, +0xe4
paused, +0xe8 jump.

Each step, `Sim_SimpleCars` 0x425950 (always called; the places 0x40a580 from car 0 either way):

1. `Net_TakeCars` 0x425a90: per record with a new state, `Net_UnpackCar` 0x425c40 takes it when it is
   1 to 99 steps newer and its cell exists: the inverse scalings, rows 0 and 2 from the angles through
   the sine table (sin b cos a, cos b, sin b sin a; unit), row 1 their cross (unit), then `Net_Drag`.
   The steering's share (-1..1) times +0xb16 (in sine-table units) is +0xc4; +0xb8 times +0x788. The
   frame estimate goes halfway to the state's step, to it after a gap of 51 or more (+0xe8); a record
   without a new state moves its estimate on by one.
2. `Net_Predict` 0x426810 for each remote car (+0xc1c): dt = estimate - +0xc8 (0 after a jump), the
   bounce (0x426b90), place += v dt + drag tri(dt), v += drag dt, the rows turned by spin dt
   (Rows_Turn3 0x429f20), revs += c dt - dt^2 0.048, c -= 2 dt 0.048; the shown frame is the new one on
   a snap, else blended halfway (0x4224e0).
3. `Net_PoseCar` 0x426d80: the car's records from the predictor (its height kept from +0x2ec), the
   last steps' records moved on, Car_BoxCorners, +0xc20 set.

After the collisions `Net_CarsAfter` 0x426fc0 puts the collided car back into the predictor: place and
rows, a snap (Pose_Snap), velocity plus 0.2 of the impulse +0x5c4 times the step, spin less the
damping, the drag again, the loose flag cleared when the car is upright and still; and on a change of
velocity its direction (+0x94) and the place moved onto the line through the car's place along it.

`Net_SetRate` 0x427580 (from 0x424ed0 with 20,000, 1,000,000 and 2) sets 0x5393d4 to 40 or 41; nothing
the port has read uses it.
