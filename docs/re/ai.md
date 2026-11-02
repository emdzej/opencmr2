# The computer drivers

Internal notes (0x47bda0-0x47d330, 0x498590-0x498d2c; ported in `src/game/ai.c`). Confirmed from the
disassembly unless marked.

## Data

`Ai_LoadLines` 0x47c2f0 (in `Stage_Load` after the drivers' times, only with selection bit 24) finds
`<stage>.ai0`, `.ai1`, `.ai2` in the stage archive and copies the difficulty's (0x405d90) into the
pool at 0x541f8c. Layout (little-endian):

| Offset | Contents |
|---|---|
| 0 | six ints: each car's set, 0 for none (the player's slot is 0) |
| 24 | the set count (low byte), then the sets, 0x88 bytes: +0 the state mask, +4 up to 16 entries of 8 bytes (+0 a section id, +2 the map, +3 the racing line), +0x84 the entry count |
| then | the map count, maps of 0x14 bytes: five (output node, net) byte pairs for +0x1d0, +0x1d1, the throttle, the brake, the handbrake |
| then | the line count, 16-byte records (names: `bitrcnc1`) |
| then | the net count, nets of 0x20 bytes: +1.. the inputs' state codes, +8 the layer count, +0xc.. the layer sizes, +0x14.. the weight pointers (filled by the loader) |
| then | the weights, per net per layer (inputs + 1) x outputs ints, the bias first |
| then | 64 ints: the squashing curve (an odd tanh-like table, 0 to 0.9992 in sixteenths of the sum) |
| then | 0x10 bytes per centre-line point (370): +0 a byte (0x47d0e0's result), +4 the section id, +8-+0xe the verges' margins in whole units (signed) |
| then | per line, four arrays of 370 ints: heading, speed, x, z; `0x498590` adds point 0 to x and z up to the finish point |

The ita-ch files have four (ai0) or three sets, one map, and two nets: 4-4-2 (speed, slide, the
heading against the line's, against the line's i + 2) for the steering pair, and 1-2 (the speed less
the line's) for the throttle and brake.

## Per frame

`Car_RaceControls` 0x47b000 for a car without a pad (0x53acc8 -1) calls 0x47b620, which with
selection bit 26 calls 0x47bdd0. It reads the car's set (0 returns), its centre-line point, the
set's entry for that point's section (line, map; 0x47cbc0), ORs the set's mask with 0xfffffffc (in
the data), and fills the state (0x498620, docs in the source). The verge distance (0x58e230, read by
`Sim_Collide` to skip the walls for an airborne car) is the smaller of the point's margins less the
offset.

Then, in order: the recovery rules (0x47c5e0: near a verge or turned across, a fixed reply 1-6;
none in the arcade's location 2 stage 1 at points 0xde-0xe3), the pass (0x47cd10: a car under
0xc800 up to 5 points ahead, 10 when it faces the other way, within 45 degrees and a clear width),
option 3's side check (0x47d0e0; its car count byte 0x58e0b1 is 1, so only car 0 is looked at),
option 0's aim at car 0 (0x47cc50). With a recovery reply the nets are not run; otherwise the map's
nets (0x47c9a0, 0x498ca0, 0x47ca30) give the five outputs, the pass and the side override the steering,
and three rules clear throttle or steering. Each output above zero presses its control (0x3f, the
handbrake 1).

`0x47ca30`'s last-layer test compares one past the layer index with the count less one, so it never
takes the plain sum: every layer goes through the curve.

## The start

`0x47bda0` (Race_S5_Line's 0x424c50 via 0x48ca70, bit 26) sets the bias inputs, the start revs, and
the car counts. `0x47c1e0` seeds rand() from the clock (0 in the demo) and draws one number per
racing car: at or below 0x4ccc the CPU car's revs +0x7a4 are its maximum +0x794 times 1.0 less
(a difficulty figure mod 10)/65536, otherwise 0. At the go (0x47bdc0 from the countdown and the
restart) the CPU cars are launched (0x43f570). On the line 0x47b640 blips their throttle with
rand() (+0xb47 counts 5-14 steps on, as many off).
