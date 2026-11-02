# World

The scene: objects in trees, brought up to date and drawn each frame, placed in the stage's sections.
See [c3d.md](../formats/c3d.md#object-0x18c-bytes) for the record and [render.md](render.md) for the
drawing below it.

## Objects

A model's object records are loaded with the model. `C3d_Fixup` registers them in the object table at
0x67f36c and writes each one's slot number into it. With a parent object given, the model's roots
(`+0x24` = -1) are appended under that parent (`Obj_Attach` 0x4ac200): as its first child, or after the
last sibling of its first child. A root keeps its own `next` link, so a model with several linked roots
would bring its siblings along twice. Every object's `+0x24` is then -1.

The four matrices:

| Offset | Matrix |
|---|---|
| `+0x58` | rest pose |
| `+0x98` | current local (object to parent) |
| `+0xd8` | world (16.16) |
| `+0x118` | world in floats, set as the world transform when drawing |

`+0x174` is a dirty flag; `+0x178` is the kind (0 mesh, 1 light, 2, 3 node).

`Obj_UpdateTree` 0x4ac280 walks a sibling list and its children. It skips inactive objects (0x4ac390;
objects in a section ask the section) along with what hangs under them. An object with a parent and a
kind of 0-3 is recomputed when it or its parent is dirty:
- under a parent of type `0xfd` (byte `+0x30`), world = local;
- with `+0x184` set, world = the parent's world;
- otherwise world = local x parent's world (`Mat_Mul` 0x4b9f20).

A placed object (`+0x24` not -1) then finds its section again (`Obj_UpdateSection` 0x4b8690), and the
object and all its ancestors are marked dirty. A root (no parent) is never recomputed: whoever owns it
sets its world matrix.

`Obj_Commit` 0x4ad8d0 then handles each dirty object by kind:
- kinds 0 and 3: the float matrix is made;
- kind 1: `+0x0c` points to a `D3DLIGHT7` (index at `+0x68`). A point or spot light gets the world
  position, a spot also the world x axis as its direction, and is set; a directional light is left as
  it is.

The flag is then cleared.

`Obj_Draw` 0x49cad0 draws one object for a pass (0-7, a bit in `+0x17c`), when its kind is 0
(`+0x178`) and it has a mesh, then its children through `Obj_DrawList` 0x49ca50. `Obj_DrawList` only
descends under kind-0 objects, so a car is drawn by calling `Obj_Draw` on its kind-3 root.

`Obj_Frame` 0x49ce10 runs the update, the commit and `View_Set` 0x4ade00 for a camera object.
`View_Set` makes the view the 16.16 inverse of the camera's world (`Mat_Inverse` 0x4ba440: cofactors
over `2^32 / det`), kept at `PTR_DAT_00520b78 + 0x2424`, when the camera is dirty and a counter at
0x6838d0 is below 5.

## Sections

The stage mesh's 0x88-byte section records (registered at 0x71f608, count at 0x72d568) are cells of a
square grid, each holding one mesh. France stage 2 has 360 cells of 64 x 64 units in rows of 18. Its
148 meshes each fill one cell, tracing the route. On disc only the centre (`+0x00` x, `+0x04` y,
`+0x08` z) and the mesh (`+0x10`) are set; the rest is filled at run time.

`Section_Init` 0x4b8270:
- the cell size is the x distance between the first two cells, with its half and its reciprocal
  `2^32 / size`;
- the bounds are the least x and the greatest z;
- the row length is the run of cells from the first that share a z;
- it hangs the second-kind meshes on their cells (`+0x14`, count `+0x18`) and puts the world root's
  objects into the cells of their local translation (`Section_LinkObjects` 0x4b8450; type byte `0xff`
  stays out);
- it links some 0x30-byte records into the cells (`+0x28`, count `+0x24`);
- it works out per-edge heights from the nearest mesh vertex (0x4b8b90) and each cell's corners
  (`Section_Corners` 0x4b9170).

The corners are the mesh's bounding rectangle in whole units (each vertex rounded as it is compared),
or the cell's square without a mesh.

`Section_Find` 0x4b85f0 gives a position's cell from its distances to the top-left corner times the
reciprocal. The distances are taken as absolute values, so a position outside the grid folds back in,
and an index out of range gives 0.

`Section_BuildVisible` 0x4b7de0 lists the visible cells each frame:
- it takes the camera's cell, and a window of cells as wide as the far plane (`Proj_Far`) reaches;
- it builds a view wedge from the camera position and two points at the far distance, 45 degrees
  either side of the forward direction (normalised with the table square root);
- a cell in the window is listed when a corner falls in the wedge (`Point_InTriangle` 0x49da50, on
  coordinates scaled by 0.01) or when it is one of the nine around the camera's cell;
- it then records the squared distance (`+0x7c`), sets `+0x80` / `+0x84` to 0 / 1 and sets a bit in
  the mask at 0x72d258, which `Section_Visible` 0x4b7da0 reads;
- it also works out a point 10 units behind the camera and never uses it.

The draws that walk the list:
- `Stage_DrawTerrain` 0x49cec0 draws each listed cell's mesh with the identity world matrix (batched,
  or polygon by polygon for flag 0x1000), after a time-step call (0x4bcc60, not ported);
- 0x49cf80 draws a second per-cell table with z-writes off;
- 0x49d040 draws the cells' second-kind meshes as billboards turned to the camera;
- 0x49ce40 draws the cells' objects.

Each cell keeps its objects in a list (head `+0x1c`, count `+0x20`, linked through the objects'
`+0x170`). `Obj_UpdateSection` 0x4b8690 moves an object between cells as its world position
(`+0x108`) changes, and records up to three neighbouring cells within 4.5 units of an edge at `+0x28`.

## Scenery

`TEMP.OBJ` and `TEMP.SHT` hold no objects, only second-kind meshes, each with a 0xa0-byte record.
France stage 2 has 1,595 and 236 of them.

The record:
- `+0x00` the position;
- `+0x0c` the mesh;
- `+0x10` flags;
- `+0x14` an enable byte;
- `+0x18` a 16.16 placement matrix (rotation and position);
- `+0x58` a float matrix made at run time (zero on disc);
- `+0x98` the next record.

`C3d_Fixup` registers the records at 0x669364 (count 0x67f228). `Section_Init` puts each one in front
of its cell's list (`+0x14`, count `+0x18`). Meshes without flag 2 have world-space vertices (the
walls of trees, hedges, rocks, the `.sht` sheets) and are drawn as they are. Meshes with flag 2 (1,572
in `TEMP.OBJ`: the trees) are billboards.

For a billboard, `Section_DrawBillboards` 0x49d040:
- copies the frame's camera-facing turn (made in `World_Render` from the camera's forward x and z: a
  turn about y) into `+0x58`;
- scales it by the placement's diagonal and moves it to the placement's position;
- writes its distance from the camera into the mesh record (`+0x114`), so records sharing a mesh share
  the last distance;
- queues it.

`Billboard_DrawQueued` 0x49cc50 sorts the queue far to near (qsort; the order of equal distances
is the runtime's). It resets each billboard's first texture to blend type 0, marks it blended (flag 8)
within 200 units, and draws it with its matrix.

## Sky and ground

`Stage_LoadSkyGround` 0x46f060 loads four models under the world root, each made always active
(`+0x180` = 0) with its mesh's `+0x2c` set to 0:
- `TEMP.SKY`, the backdrop panorama (up to four of its objects);
- the clouds: the larger of the stage's two conditions through the byte table at 0x51c688 picks
  `Cloud_Light`, `Cloud_Med` or `Cloud_Heavy` (0, 1, 2). It mounts
  `<dir>\Clouds\Pc\<name>.bfl` (`PcLow` with a detail option) and loads `<name>top.c3d` (its pass mask
  set to 0) and `<name>.c3d`;
- `TEMP.GRO`, the ground.

`Sky_Follow` 0x46f330 moves them over the view's camera (or car) each frame, through `Obj_SetPosition`
0x4ac480:
- the sky, ground and clouds to 15 below it, plus height offsets from the stage (0x4928c0), the clouds
  4 lower again in one stage;
- the cloud top 20 below the clouds.

They sit in cells like other world objects, so the update moves them into the camera's cell
(`Obj_UpdateSection`), and `Section_DrawObjects` draws them with the z-buffer off, before the terrain.
Last, `Sky_Follow` sets the dome's matrix row 1 to (0, `Sky_Scale`, 0); the scale is 1.0 from
`Hor_Apply`.

## The sky

The sky's colours are not in the models: they come from the horizon records ([Time of
day](#time-of-day)) and are written into the meshes' vertices (colour at +0x18, specular at +0x1c,
whose top byte is the fog factor) and re-sent to the device. `TEMP.SKY` is the dome with a child A (the
sun's disc); A's next sibling B is a sheet lit by lightning and B's next C the sun's halo.

**Set-up** (`Sky_Setup` 0x4919a0, first in `Hor_Apply`): the objects and meshes, every dome vertex's
height as loaded, the lowest and 1 / (highest - lowest); the sun is the dome's vertex 104 (kept at
0x592114) and the ground's vertex 50 the point under it (0x592108); B's colours cleared.

**The cut** (`Sky_Cut` 0x4925c0): three 16.16 offsets for the sky, ground and clouds (the record's
first three i32; kept for `Sky_Offsets` 0x4928c0). Every dome vertex but the sun that lies below the
horizon height (the ground's offset less the sky's) is raised to it. The sun light (device light slot
0) is placed at the sun's point (`Dlight_SetPosition` 0x4b35b0: position, and the unit direction toward
the origin).

**The horizon state** (`Hor_Apply` 0x460da0, at the end of `Stage_Load`): two blocks of 0xb4 bytes,
the start (0x547950) and the end (0x547a04), each from its record (byte channels as 16.16):

| Offset | Field | Record |
|---|---|---|
| +0x00 | horizon colour | +0x20-0x22 |
| +0x0c | zenith less horizon | +0x1c-0x1e minus +0x20-0x22 |
| +0x18 | sun colour | +0x24-0x26 |
| +0x24 | sun glow colour | +0x3c-0x3e |
| +0x30 | ground colour | +0x28-0x2a |
| +0x3c | cloud colour | +0x2c-0x2e |
| +0x48 | low light | +0x30-0x32 |
| +0x54 | high light | +0x38-0x3a |
| +0x60 | sun disc colour | +0x34-0x36 |
| +0x6c | sky, ground, cloud offsets | i32 +0x00, +0x04, +0x08 |
| +0x78 | fog colour | +0x44-0x46 |
| +0x84 | halo colour and alpha | +0x40-0x43 |
| +0x94, +0x98 | fog near, far | i32 +0x14, +0x18 |
| +0x9c | the sun's mix into the dome | +0x27 / 255 |
| +0xa0 | the sun's share under it | +0x2b / 255 |
| +0xa4 | cloud alpha | +0x2f |
| +0xa8 | disc alpha | +0x37 |
| +0xac | the glow's distance | i32 +0x10 |
| +0xb0 | clearness | 0x51b0b8 by that end's weather condition (1.0 for 0 and 1, 0.1 for 2 and 3) |

Without both records the two blocks take one built-in set (a pale blue sky, grey ground, offsets
(-5, -38, -13), no fog). Then the cut at the start's offsets, the scale 1.0, the blend 0x547ab8 = -1.0
(so the first frame sets the sky), the flash 0x547ac0 = -1 and the fog on (`Sky_FogOn` 0x492fd0,
flag 0x547ac4) if either end has a fog range.

**Per view** (`Weather_Sky` 0x461c30, in the world frame before `Sky_Follow`): the sky is set again
when the view's blend along the stage (the weather's per-car record +0x1c, capped at 1.0) differs from
the last one and the stage's sky changes at all (0x543d88 != 0x543d8c), when the flash changed, or
when the record is marked (+0x28). Every field is blended `start + (end - start) t`; with w the
clearness:
- the low light L is the record's times 0.8 + 0.2 w;
- the ground is L + (ground - L) w and the high light L + (high - L) w;
- under the sun: L + (G + (sun - G) m - L) w, with G the ground and m the share under it times the sun's
  mix;
- the dome by height from horizon to zenith, the sun's vertex toward the sun colour by the mix
  (`Sky_ColourDome` 0x491d40); the ground (0x4920d0), clouds and cloud top (0x492220), disc (0x492470)
  and halo (0x492520) one colour each but the special vertex;
- the sun glow's colour (0x543eb4) and two sizes from the distance (x 0.0016 and x 0.0021, at
  0x543d58, 0x543d5c);
- the fog (`Sky_Fog` 0x492fe0) when on: `Fog_Set`, and the sky's fog factor from where the draw
  distance ((option + 2) x 50) falls between near and far: 255 before near, 0 past far;
- the light colours (`Light_SetLowIfChanged` 0x462cb0, which on the super special also lights the
  fireworks, and `Light_SetHighFor` 0x492e30), the cut with the sky's offset blended (the ground's and
  clouds' stay the start's), and `Sun_Direction` 0x463070.

A flash (lightning on, the view's rain over 0.8) adds 20 to the ground, makes the sun, the point under
it and the high light white, the clouds' bolt vertex white with its alpha up 50 (at most 250), B's
alpha 255 (else 0) and the sun's mix 0.3. Every frame then: the clouds turned to the record's heading
(`Sky_Turn` 0x4926f0; the dome not turned), A and C turned to face the camera (`Sky_FaceCamera`
0x492bd0, its rows by 0.046) and the changed meshes uploaded (0x492f10).

**The sun glow** (`Weather_SunGlow` 0x462d80, in the weather pass, one player or a shared replay):
the sun's point (plus the dome's position) through the camera (0x4bad40: camera space, nothing nearer
than 1, then the projection 0x4ba870 and the screen's middle). A 16 x 16 rectangle around it is
tested against the disc's colour (0x462d10 and 0x4bc490, below); 0x543ea0 keeps the share hidden (100
less the matching share). The fade f is 2 - 2w (clamped to 0-1) plus the hidden share eased toward
by at most 0.3 a frame (the weather record +0x68), capped at 1; the glow's colour is the sky's glow
colour times min(1, 3 (1 - f)); its size is the screen's width and height times the two factors from
the glow distance (0x543d58, 0x543d5c). It goes to the sprite layer 2 with the `sunglow` texture.

**The colour test** (0x4bc490): a call with a rectangle keeps it, the colour's bytes 0-2 and the
tolerance (40 here) and returns the last share; `World_Render` calls it without one after the 3D
world, and that measures: the rectangle clipped to the screen, the back buffer blitted to an
offscreen surface (made by 0x4bca70) and locked, and the pixels counted whose red, green and blue are
within the tolerance of bytes 0, 1 and 2. The colour is the disc's first polygon's first corner colour
as loaded (0x492bb0), ARGB, so byte 0 is its blue: red is tested against blue (white on the disc
here). The share is of the unclipped area, so the part off the screen counts as hidden. The port reads
the frame with `glReadPixels`.

**The sun's direction** (`Sun_Direction` 0x463070, also at the race start): the sun's point
(`Sky_SunPoint` 0x492890, raised by the sky's offset) divided by its largest axis and made unit, kept at
0x5477f8 for the shadows' outlines (0x498370). At the race start `Shadow_SetSortedDir` 0x4b5340 also
keeps it negated at 0x6dfdd8 for `Light_SunObjects` 0x4b50b0. That runs on every cell's objects in
`Light_RelightAll` 0x4b3f20 (each with the siblings after it), which at the race start comes before
0x4b5340, so the first relight uses the previous direction (zero in the first race). For each sorted
mesh object with mesh flag 0x800 it writes the mesh's per-vertex extras: each polygon corner's normal
on the direction (into the object's frame) times 4, from 0 to 1, at most the light of the ground under
the object (the extra of the nearest stage vertex within sqrt(32000) in x and z, 0x4b3860). Nothing in
the exe turns these extras into colours: the other caller, the movable scenery's 0x4b5320, follows it
with 0x4a3240, an empty function.

**Lightning** (0x45f890): the bolt is the clouds' vertex nearest the sun within 100 (`Sky_BoltVertex`
0x492910). `Sky_BoltTexture` 0x492b50 would put one of three bolt textures on B, but none is ever
loaded: only its `rand()` matters.

## The world frame

`World_Render` 0x49d3f0 (camera, viewport x/y/w/h, pass, a world flag) draws one view, in this order:
1. Zero the frame's polygon counts, begin the scene and set the viewport. In lighting mode 5 with
   culling off, draw 2D layer 4 (behind everything).
2. With the world flag set:
   - list the visible cells, once `Section_Init` has run (0x72d56c), and run a per-cell vertex
     relighting for each (0x4b3c00), which redoes a cell's colours when the light colours change;
   - call 0x4b0480 (camera, pass) and make the billboard turn.
3. Without stage sections, draw every object (0x49cd90) and the 0x10 triangle bucket. Otherwise:
   1. Draw the cell objects (`Section_DrawObjects`, z-buffer off: the sky and ground). This is done
      with the fog-enable state of 0x816704, fog off, mode 5 and the far plane at 150.
   2. Draw the terrain in mode 0 with linear fog.
   3. Draw the tree shadows (`Stage_DrawOverlay` 0x49cf80, [stage.md](../formats/stage.md#tre)) in
      mode 5.
   4. Draw the glows (`Glow_Draw` 0x4af120, [below](#glows)), then the triangle buckets 8 (mode 5) and
      0x20 (mode 4).
   5. In mode 1, draw the scenery. Then come the sorted objects (`Section_QueueSorted` 0x49d290,
      `Obj_DrawQueued` 0x49cd20) and the queued billboards. With the mode at 0x59ce14 set
      (`World_SetMode` 0x49c420), the billboards come first and the 0x10 bucket and the indexed draw
      are drawn before the sorted objects.
4. Still with the world flag, but fog off and source-alpha blending in mode 5: draw the 0x10 bucket,
   the 0x40 bucket (mode 4), the debug 3D lines, the sprites (`Render_DrawIndexed` 0x4b1530,
   [render.md](render.md#sprites)) and 0x4b6240 (pass).
5. Call 0x4bc490 (0, 0, 0). Then, with z off, draw 2D layers 3, 2 and 1 over the whole screen with
   culling off.
6. Run the frame statistics (0x4b21e0) and end the scene.

The sorted pass takes the objects in the listed cells that have +0x180 set (`Section_DrawObjects`
leaves them out). It writes each one's distance to the camera in x and z (the table square root) to
+0x16c, and draws them far to near. `Obj_CompareFar` 0x49cbc0 always answers "after" for an object of
type 0x14, whichever side it is on. Within one unit, type 5 goes after type 0. The sort only runs when
0x5207b4 is set, as for the billboards.

The 2D line queue (0x4bb2b0) is a debugging aid nothing fills in the release. The 3D one (0x72d680,
200 lines; 0x4bb430 adds, 0x4bb4c0 draws each as a two-vertex line list of model vertices, untextured,
source-alpha blended, before the sprites) has one user: the cars' whip aerials (0x485860, see
`src/sim/aerials.c`).

## Time of day

The stage selection word (0x52f2ac) holds the rally in bits 0-4, in the order of the exe's prefix table
at 0x51931c (FIN, GRE, FRA, SWE, AUS, KEN, ITA, UK, JAP), and the 0-based stage in bits 5-9. The stage
id is `<prefix><stage + 1>`. Two modes (bits 24 and 26) take the time and weather from other tables
instead.

- **Start and end time** (0x407650, 0x407710): the two bytes at 0x516188 + (stage + rally x 12) x 2,
  times 100 (fra02: 12:00 and 13:00). Rally 8, stage 12 in a rally is always 20:00.
- **Weather** (0x407520): two conditions per stage index at 0x52f100, start and end. The exe holds
  zeros, and the game fills them before a rally.
- **Horizon records** (`Stage_LoadHorizons` 0x41fd30): 48 files of 80 bytes in the stage archive. The
  names are the strings at 0x5196dc going down in steps of 12. There are 12 times (0x51b034: 5:00 to
  24:00) in four sets: plain, `CLO`, `STO`, `BLI`.
- **The record for a time** (0x461830):
  - find the first listed time not before it; a time before 5:00 blends from 24:00, and minutes of 60
    or more give no record;
  - the weather condition picks two sets (0x51b0dc) and their mix (0x51b0f0);
  - blend each set's records at the times before and after (`Hor_Blend` 0x461710), then blend the two
    results by the minutes elapsed.
- **The blend:** the seven i32 at +0x00 go linearly in 16.16. The bytes +0x1c-+0x47 and +0x48-+0x4d
  use `b + (a - b) t`, keep the whole part and cap it at 255.
- **What the race uses:** `Hor_Apply` 0x460da0 builds the sky's start and end state from the two
  records ([The sky](#the-sky)). The race start (0x45eca0) sets the start's low (+0x30-+0x32) and high
  (+0x38-+0x3a) light colours (without records: 200 and 255); fra02 at 12:00: low (106, 131, 164),
  high (255, 255, 245). Each view then blends toward the end by its progress (0x461c30).
- **The rest of `Stage_LoadHorizons`:** with both records, the averages of their bytes +0x48-+0x4d go
  to 0x4a5ff0-0x4a60b0, a level, a scale and an offset for each of `Tga_Pixel` 0x4a60d0's flags 0x80
  and 0x100. No caller passes those flags, so the values are never read. A start at 21:00 or later
  sets bit 20 of the selection word (0x407800), for which no reader was found.

## Weather

The race's weather (0x45e5b0-0x462d80, `src/game/weather.c`). A stage's start and end conditions
(0x52f100) give its kind (0x51b04c: 0 dry, 1 rain, 2 snow), its level (0x51b070) and its wet value
(0x51b094). Rain and snow are per player. The wet value is per car and feeds the grip and the lamps'
shine on the ground.

**Load** (`Stage_Load`, 0x45ea70). The kind is the start's. When only the end has one, the start's
level is 0; when only the start has one, the end's level is 0. A change of level runs from a fifth of
the way to the finish to the finish less a twenty-fifth of it. (The original divides the fifth by five
again for the end.) The computer drivers' times read the same change (0x45e6b0).

**Race start** (0x45eca0, in the cars' placing):

- **The ramps** (0x45f260). Each is a value between two centre-line points: from, to, slope, first
  point, last point. There are three:
  - the sky's blend (0x543d88), 0 to 1 over the whole stage when the time or the weather changes
    (not on circuits);
  - the level (0x543d60);
  - the wet value (0x543d74), which also sets every car's grip.
- **The detail's share** (0x543d50): 1.0, 0.25 or 0 by the effects detail.
- **The vertex and sprite templates** for the streak, the splat, the flake and the drop on the car.
- **The wind** starts along (0.707, 0, -0.707), still.
- **The drops.** 400 are shared between the players (0x543fb0, 0x24 bytes each). They are scattered
  through a box 24 x 12 x 24 one unit ahead of each camera.
- **The light.** The low and high light come from the start's horizon. The sun's direction for the
  shadows (0x463070, 0x4b5760) is not ported.

**Each step** (0x45f4a0, at the end of the lamps' frame 0x4664c0):

- **The wind** (0x45f6d0). Gusts hold for 25 to 100 frames. Then the strength eases to a new aim
  (0 to 1, at 0.005 to 0.03 a frame). With snow the aim and rate are halved.
- **The lightning** (0x45f890). A wait of 125 to 500 frames, then a bolt on a sky vertex for 20 to 40
  frames. In rain above 0.8 the flash shows 30 percent of the frames. The sky's bolt vertex and colours
  (0x492910, 0x492b50) are not ported.
- **Per player:**
  - the level by the point its car is at;
  - the sky's blend;
  - the clouds' heading, turned by 6 percent of the wind across the view;
  - the drops (0x45f9d0);
  - a change of type (0x460b60, never in the release: nothing sets the wanted type after the set-up).
- **The drops** (0x45f9d0):
  - The box is centred 14 ahead of the camera, further when zoomed. It is never lower than 6 above the
    camera or 6.75 above the wheels' ground.
  - The drops fall with the wind, move against the box's own motion, and wrap round it. Rain falls 1.2
    to 2.0 a frame, snow 0.1 to 0.3.
  - In rain a 5 x 5 grid of ground heights (cells of 4.8) brings a drop that went under the ground
    back at the top, marked for a splat.
  - Snow sways along the camera's side and drifts with the wind.
  - Under the roofs of rally 6 stage 1 (points 103-108) and rally 7 stage 3 (470-471) the drops fade
    out, 0.2 a frame (0x45e9a0).
- **The cars' wet values** (`Race_Step` 0x45e7f0, after the computer drivers): each car's value by
  its point.

**Drawing** (0x460330, at the end of the cars' pass):

- **Rain.** A streak per drop: a triangle from its tail to a head across the view, 0.06 to 0.1 wide.
  The head's alpha is 30 in the open. A splat sprite goes where a drop came through the ground.
- **Snow.** A flake per drop, coloured by the terrain's light ramp at 0.8.
- **Not ported:** the drops on the car (0x460ca0) and the sun's glow (0x462d80, which reads the z
  buffer back).

**The horizon records** (0x461a70). A cloudy end gets +0x2f 200 next to clear or cloudy, else 50.
The fog range is 15 to 80 on a stage with heavy rain (0x52f1f0). Otherwise it is none near, and far
at (draw distance + 2) x 50 over the condition's fog factor (0x51b114). A record without fog takes
the other's far value.

## Stage lights

There are two colours:
- the low colour (`Light_SetLow` 0x4b3740), which is also the device ambient;
- the high colour (`Light_SetHigh` 0x4b3f90).

They make two ramps of 50 entries (0x4b3940), built when a colour has changed:
- **The terrain ramp:** the low colour plus the difference times i x 0x539. Each channel's whole part
  is clamped to 0-255 and the entry is opaque. The last step is just short of the high colour.
- **The shadow ramp:** the shadow colour, with an alpha of the shadow alpha times the step.

The shadow colour and alpha come from 0x4b4910:
- **Alpha:** the summed channel differences times 0x55, capped at 1.0, then times 255.
- **Colour, outside rally 3 (Sweden):** the alpha is doubled (capped at 255) and the colour is a sixth
  of the low colour.
- **Colour, in Sweden:** each low channel's excess over the smallest one.

A vertex's 16.16 intensity in its mesh's +0x34 array picks an entry (`* 49 >> 16`).

`Section_Relight` 0x4b3c00 runs for every listed cell each frame. A cell whose cached low and high
colours differ from the current ones gets the following, and its buffers are uploaded again (0x4b2020):
- the terrain mesh's vertices from the terrain ramp;
- each scenery mesh: one colour from the record's +0x9c (0x4b2090), or per vertex when mesh flag 0x80
  is set. A mesh shared by several records takes the last one's colour;
- the tree shadows from the shadow ramp. With 0x5210c0 set (the exe's value), each tree's vertices are
  scaled by its factor.

The race start moves every cell's cached red 10 away, so the first pass relights them all (0x4b3f20).
Before each cell is relit there, its objects get their sun light (`Light_SunObjects` 0x4b50b0, see [The
sky](#the-sky)). Not ported yet: the fade of shadows near a car's lights (0x4b6ca0).

## Device lights

The Direct3D lights are records at 0x6dffa4: up to 60 of them, each a `D3DLIGHT7` with the slot's
index at +0x68. `Dlight_Add` 0x4b2f80 takes the type, a 16.16 colour, a position, three angles and
a parent object. It fills the first free slot and sets the light and switches it on. The colour is
the diffuse; its alpha is 1/65536. The defaults are a direction of (0, 0, 1), a falloff of 1, an
attenuation0 of 1, a theta of 0.5 and a phi of 1. By type:

| Type | Direct3D | Fields |
|---|---|---|
| 0 | point | ambient = the colour, range 60, attenuation (0, 0, 0.1) |
| 1 | spot | ambient = the colour, range 100, theta 0.2, phi 2, attenuation (0, 0, 0) |
| 2 | directional | direction = -position made unit (the inline table square root) |
| 3 | 4 (parallel point) | nothing else |

With a parent, a new object is made under it (0x4ac6f0, set up by 0x4ac140: kind 3, every pass,
sorted, identity matrices). It becomes kind 1 with the record as its data (0x4ac260) and is placed at
the position, turned by the angles (0x4ac3d0). `Obj_Commit` then moves a point or spot light to the
object's world position each time the object is dirty. The object is returned; without a parent the
function returns 0. Freeing an object of kind 1 frees its record (0x4ac680, 0x4b3480). The count at
0x6e0b48 drops even when the record is not found, and a freed light stays switched on.

The device set-up (0x4a78a0) calls 0x4b2e50, which sets every light again and switches it on. It also
sets a white material (diffuse and ambient 1) and recomputes ambient B (0x6e020c) from the low colour.

The lights of a stage:
- **The sun** (0, `Light_Defaults` 0x4918d0, with the horizons). A white directional light from
  (50, 75, 75), under the world. `Light_SetMode` 2 and 3 switch it on for the cars.
- **The headlights** (1 and 2, `Lamps_Setup` for car 0). Two grey point lights of 20 under the world.
  `Light_SetMode` 0 and 1 switch on one each.

**Each view** (0x464960 in `Race_DrawView`, for the camera's car):
- **The daylight** (0x4648f0) is (R + G + B - 70) / 550 of the high colour, clamped to 0-1. The
  difference is taken as 16 bits.
- **The headlights' flag.** +0xb58 is set below full daylight.
- **The attenuation0** starts at the daylight times 76. When the headlights' lens level (group 4,
  `Lens_Get`) is 0, it is 1,000 instead, which leaves the lights dark. Then (1 - level) times 25 is
  added.
- The attenuation is set on both lights when it differs from the last value (0x547ff4).
  `Lamps_Setup` resets that value to -1.
- **Lights 1 and 2** stay on after a mode change (0x5210c0, 0x4b6ef0) while the lens is lit.
- **The two lights move** to 30 and 15 ahead of the car (0x547fe0, 0x547fa0). Each offset is turned
  by the main object's local matrix and added to its place.

## Glows

A pool of light glows (0x6a2a98, 0x5c bytes each), made with the race (0x4ae170: 160, or 4 with option
0 set) and emptied when the cars are placed (0x4ae260). `Glow_Add` 0x4ae2f0 takes the first free slot.
A glow has:

| Offset | Contents |
|---|---|
| +0x00 | kind (0 free; 2 seen from in front only, 3 from both sides) |
| +0x04, +0x10 | position and direction, in the parent's frame when +0x54 is set |
| +0x1c, +0x28 | a point and the normal of a plane under it (0x4ae420, with +0x44) |
| +0x34, +0x38 | the sprite's half width and height |
| +0x3c | level (0 not drawn; 0x4ae3f0) |
| +0x40 | the pool's size, times the width |
| +0x44 | the shaft's level (0: none) |
| +0x48, +0x4c | the sprite's and pool's texture, the shaft's texture |
| +0x50, +0x51 | on (0x4ae3d0), the pool's level (0: none) |
| +0x54 | parent object: the glow is skipped when it, or its parent, is not drawn in this pass (+0x17c) |

`Glow_Draw` 0x4af120 takes each glow that is on with a level above 0:
- **The angle** (kinds 2 and 3): from the camera to the glow, divided by its largest part and made
  unit, against the glow's direction, through the arcsine table as degrees. Past 90 degrees the
  factor is (a - 90) x 0x3d7 (full at about 156 degrees); kind 3 also takes (90 - a) x 0x3d7 from
  behind. At exactly 90 degrees, or a factor of 0, there is no sprite. Other kinds have the factor 1.
- **The sprite:** grey of factor x level x 254, opaque, its corners (-w, h, w, -h), moved one unit
  towards the camera and shrunk by 1 - 1/distance to keep its size, into the sprite queue with the
  glow's texture.
- **The shaft** (0x4ae470, with +0x44): an upright quad from the glow's foot on the plane, 2 x its
  height tall and 0.6 w wide along the camera's level right, moved towards the camera by its distance
  less 3 (at most 10) and shrunk to match. It fades out from 2 to 2.5 above the plane. Bucket 0x10,
  flags 6 (no z writes, no culling).
- **The pool** (0x4ae950, with +0x51): a square on the plane under the glow, size x w, its sides from
  the plane's normal crossed with x. It fades out from 0.4 to 1.0 above the plane and is moved a unit
  towards the camera. Bucket 8, flags 6.

## Start lights

The start gantry is the stage's `<stage>.bsp` (`Stage_LoadBsp` 0x420020): four rows of three words,
the gantry's x, y and z axes and its position (0x463290 into 0x547b40). The textures are Common.bfl's
`lgt_red` and `lgt_gre` (0x463360, blend 1).

When the cars are placed (0x463410, from 0x466360) and the gantry's position is not zero:
- **The layout:** stage 10 has 7 lights in layout 2 (rally 3) or 3; otherwise 8 lights in layout 1
  when the rally's bit for the stage is set in 0x51b6f8, else 7 in layout 0.
- **The frame:** the gantry's axes made unit, their lengths the scales. The glows face the frame's -z.
- **Each light:** x and y from 0x51b1a8 (per layout and light), z from 0x51b2a8 (per layout), times the
  scales, through the frame. Its colour (0x51b2b8: 0 red, 1 green) picks the texture and the size
  (0x51b6c8 per colour and layout, times the x scale). A kind 2 glow, off; a second one moved along
  the gantry's x by 0x51b6b8 (per layout) when that is not 0.

Each step (`Stage_Step` 0x466520, 0x463bd0) every light's level moves 0.2 towards its state in
0x51b338 (per layout, countdown step 0-6 and light), shown while above 0. The co-driver sets the step
(0x463ce0, above 7 becomes 6): 3, 2 and 1 with the countdown's numbers, 0 at the go. The restart
puts it back to 6 with every light off (0x463d00). In fra02 (layout 1) the reds come on through the
countdown and two greens at the go.

## Lamps

Each car's lamps are section 0 of its `.cin`: a count, then 0x28 bytes a lamp (confirmed from the
code and the files; the Puma has 13, the 205 11):

| Offset | Field |
|---|---|
| +0x00 | position, car frame (3 x 16.16) |
| +0x0c | direction (3 x 16.16) |
| +0x18 | sprite size (both half sizes) |
| +0x1c | brightness |
| +0x20 | kind: the level it follows (below) |
| +0x21 | texture: 0 brake, 1 reversing (also the headlights), 2 and 3 hazard, 4 head |
| +0x22 | the loose part it sits on, -1 none |
| +0x24, +0x26 | the damage slot and vertex nearest (u16, written at set-up) |

**Textures** (0x463d60, in the race set-up 0x40f020 before the start lights): the glows cleared, then
Common.bfl's `brkelite`, `revlite`, `hazdlite`, `headlite` twice over, into a sprite table (0x547fac)
and a shaft table (0x547ff8), five entries each by the lamp's texture byte.

**Set-up** (0x463fe0 per car, from 0x466360's loop over the listed cars when they are placed):
- Each lamp is a kind 2 glow on the second model, or on its loose part when dents are on and the part
  has an object (0x484de0). Both its textures get blend type 1. Kind 9 lamps get a pool of light (0xff)
  and are listed, two a car, in 0x543580 and 0x5431c0: they are the exhausts (0x45af90, 0x45d2d0).
- With dents on, each lamp notes the damage slot vertex nearest its position at rest (the squared
  distance, under 1000, the first of equals). Each frame it is moved by that vertex's dent.
- Car 0 also makes the headlights' two lights, 30 and 15 units ahead (0x4b2f80, see [Device lights](#device-lights)).

**The lens** (0x477b60): the second model's type 0xe object. Its +0x2c is zeroed and its first polygon's
texture, given blend type 2, is the lens. The interior model's lens is the same (the port does not load
the interior). Per car 0x58d6d0 + 0x48 n keeps five groups, each with a left and a right side:

| Group (bit) | Lit by | Lamp kinds |
|---|---|---|
| 0 (1) | the headlights (+0xb58), per side unless that side is broken | 6 left, 7 right |
| 1 (2) | braking (+0xb54) | 0, 1 |
| 2 (4) | the hazards: panel 23 (the engine) above 0.3 | 4, 5 |
| 3 (8) | reversing (+0xb5c) | 2, 3 |
| 4 (0x10) | the headlights, both sides | 8 (the right side's level) |

A side is broken when the damage record's +0x4a0 (left) or +0x4a4 (right) is set. Kinds 9 and 10 read
levels that are always 0. The hazards' clock (0x547ce0) advances by the frame scale. It turns them off
when it wraps past 20 and on as it passes 10.

**Each frame** (0x4664c0 in `Race_Update`, after the cameras, for every car in the race):
- **The lens** (0x477ce0). Each group and side fades towards its bit: up by 128 or down by 80, times the
  frame scale (0x51bd3c), within 0-255.
- **The side lamps.** For most cars two thirds of group 0 is added to the brakes (group 1). For handling
  classes 6, 7 and 10 group 0 itself drops to two thirds.
- **The lens texture.** When a level changed, the texture is restored from a per car copy (0x4a5080,
  made on the first keying), then keyed (0x4a4d30): pixels of alpha 0xbf, 0x40 and 0x80 take the levels
  of groups 1, 3 and 2; those of 0x60 and 0xe0 take groups 0 and 4. Each level is the brighter side's.
  The keys apply one after another to the same pixel. The 32-bit surfaces compare the alpha byte; the
  4444 ones its high nibble.
- **The glows** (0x4643f0). A lamp's glow is shown while its level is above 0, at level times the
  lamp's brightness. The colour it computes (red, white or amber by texture, times half the level) goes
  to 0x4ae410, which does nothing. The shaft's plane is the ground under wheel 0 with the car's ground
  normal (+0x48c). Its level is the surface's shine (0x4789d0: table 0x51e888 per surface, towards the
  wet surface's 0x51e8e8 by the weather value) on the wheel of the lamp's corner.
- A car off the stage (+0xb70) has its brakes and reversing off and every lamp hidden.

**The frame scale** (0x466630): the rate in frames a second, as 16.16, gives 0x51bd40 = rate / 25
(at least 0.6) and 0x51bd3c its inverse. `Cars_Load` sets 25. `Race_Update` (0x42b790) sets the
frame rate each frame, 1000 over the milliseconds since the last frame (0x4b21e0, World_Render's last
call; infinite on the first frame, which fistp turns into the smallest integer, so 0.6). The weather
code (0x45e9a0-0x462d80) uses the same scale.

## Particles

The effects (exhaust smoke, dust, snow, splashes, leaves, gravel, sparks, paint and glass chips) are
particles drawn as camera-facing sprites (0x4b11c0). The old Ghidra name of 0x4aff80, `Skids_Reset`, was
wrong: it kills every particle. The skid marks and car shadows are elsewhere ([Skid marks](#skid-marks), [Car shadows](#car-shadows)).

**Tables** (0x4b0060, from the race set-up 0x40efa0 with 32 types and 400 particles): types of 0x70
bytes at 0x6a2cd8 and particles of 0x68 bytes at 0x6a2cdc. Their counts are 0x6a2cd0 and 0x6a2cd4.
0x4b1140 sets the particles in use. `Cars_Load` and `Car_Recover` kill them all (0x4aff80).

A type is defined in three steps: 0x4af940 begins slot n, the setters 0x4afb20-0x4aff10 fill it, and
0x4aff60 marks it defined (+0x35 bit 0). The begin resets most fields: alpha 0x80 to 0, white, no
texture or callbacks, flags cleared. It keeps the scales (+0x20-+0x28), the spin (+0x2c) and the
extents (+0x3c-+0x48), so a slot defined twice keeps them. 0x4afb20 copies a defined type whole.

| Type | Field |
|---|---|
| +0x00 | life in steps (16.16) |
| +0x04 | the velocity's random spread (3 x 16.16) |
| +0x10, +0x14 | gravity a step; drag (towards the wind, times 0x290) |
| +0x18, +0x1c | a bounce's kept y velocity; its lost x and z |
| +0x20, +0x24, +0x28 | scale start, end, step (a random start: base and range) |
| +0x2c | spin a step (s16, 4,096 a turn) |
| +0x2e, +0x2f, +0x30 | alpha start, end, step |
| +0x31 | colour (RGB) |
| +0x35 | 1 defined, 2 dies below its ground, 4 dies at the fade's end, 8 grows, 0x10 random scale, 0x20 spins, 0x40 fades, 0x80 bounces |
| +0x36 | 1 spread along the velocity, 2 lit, 4 the animation loops, 8 random first frame |
| +0x38 | texture; +0x3c-+0x48 the sprite's four extents |
| +0x4c, +0x50, +0x54, +0x58 | animation frames, count, start age, age a frame |
| +0x5c, +0x60, +0x64, +0x68 | callbacks: draw, step (in place of the motion), after the step, after the emit |

| Particle | Field |
|---|---|
| +0x00 | type |
| +0x04 | velocity |
| +0x10, +0x1c, +0x28, +0x34 | previous, drawn, current and stepping positions |
| +0x40 | a record whose +0x30 vector is added when drawn (0: none) |
| +0x44, +0x48, +0x4c, +0x50 | life left, ground height, scale, turn |
| +0x52, +0x53, +0x56 | previous, drawn and current alpha |
| +0x54, +0x55, +0x57, +0x58 | shade, pass mask, alive, at rest |
| +0x59, +0x60 | colour, texture |

**Emitting** (0x4b07a0: type, position, velocity, ground, record, colour or NULL, shade, callback
argument, pass mask):
- **The slot.** The first free slot from the cursor 0x6a2ce4 on, where the cursor stays. If none is
  free, the oldest particle is reused (its type's life less its own, the first of equals from the
  cursor). With no particles in use the slot at the cursor is still written.
- **The spread.** Three `rand()` draws scaled to -0.5..0.5 (`-0x8000 - round(r / 32767 * -65536)`) are
  multiplied by the spread on each axis. With bit 0 of +0x36 they apply along a frame: d (the
  velocity), u (up, or the horizontal part of d when d's y is above 0.5, made square to d) and
  w = d x u. The frame is only built when both x and z of the velocity exceed 0xa0. Otherwise the
  original adds x's spread to x, z's to y and y's to z, both of the last with the third draw.
- **The fields.** A random scale adds the range times a fourth draw (0 to 1). Then the type's
  spawn callback runs.

**Each step** (0x4b0110, `Race_Update` after the cameras, not while a replay plays):
- The previous position is saved and the life drops by one; at 0 or below the particle dies.
- A step callback replaces the motion. Without one:
  - **Drag** pulls the velocity towards the wind (0x6a2ce8 times 0.1, never written, so zero).
  - **Gravity** comes off y; the position moves by the velocity plus half the gravity.
  - **Fade and growth** move towards their end by the step, clamped. A type dying at the fade's end
    dies on reaching it.
  - **Spin** wraps at 4,096.
  - **The ground.** Below it, a type dying there dies. A bouncing type has its y velocity reversed
    and scaled by +0x18, and x and z scaled by one less +0x1c. Under 0.1 it comes to rest and stays
    at the ground height.
- Then the after-step callback runs.

**The pose** (0x4b06a0 in `Race_PoseInterp`): the drawn position and alpha, between the previous and
the current at the step fraction.

**Drawing** (0x4b0480, World_Render after the sections' relighting, per pass): each live particle in
the pass mask becomes a sprite (or calls the draw callback).
- **The frame.** The animation frame is (age - start) / frame age. With bit 3 it is offset by the low
  16 bits of the particle's heap address modulo the count. The port takes its table to start at
  0x1000000, as the heap address can't be reproduced. Past the end it holds the last frame unless it
  loops.
- **The sprite.** The extents times the scale; the turn only for spinning types. Lit types carry
  the shade.

**The types** (0x45a170, the last call of 0x466360 when the cars are placed): the count follows the
effects detail, bits 25-26 of 0x52afd0: 400, 100 or 0. With 3 the count is an uninitialised local;
the port leaves it. Thirty types are defined, 9 and 25 are not:

| Types | Effect | Textures (0x459c80, `\Textures\NEWIMAGE\...`) |
|---|---|---|
| 0-2 | exhaust smoke | `Exhaust\exhst03`, `exhst02` |
| 3 | the backfire's flame (callbacks 0x45d2d0, 0x45de80) | `Det\13_12\DE_001-004` |
| 4-7 | snow and spray | `SnowCld\snow_004` |
| 8, 10-13 | dust clouds | `DustCld\23_5\D_000-007` |
| 14, 15 | splashes | `Wsplash\spray2f`, `spray2` |
| 16-23 | leaves and grass (dying below the ground) | `Leaf\leaf_000-004`, `GRASS\GR_001-004` |
| 24 | drops | `WSplash\drop` |
| 26-28 | sparks, paint chips, glass chips (draw callbacks 0x499070, 0x4994f0, 0x499ac0) | `spark4`, `paint`, `glass` |
| 29, 30 | gravel (bouncing; gravity from 0x49af20) | `Rocks\Grav_000-004` |
| 31 | the sparks' glow | the hazard lamps' sprite (0x547fb4) |

0x49af20 (height 1, time 25) fills the thrown stones' record at 0x593860 and returns 8 h / t^2.
Type 29 has a fifth of that as its gravity and type 30 all of it. 0x499020 writes two records of the
sparks and chips (0x592888, 0x593728).

**The wheels** (`Race_Update` each frame after the fades, not while paused or a replay plays). For
each listed car (its list index used as the car number) the sounds run (0x464c60, not ported), then
0x45def0, then 0x45af00 (not in the network mode or modes 11 and 12).

0x45def0 keeps tables per car and wheel (n = 4 car + wheel) in the game state:
- 0x543200: the ground point, the wheel's contact point (+0x3c0) 0.35 lower, turned with the car.
- 0x543400 and 0x543b58: the point of the last mark and the run since. A wheel more than 0.1 from it
  starts a new run; 1.3 or more counts as a jump (0x543140), which clears the run.
- 0x543788, 0x5435d0, 0x5430a0: the touch (+0xbac), the surface (+0xaae) and the steps on the ground.
- 0x5439b8: the car's wheels down.
- 0x543120: a shade byte, 255 less the car's +0xa70.

0x45af00 runs for a car still in the race with particles on:
- **The light** (0x45d1e0). The colour under wheel 0 (0x543f28, written by 0x462aa0, not ported)
  goes to 0x5435c0, its half towards white to 0x5435cc and its grey to 0x543808.
- **Grass and leaves** (0x45d540, not with camera kind 3). Each wheel down on a grass or leaf surface
  throws them. The count is kept from wheel to wheel. Above 10 of wheel 2's speed (0x42b600) it is
  (rand % 4) / 4, always 0; above 15 it is (rand % 3) / 2, so a third of the time one.
  - The particle leaves from the ground point, relative to the car. Its velocity is the wheel's
    (+0x42c) with x and z quartered and negated, and a random rise under 0.15 (0x45dc00).
  - Leaves (surfaces 0x19, 0x2e-0x32, 0x4f) are types 16 and 17 (left and right wheels), in one of
    ten colours (0x51abf8).
  - Grass is types 18-21 in a colour per rally and surface group, one to three a time (fewer when the
    car's weather is wet).
  - The spawn callback (0x45de80) notes the car at +0x64. The after-step callbacks add the car's
    position each step (0x45dea0, 0x45d270, 0x45d3a0), so the particles move with it. The leaves'
    0x45d3a0 also sways them on a circle picked by the particle's address.
- **Dust and snow** (0x45b580) come from each wheel on the ground (+0xbac).
  - **The axles.** One axle throws dust (type 8) or snow (type 4); the other only snow (type 6). That
    second axle is the rear on a car whose +0xb1e is 7, otherwise the front.
  - **Dust** comes from a dusty material or surface (+0xaae, +0xac6), each kind passing a random draw
    (material 9 a fifth of the time, the 0xa group two fifths, the 0xb group half), in any rally but 0.
    It also comes from a wheel spinning to 0.9 (`Wheel_Slip` 0x465d70, from +0x880 and +0x870) on
    materials 0xd and 0x18. There is none in the rain, and none in rally 7 (after its colour is set).
  - **Snow** comes from materials 0x11 and 0x13-0x17; on the second axle only 0x13-0x15, and not in
    heavy snow.
  - **The colour** is by rally and surface, white on snow. It starts white and is kept from wheel to
    wheel when not set.
  - **Fewer particles.** Below 30 of wheel 0's speed, only at that fraction. 30% fewer on the second
    axle, and half for any car but 0.
  - **The place.** A random point between the wheel's ground point and its axle partner's (w ^ 1).
    On the second axle the fraction is also divided by 1.33-10.33 and by 3-17. The point is moved
    back along the last mark's run by up to all of it. It is relative to the car.
  - **The velocity.** A quarter of the wheel's velocity (+0x42c), x and z negated, and a tenth of its
    rise. Above 35 of wheel 2's speed (50 on the second axle) it is scaled down to that speed.
    Otherwise 0.4 of the wheel's slip along its axes is added: a tenth of +0x870 along the forward
    axis (+0x6b4 front, +0x6c0 rear) and 0.3 of -0.5 to 0.5 along the side axis (+0x6cc, +0x6d8).
  - **A finished car** (race state 9), or one whose record flag is set (0x41f3d0, not ported: clear),
    takes type 7 (11 with dust). It is placed back by the wheel's velocity and thrown with it.
  - **Quirks kept.** The rally is read twice. An unused rand() % 2 is drawn for type 4. A counter
    raised on a slipping wheel is never read. Branches for type 5 and a forward shift on the rear
    axle cannot run.
  - The player's wheel spin tallies (0x45c750) are not ported.
- **The splashes** (0x45c820). A wheel in water (materials 0xe, 0xf) throws a splash sprite, type 14
  or 15. The two are mirror images, picked by the wheel's side and the car's side axis against the
  camera. On the first axle, half the time the other one is picked at random; on the second axle, half
  the time none is thrown.
  - **Quirk kept: the spray.** A wet surface (0x18, 0x19, 0x2a) in rain above 30 of 255 picks the
    spray (type 12), but the same branch clears the flag that lets a wheel emit, so the spray never
    comes.
  - **Quirk kept: the flag.** It carries over from wheel to wheel. A wheel after one in water throws
    type 0 (the exhaust's smoke) unless it is in water itself. Type 0 has no callback, so the puff
    sits about the world's origin.
  - **Where and how fast.** The particle sits between the wheel's ground point and its axle
    partner's, much nearer its own. It is thrown at a tenth of the wheel's rise and a fifth of its x
    and z backwards.
- **The stones** (0x49b3e0). Each spinning wheel of an upright car on the ground can throw stones
  (type 29) or mud (30). The kind comes from the surface in the rally (0x49b0d0), cached for the last
  surface asked.
  - **Per kind (13):** the colour (0x5206f8), stone or mud (0x52072c), whether it throws on a dry
    stage (0x520760), and the steps between two throws, at dry and at the wettest (0x520794).
  - **When.** The wheel's slips (the front ones turned by the steering) must sum above 0.35.
  - **The throw.** The stone leaves from the contact point, 0.35 down, along the slip's direction, by
    0.5 to 1 of the stones' speed, up by the same, and to a side by up to a third of it. Stones go
    twice as far along, half as high and 0.7 to the side. One thrown back towards the car is dropped.
- **The exhausts** (`Exhaust_Smoke` 0x45af90, last in the car's effects). Per exhaust (the kind 9
  lamps; none without a first one):
  - with the revs (+0x7a4) over 5 and the acceleration in x and z (+0x408, +0x410) under 0.01
    together, smoke at the exhaust (type 0, or the darker type 1 while the engine smokes, below),
    moving with the car plus its +0x360 vector times minus a hundredth of the revs, its ground 1 below;
  - the engine's smoke level is 380 less 350 times the damage record's engine (+0x404), 0 to 255; over
    80, with the acceleration under 5.8, it drops by a random 0-19 and a puff of type 2 comes from one
    of the four boxes at 0x51ab68 (in the car's frame, 0.2 lower), moving along +0x36c times 0.1. That
    0.1 then replaces the revs' share for the rest of the call: the second exhaust's test and velocity,
    and the flames';
  - a backfire (0x45b550, from the engine sound's backfire in 0x479360, not ported) sets the flame
    count (0x5439b0) to its frames. While it runs, one less a call, each exhaust gets a flame (type 3,
    at the origin: its step 0x45d2d0 puts it at one of the car's exhausts in turn) and its glow lit at a
    random level.

**Sparks** (`Sparks_Add` 0x499750). The callers give a strength, a point (relative to the car's first
model, which the particles take as their base), three rows and the most to throw:
- the walls (0x48fb80), on the metal-like faces (types 2, 14, 15, 18) when the car slides along the
  face faster than 0.5: the rows are the slide reversed, its cross with the face's normal and the
  normal; the point the deepest corner less the car's place, times 0.8, at height 0; up to 9;
- a loose part scraping the ground (0x483100): the scrape reversed, the ground normal and their cross;
  up to 4;
- the cars' contacts (0x48ae90) and the contact points' scrapes and bottoming out (0x469e40, 0x46a500).
Each throws 1 to that many (at random), each moving along the rows scaled by the strength times 0.2,
0.1 and 0.05 and a random share, the second and third rows one way or the other at random. For a
random share under 0.4 a paint chip (type 27) comes in place of the spark while the damage record's
chip count (+0x469) is under 8; then the record is marked (+0x46c) and the count goes up by 2 (also
for the sparks that stand in after 8). Not in mode bit 26 or for a car out of the race.

The spark's draw (0x499070) is a streak triangle: from the particle forward twice its velocity (at
most 0.35 long), as wide as a quarter of that length across the line to the camera, colour 0xfefefe
at the particle's alpha. Its step (0x4994d0) keeps the type's first alpha while more than 7.5 steps
are left. A paint chip (spawn 0x499710: its car and a random shape 0-9, the car's light +0xa70 as its
scale) is drawn (0x4994f0) as that shape in the car's paint (its .cin section 2's colour 0, 1 or 2 by
the shape), times the light ramp's colour at the scale. The ten shapes are made at the race start
(0x498dd0, after `Race_StartLights`): three random corners of 0 to 1, less a "centre" (the middle of
the first two, then halfway to the third, not the true centre), times 0.2.

**Glass.** When the damage breaks a panel (0x4692f0), `Glass_Queue` 0x49a910 compares its id with the
damage record's first four panels (+0x460-+0x463): the side windows queue one piece (kinds 0, 1), the
front and rear screens two each (2 and 3, 4 and 5), each with the car's impact (+0x5c4); other panels
throw nothing. The queue (ten a car) is thrown at the end of the car's loose parts step
(`Glass_Pieces` 0x49ab20, last first). A piece (0x49a870) takes its four corners from the car's .cin
section 2 (+12) by the indices at 0x5203e0, z negated by the flags after them. `Glass_Shards` 0x499de0
turns them by the car's orientation and cuts the panel in a grid of 10 x 7 cells. The panel's normal
is the cross of corner 0 less 1 and the diagonal, written over the diagonal while it is computed (so
its y and z use the new x), then turned by the car's orientation a second time. Each cell throws a
glass chip (type 28) at a random point in it: along the normal by up to 0.1 of a force (0.6 plus 0.2
of the impact along the normal, at most 1), less near the panel's edges, and away from the middle by
the rest. The chips (spawn 0x499a80: one of thirty shapes at random) are drawn (0x499ac0) at alpha
0xaa in a tint from 0x5203d8 times the light ramp at the car's light, each corner pulled toward the
camera by up to 1.5, with no age test. The thirty shapes (at most 0.1 from their centre) are made at
the race start (0x49ab90, before 0x494bb0).

Checked headless: a crash into a sign on fin01 breaks a side window and throws its 70 chips.

On screen the sparks are not yet checked: the runs so far hit no metal faces.

## Option 0's balls

With option 0 on the super special, or with selection bits 24 or 27, the handbrake fires a glowing ball
(`src/game/balls.c`). The test is the same in the set-up (0x466360), Stage_Step (0x466520) and the
car-to-car pass; the glows are placed whenever option 0 is on (Race_PoseInterp 0x41d060).

- **The pool:** 100 records of 0x5c bytes at 0x58e4c8. Each holds the velocity, position, ground
  normal and brightness (+0x00-+0x27), the last step's copies (+0x28), the ground's height (+0x44),
  the life (+0x48), the ground triangle (+0x4c), a glow (+0x50), the in-use flag (+0x54) and the car
  (+0x58). The set-up (0x47d510) gives each ball a kind 1 glow, hidden, of size 0.2 with the hazard
  lamps' sprite and shaft, a pool of 0xb4 at twice the size. It also clears the cars' timers
  (0x58e4a8).
- **The glow pool is too small:** with option 0, 0x40efa0 allocates only 4 glows (160 without it).
  So only the first four balls get one, and the cars' lamps that come after get none. This is kept.
- **Firing** (0x47d5a0, from Car_Boost 0x4932f0 with +0xb9c and the handbrake +0x1d8): only when the
  car's timer has run out, into the first free record.
  - The ball starts between the first two box corners, raised along the up row (+0x36c) by +0x774:
    just past the nose.
  - Its velocity is 0.8 along the forward row (+0x360) plus the car's, kept along the forward row.
  - It lives 50 seconds and sits on the ground (Car_PointGround 0x4932b0) half a unit up. The car
    waits 16 seconds for the next one.
- **The step** (0x47dd70): the timers run down. Each ball keeps its last step, ages and moves by its
  velocity once a step (with no time factor), back onto the ground.
  - It leaves a type 0x1f particle (the sparks' glow). The particle starts behind the ball and drifts
    at 0.3 of a random push, taken across the path less half the path, plus the path.
  - The ball fades over its last five seconds (0.2 a second).
- **A hit** (0x47d850, per car in Cars_Collide after the pairs): a ball of another car within 100
  units, inside the car's box y range and flat extents, hits the car.
  - The impulse is the ball's direction raised by 0.4, times 6. It is applied at the ball's offset
    lowered by 3.5, so the car turns.
  - The car is lifted by 0.25 and made loose (+0xc00, +0x96c 1.0). It is damaged (kind 2) along the
    impulse, and the ball is gone.
  - The direction is made unit without a test for zero; a ball always moves, so the port's guard
    never acts.
- **The glows** (0x47e1e0) are placed between the steps: the brightness, and a plane half a unit under
  the ball with the ground's normal. The glow's direction is set to its position, as in the original.

Checked in race_test: a ball rolls ahead of its car, and one inside another car's box knocks it.

## The start grid

`Cars_Load` 0x456d90 loads `Cars_Count` 0x456ca0 cars: `Cars_Racing` 0x420190 (selection bit 26, not
networked: the players and the CPU cars from the low nibble of 0x52f2a9; bit 27: two; otherwise the
players), plus one for the ghost (bit 28 with 0x406310), clamped to 1..8. The CPU cars come after the
players (car n is a CPU car when n >= the player count, `Sel_CpuCar` 0x52ea98).

With selection bit 22 or 31 it first calls `Cars_Grid` 0x4584d0, which fills the grid at 0x542cd8
(twelve bytes per car, x and z used). Case 0 (the arcade; confirmed from the disassembly, jump table
0x4589c4): the direction is the centre line's step from its last point before the finish
(`Cat_FinishPoint() - 1`) to point 0, an angle by `atan2` (0x498d80, degrees at 0x5113f8, x 65536)
turned to a table index by 1/5760. Car i gets slot k: k = i, the championship order at 0x533620 in
mode 5 with more than two cars, 1 - i with two players. The slot is 5 k - 5 n units back along the
direction (-10 for two cars), 4 units to one side for even k and the other for odd, so slot 0 is
the last; the car's progress start (0x542d38 + 4 k) is the last point. Cases 1-3 (bit 27's head to
head, bit 31, the network) are pending.

`Csp_StartPos` (0x4583d0's part) returns the grid entry when selection bit 26 or 31 is set.

## Fireworks

The super special (rally 8 stage 11 in mode 0, `DAT_0058896c`) sets up 20 rockets (0x47e4d0, the end
of 0x466360), records of 0x938 bytes: the position and velocity, a ring of 20 trail points, 18 sparks
(three rings of three directions at 15, 45 and 75 degrees, each with one mirrored in y), each drawn
mirrored four ways in x and z; the last step's and the drawn copies of all three (0x480600 in
Race_PoseInterp); timers, colours (six from 0x51f4ec, the flash's from 0x51f504), flags.

Race_S10_Replay launches one each frame (0x480380: from one of four pads, 0x5909c8 plus 0x51f4e0, a unit
vector up 4 and out up to 1), into the first free slot (0x47fcb0). A rocket rises against 0xa3d a step,
dropping four scattered trail points a step, until its fuse (25 steps; a burst's rockets 5-15) runs
out; then the flash (0x5909b8 = 1.0, decaying by 0.5 a step in 0x480220, its square root times the
flash colour over the low light colour 0x5909c0) and either a burst of sparks (the bangs) or, for kind
2, one to four smaller rockets, which always burst (their kind test is above 1.0). Sounds are the
eleven of FireWork.bfl; the whistle's voice is kept in a byte, so it is never found to stop it early
(the port keeps that).

## Knocking scenery down (ported: src/sim/knock.c)

Movable scenery (record flags 0x2001000, the movable cell lists 0x58c94c, `n_m` 0x58ca6c) is knocked
by `Car_Scenery` 0x4878a0 after a hit and `0x48be20`: `Scenery_Knock` 0x46fe70(entry, cell, car).

- Standing bits 0x58c938 (a bit per car per movable entry; 0x471d40 test, 0x471d80 set/clear). A knock
  clears the car's bit and, with one player and the car not +0xc0c, lowers the static instance by
  1000 (scenery +4) and clears its drawn byte +0x14; `0x470240` undoes it. `0x471cc0` gives an
  entry's position, +1000 in y when not standing (one player: bit 0; more: 0x58c958).
- The pool 0x5894e0: 40 records of 0x128 bytes (count 0x58c924). Ints: [0] the entry, [1] its object
  (Obj_New under the world root, kind 3 hidden / 0 shown, +0x17c the car's pass bit, +0xc the mesh),
  [2..0x11] the matrix, [0x12..0x21] the last step's, [0x22..0x31] the drawn (0x471950 blends, flags
  [0x49] when it changed; 0x471a60 copies to the object +0x98/+0xd8 and relights via 0x4b5320 with
  the handle [0x45]), [0x32] a pointer to [2], [0x33] the state (0 still, 1 tipping, 2 flung),
  [0x34] the shape (0x58c930: 1 when x is under 0.3 of y, else 0), [0x35..0x37] the ground normal,
  [0x38..0x3a] the spin, [0x3b..0x3d] the velocity, [0x3e..0x40] the pivot, [0x41..0x43] the half
  extents (mesh +0x10c record +0x44/+0x4c/+0x48 times 0.5), [0x44] the ground height, [0x46] the
  cell, [0x47] (short) the ground section, [0x48] on the ground.
- 0x46f550 (Scenery_CollInit's end): the pool objects; per movable entry a mesh clone (0x4abaf0, the
  placement's scale baked in) and its shape.
- 0x46f810 starts a record: shape 1, or shape 0 at a car speed (+0x778) up to 0xc000: tipping, with
  the spin from the car's velocity (+0x408) turned into the object (scaled to 0.5 / speed above
  0x8000); else flung: the velocity 0.8 of the car's, the spin from it at 0.4.
- 0x470580 per step: state 1 spins up by 1.1 a step about the object's axes (Rows_Turn3, the spin
  times 0x519c8c (1.0) as radians), and once its up row is level with the ground (dot with the normal
  at or below 0) squares it onto the ground and stops; state 2 falls (gravity 0xfffff5c3 a step until
  on the ground, halved with option 1), bounces at 0.8 and settles below 0x28f; both keep the lowest
  box corner (0x589458, chosen by the down direction, 0x4702f0) on the ground (Hpc_Ground).
- When the pool is full, each car's farthest record is counted (0x58c424: flat distances, max-axis
  normalised then the table square root, 0 within 0x290; 0x5894b8 the byte counts) and the record
  farthest from the most cars (the first of equals) is reused, its entry put back up for all eight cars
  (0x470240). 0x4702a0 (restarts, replays) puts everything back.
- Details of 0x470580 (confirmed from the disassembly): the spin's angles are round(fx(spin, dt) *
  0.009947183943243459) (the double at 0x511380, radians to 4,096ths) as u16, Rows_Turn3 0x429f20 on a
  3 x 3 copy of the rows. Tipping: the lift is measured with the rows before the turn and added to the
  position's y, then the turned rows are set; at rest r1 = unit(r1 - n (r1.n)), r0 = unit(r0 - r1
  (r0.r1)), r2 = unit(r0 x r1). Flung: the world pivot P moves by fx(v, dt) less fx(g, dt / 2); on the
  ground spin and v fall by 0.8 a step and both below 0x28f stop: r2 eases a tenth a step onto the
  ground normal (negated when r2.n < 0) until within 0x1999, then r0 = unit(r0 - r2 (r2.r0)), r1 =
  unit(r2 x r0); in the air the ground is tried only while v.y < 0 and taken when the lift is above
  -0xccc. The position is then P less the pivot through the new rows.
- 0x4702f0: off the collision mesh the normal is (0, 1, 0) and the ground ten units under P; the
  lowest corner is (8 corners, the car shadow's order) index (d.y >= 0 ? 4 : 0) + (d.x < 0 ? 2 : 0) +
  (d.z < 0 ? 1 : 0) with d the negated normal through the transposed rows; the result is
  (n.(P' - corner)) << 16 / n.y with P' at the ground's height.
- 0x4abaf0 copies the record's first 12 bytes, the counts, +0x2c, +0x30, +0x104, +0x108, +0x111-+0x114
  and the vertex, polygon, 0x14 and 0x34 arrays; the vertices (rounded to 16.16, less the placement's
  position) and normals go through the transposed unit rows. The copies are counted by a byte
  (0x58c320): past 255 movable entries they would wrap (none of the stages has that many).

## Skid marks

The skid marks (0x464c60-0x465ea0, `src/game/skids.c`) are kept per car (8) and wheel in a ring of 200
segments. Each segment is two ground points either side of the wheel. The marks are drawn as quads
between two segments.

**Tables** (all in the game state). A segment is 0x28 bytes at 0x549da0 + (800 car + 200 wheel +
n) x 0x28:

| Offset | Field |
|---|---|
| +0x00 | age (expiry and fading read it; nothing raises it) |
| +0x04 | the frame it was laid in (0x588750, raised once a frame by car 0) |
| +0x08, +0x14 | left and right points |
| +0x20 | colour (3 bytes) |
| +0x24, +0x25 | the left and right alphas |
| +0x26 | the surface class (low nibble) |

The other tables:

- The alive bytes are at 0x548220 (the same index).
- Per wheel (4 car + w, ints): the ring head is at 0x549ba0. The "move on at the next step" flag is at
  0x5885a0. 0x543708 and 0x549b20 record whether the last segment was laid on hard or soft ground.
- The running colour is at 0x588620 (4 bytes a car).
- The wheels' ground points are at 0x549c20 (0x30 a car).
- A surface's class is the signed byte 0x51bcb0[material]. Classes are 8 bytes at 0x51bc68. The flags
  byte has bit 0 (marks on hard ground), bit 1 (soft ground) and bits 2-3 (the texture: 1
  `skids_mark`, 2 `skids_blank`, else `skids_1`). The colour is at +4.
- The span L is at 0x51bca8.

**Load** (0x465600, from the cars' placing 0x424c50). It resets everything (0x465530: segments dead,
the per-wheel words zeroed, 0x543140 set to 1, the runs 0x543b58 zeroed). It loads the three
`\NEWIMAGE\skid\` textures from the common archive. L is 4 in modes 5-7, else 2. Class 2's colour is
0x6f7a89 in rally 0 and 0x4389b7 in the others, and it is copied to class 7. The code tests rallies 4-5
apart from the rest but gives them the same colour. The reset also runs when the finish is counted
(state 9) and on a restart (0x41e6b0).

**Each frame** (`Race_DrawView` 0x40f0c0, after the world and not while paused). The view's own car
gets new segments; other cars leave none.

- **0x4657d0** sets each wheel's ground point. It is the middle of the shadow record's two points for
  the wheel (+0x198 + 12 w and +0x1c8 + 12 w), raised by 0.05.
- **0x464cb0** (cars below 8; nothing at effects detail 2):
  - **Expiry.** An alive segment older than 80 L frames (4 L when it rains or snows above 0.2) dies.
    The ages are never raised, so this never happens.
  - **When a segment is laid.** The wheel must touch (+0xbac). Then one of two conditions:
    - hard ground: both this surface (+0xaae) and the last frame's (0x5435d0) have class flag bit 0,
      and 0x5430a0 > 2, 0x5439b8[car] > 1 and +0xb74 == 1;
    - soft ground: both surfaces have bit 1.
  - **Alpha.** The alpha is the wheel's slip (`Wheel_Slip` 0x465d70) times 255. On soft ground with
    the other wheel of the side touching too (w ^ 2), it is half instead, and no segment is laid on
    material 0x19. Otherwise the slip must be above 0. The wheel's run (0x543b58, from
    `Wheels_Track`) must be nonzero in x or z, and 0x543140 clear.
  - **Points.** The segment at the head gets the ground point plus and minus run x up, made unit,
    times 0.03. That width is scaled by 0.6 for narrow wheels (0x42cae0 mode 1), or by 2.5 instead with
    option 6. Both points are lowered by 0.008 + 0.002 w (-0x20c - 0x83 w).
  - **Colour.** On hard ground the car's running colour moves half way to the last surface class's
    colour. The segment gets that colour times the wheel's light (0x543f28, /255). On soft ground the
    running colour becomes the class colour times the light at once.
  - The next segment's alphas are cleared and the head moves on at the next step (0x464c60, from
    `Race_Update` per listed car). A wheel that lays nothing still moves its head on if it laid a
    segment before.
- **0x465780(view)** draws, for each car below 8 not hidden from the view (0x588cd4[view + 2 car] !=
  7), each wheel (0x4658e0):
  - Back from the head, the segment is paired with the farthest earlier alive segment laid exactly
    k frames before, for k = L - 1 down to 1. The walk then goes on k segments back.
  - The alphas fade with the segments' ages when b's red is above 200 or b's age above 1,240. The
    ages are 0, so this never fades anything.
  - The quad is skipped when all four alphas are 1 or less, or when the older segment's two points
    coincide in x and y.
  - The two triangles (a.R, a.L, b.L) and (b.R, a.R, b.L) use bucket 0x20 with flags 6 (no z write,
    no culling). `skids_blank` is drawn black with a third of the newer segment's alphas at both
    ends. Race_DrawView queues the marks after the world has drawn, so they show with the next
    frame's world.

## Car shadows

The cars' shadows (0x494b10-0x498590, `src/game/shadow.c`) are triangles laid on the ground under each
car: a patch under each wheel, a quad under the body and an outline cast away from the sun. They go
into triangle bucket 8 (flags 10: no z write) with no texture, after being pushed one unit toward the
camera. The bucket is flushed after the stage's overlay and the glows, so the shadows are never hidden
by small bumps in the ground; whatever is drawn after them covers them.

**Record** (0x2a4 bytes a car at *0x592734, count 0x592738; allocated zeroed by 0x494b50, freed by
0x494b10, car n's at 0x498570). Points are world positions, 16.16:

| Offset | Field |
|---|---|
| +0x000 | four points a wheel (0x30 each): A, B on one side, C, D on the other (A, C forward) |
| +0x0c0 | the points: the body's quad (0-3), then the outline (4 on); 18 at most |
| +0x198, +0x1c8 | each wheel patch's front and rear middles (12 bytes a wheel, read by the skid marks) |
| +0x1f8, +0x240 | the last ground heights of the points and the wheels |
| +0x250 | the outline's inner ring scale (0x9999 at the placing, then set by 0x4984b0) |
| +0x254 | each wheel patch's strength (16.16) |
| +0x264, +0x26c | the ground triangles of the wheels and the points (s16, kept between calls) |
| +0x290, +0x292 | the quad corner the outline starts from; the number of points (4, or the profile's + 5) |
| +0x294 | box only: the quad from the box's corners, no outline |
| +0x298 | the wheel patches on |
| +0x29c, +0x2a0 | the outline from point 5 (its extra point at 4); the outline in profile order |

**Placing** (0x494bb0, in 0x466360). In modes 5-7 every car but car 0 is box only. Car 0 is set to
"not box only", and its wheels are set again when 0x4074f0 is 1, which changes nothing since they
are already on. In the other modes no car is box only. All have their wheels on, scale 0x9999 and
strengths 1. The profile is the car's .cin section 1:

- byte 0: the profile's point count N;
- bytes 1-2: the range (first, count) of outline points a damaged car leaves out;
- +4: a sideways offset W;
- +8: the wheel patch's half length;
- +0xc: N pairs (along, across).

The five pointers are kept at 0x592278, 0x5922bc, 0x592344, 0x59229c and 0x5922fc.

**Box corners** (`Car_Shadow` 0x487140, in `Car_PoseInterp` and `Pose_Init`). The drawn pose's eight
box corners are kept at 0x53c5a0 + 0x60 car. With a, b and c the pose's rows 0-2 times the half
extents +0x204, +0x208 and +0x20c, the corners are a-b+c, a-b-c, -a-b+c, -a-b-c, a+b+c, a+b-c, -a+b+c
and -a+b-c, plus the position.

**Each frame** (0x496e00, the first call of 0x4664c0 for each listed car without +0xb70). The pose
record (0x42ca70), the wheel matrices (0x42cab0) and the box (0x42cac0) are copied to globals.

- **Ground point** (0x5922e0). The position is moved along the ground normal (pose +0xe0) to the
  plane of the first box corner, taken at the pose's first height (+0xec). In the air (+0xb64) only
  the height difference times the normal's y is used.
- **Wheels** (0x497db0, when +0x298 is set). The car's tilt is (up . normal - 0.8) x 5, clamped to 1;
  in the air it is 1. Each wheel still on the car (its parent not the world) gets:
  - a centre at the wheel matrix's position in the pose, lowered 0.35 along the car's up row (x 2.5
    with option 6);
  - with `Car_PointGround` 0x4932b0 (when on the ground), the centre is dropped to the ground. The
    strength is 1 - min(4 h, 1), where h is the height it fell, times the tilt;
  - the forward half length is the wheel's row 2 in the pose times the profile's half length (x 0.6
    for narrow wheels, 0x42cae0 mode 1). The side half width is up x forward, times 0.35;
  - the four ends A-D, laid on the ground's plane.
- **Body quad** (0x4962c0). On the ground (or loose, +0xc00, even when box only):
  - the forward axis is the car's row 0 laid on the ground, or row 1 when row 0 is within
    0.99 of the normal. The side axis is forward x normal;
  - the reach along each axis is the largest positive projection of the eight corners;
  - the corners are F - S, F + S, S - F and -F - S, from the ground point.

  In the air the car's own rows (+0x360, +0x378) and extents (+0x204, +0x20c) are used, and the
  corners are the car's +0x270 points moved to the ground point; corners 2 and 3 are swapped. A box-only
  car takes its box corners 1, 0, 2, 3, at the pose's four heights on the ground. A box-only car
  driving another model than its class (slot model != +0xb1b) has the quad pulled in or out. The
  pairs (1, 2), (0, 3), (1, 0) and (2, 3) (bytes at 0x5201c0) are scaled about their middles, by the
  model's entry in 0x520150 for the first two and in 0x520188 for the others.
- **Outline** (when not box only and render option bit 0x20 is clear). It uses the sun direction at
  0x5201c8 (x, z, unit and reversed) and 0x5201d4 (its length, 2 (1 - y)), written by 0x498370 from
  the sky's sun (0x463070, not ported: the port keeps the exe's initial value, (1, 0) and 1).
  - The along and across sun terms are the forward axis's horizontal direction dotted with the sun,
    and crossed with it, each times the length.
  - The across term is scaled by the car's tilt ((up . normal - 0.8) x 4.9998, clamped to 1).
  - The N profile points are scaled along the axis by reach / pair 0's along value. They are shifted
    by the along term and pushed across by the across term and W. The inner points also move toward
    their neighbour by |cos|.
  - The points are written in profile order or reversed (+0x2a0). A last point at the kept end
    crosses the car's whole width. The start corner (+0x290) is 1, 0, 2 or 3 by the two terms' signs.
  - The outline is then relaxed: going round from its first point, each point must be at least 1.5
    degrees further round the ground point than the one before (angles from the forward axis, by
    the arcsine table). A point that is not is moved onto that angle, at its own distance.
- **Drop.** Unless box only, every point is dropped onto the ground (`Hpc_Ground`), at most 0.5 a
  frame.

**Drawing** (0x494db0(car, view), in the cars' pass 0x466570). The cars drawn are those not hidden
(+0xc0c, +0xb68 view) whose view mode 0x46bd20 is not 7. In a shared replay (0x41f3a0) a car is drawn
with view 1 under 0x407e90 or 0x407e70, or when it is camera 1's car; the port leaves the last case
pending. The record is copied to 0x592490 and the copy's points (and the wheels' when on) more than 2
from the view's camera are moved a unit toward it (0x495f50). The colour is 0x4b4a80's shadow colour
(R, G, B at 0x6e0098) and the alpha is 0 at the outer edges.

- **Wheels** (+0x298). For each wheel still on with a nonzero strength, the live record's front and
  rear middles are written (+0x198, +0x1c8) for the skid marks. Four triangles join the ends to the
  copy's middles: (A, B, F), (F, B, R), (F, R, C), (R, D, C). The middles F and R have alpha strength
  x 230.
- **Outline** (not box only, bit 0x20 clear). Points 0-1 are quad corners from the start corner,
  then the N + 1 outline points (a damaged car, Damage +0x4bc, leaves out its range, 0x494d40), then
  the other two corners. With L the count less the damage range, an inner ring is made toward the
  quad's centre, by 0.99 - (1 - scale) x 0.14. Three triangles per edge, for L + 4 edges taken modulo
  L + 4, so the last point is never drawn (the original's). Alpha: the shadow alpha (0x6e0b38) times
  the car's ground light +0xa70.
- **Quad** (always). Each edge is cut at 5% and 95%, making 8 points. An inner ring is made at 0.6
  toward the centre, with three triangles per edge. Alpha: 255 for a box-only car. Otherwise it is
  255 (1 - min(2 h, 1)), with h the lowest of the car's eight corner heights over their ground
  (+0x274 - +0x8dc).

**The light under the wheels** (0x462aa0(car, view), for every car in `Race_DrawView` 0x40f0c0 before
the lamps).

- For each wheel on a collision triangle (+0xa9e), the stage's `.xhi` light (16.16 per collision
  vertex, 0x6dfe10) is read at the wheel's corner point (+0x270 + 12 w). 0x4b6340 interpolates it on
  the plane through the triangle's (x, light, z); an edge-on plane gives -1000.
- The light's terrain colour (0x4b3ae0) is kept at 0x543f28 (4 bytes a wheel). The skid marks and
  `Effects_Car` read it.
- The mean light, clamped 0 to 1, is the car's +0xa70. Its colour less the low colour lights the sun
  (light 0x5920b0, 0x4b34d0) for the view's car (`Camera_Car` 0x422fb0), at a 255th per channel
  (0x492e60).
- The mean plus the summed channel differences, x 0x55 / 65536 (at most 1), is passed to 0x4984b0.
  That sets the record's scale (+0x250) and the texture factor's alpha (render state 60) to
  100 + 75 v. It also stores (0, 0, 0, 75 v) at 0x520f94, which nothing reads.

**The port.** The port's stand-in for the cars' drawing (see the frame) runs after bucket 8 is flushed,
so the cars cover their shadows; the original's drawing of the cars is not found yet. The world matrix
is set back to identity after them, for the queues and billboards drawn next.

## Loading a stage

`Stage_Load` 0x41f930 (the start-up of the game fills the math tables first, 0x4d08d0), in order:
1. the country's texture archive (0x4b23c0) and the loading bar (0x40fec0);
2. the horizons, the BSP and `TEMP.C3D` (`Stage_LoadC3d` 0x41fc50);
3. `.hpc`, `.srf`, `fin.dat`, 0x471af0, `.csp`, `.cat` and the co-driver's `.cod`;
4. the `.tsc` splines (0x48caa0) and the sections (0x4b8270);
5. `.tre` and `.xhi`.

Each model goes through `C3d_Load` with two arguments:
- the parent object at `0x536be0`, under which the model's roots hang;
- the texture archive slot at `0x538858`, used only when it is loaded.

`Stage_LoadScenery` 0x471af0 (step 3, before the sections) loads `TEMP.OBJ` (`Stage_LoadObj`
0x472830), `TEMP.SHT` (0x472870) and the sky and ground (`Stage_LoadSkyGround` 0x46f060) the same way.
It notes the object and record counts before each, which are the ranges those files own.

## The port

`src/world/obj.c` copies each record into an `Obj` with real pointers, since links cross models. The
record stays reachable for the fields not yet understood. The update, commit, draw and view are
ported, and so are the grid, the lookup, the corners, the visible list and the terrain draw
(`src/world/section.c`) and `Stage_LoadC3d` (`src/world/stage.c`). The scenery and billboards are ported
too (`Stage_LoadScenery`, `Section_DrawBillboards`, `Billboard_DrawQueued`). So are the sky and ground
(`Stage_LoadSkyGround`, `Sky_Follow`, `Obj_UpdateSection`, `Section_DrawObjects`). So is `World_Render`
(`src/world/frame.c`), with the sorted pass and the triangle queues (`src/render/polyq.c`). `.tre` is parsed into its shadow
meshes (`src/world/tre.c`). The time of day and the horizons are in `src/world/tod.c`, and the lights
and relighting in `src/world/relight.c`. The stage viewer lights a stage at its start time. With `--param car=` as well, it parks the car at
the start line (`.csp` position and angles), at the `.hpc` ground height under it
(`src/world/hpc.c`), and draws it where the cars' pass goes. It goes through `Car_Place`,
`Car_Init` and `Car_Settle` (step 6), which put the body on the corners' ground. The models' poses
come from `Car_PoseInterp` once the simulation runs; until then the viewer sets the main model to the
settled position. The calls
not ported yet are left out in order, and they are listed there. Still to come: the
stage height offsets for the sky, the overlay, the object draw per cell, the edge heights, `Obj_UpdateSection` and the section test. The car viewer hangs the car
under a turning stand and takes the view from a camera object, so a car on screen goes through these
functions. `tests/d3d7_test.c` checks the 205's tree: world = local plus the stand's move, the float
matrix, the inverse view, and all 672 triangles drawn through `Obj_Draw`. The stage viewer loads `TEMP.C3D` with
`Stage_LoadC3d` and draws what `Section_BuildVisible` lists for a camera object (about 25 of 360 cells
with the far plane at 250). `tests/world_test.c` checks fra02's grid (18 x 20 cells of 64), the lookup
and a camera's visible cells without a GPU.
