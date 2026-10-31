# Status

**Steps 3, 5 and 6 of the port.** Not a game yet: a car can be driven on a stage, with no race around it. Done so far:

- The disc analysed: every data file read by a reference decoder (`tools/bfl.py`), the formats of the
  containers, models, textures, sound and music documented ([file formats](../formats/index.md)).
- The program `CMR2.exe` v1.11 analysed in Ghidra: 3,583 functions, the main loop, the mode and process
  framework, the file layer, the model loader, the renderer's use of Direct3D 7, the input defaults and the
  network layer identified ([architecture](../re/architecture.md), [inventory](../re/inventory.md)).
- The [port plan](../re/port-plan.md): eleven steps from the file layer to network play, each ending with
  a check. Its early questions are answered: the simulation runs in fixed 40 ms steps, replays are input
  recordings, the handling is in the code.
- Step 1: `opencmr2.wasm` builds for gasm 0.16.0 and starts from the CD image or the CD's
  files. The file layer reads folders and ISO images, the InstallShield cabinet in place (so `CMR2.exe` and
  the demo runs need no install), the gzip and CMPR layers with the port's own inflate, and the archives,
  with `File_Load` and the `Bfl_*` lookups ported. A native test reads every file and member of the disc
  and matches the reference decoders; the exe check reads the first tables (race states, cheat codes) from
  the user's `CMR2.exe`.
- Step 2: decoders for every texture (TGA, DXT5), sound (PCM, MS ADPCM music), the text tables, the fonts
  and the models (PP_F with its relocations checked), each agreeing with a Python reference decoder over
  the whole disc; `tools/cmr2fmt.py` exports textures to PNG, sounds to WAV and cars and stages to OBJ.

- Step 3, in progress: a Direct3D 7 device on OpenGL ES 3.0 (transform and lighting, the texture
  stages, fog) and the original's render-state layer; cars and stage meshes render with their
  textures in a headless viewer (`--param car=f99a1N`, `--param stage=fra02`).

- Step 3 continued: the original's texture records and stage blending, mesh drawing, lighting modes,
  projection and fog, the 2D queues and the text drawing, the fixed-point math tables.
- Step 5, started: the scene's object trees (update, lights, drawing, the camera view) and the stage's
  section grid with its visible-cell list and terrain drawing. The viewers draw a car and a stage
  through these, with their title in the game's font.

- Step 5 continued: the stage's scenery and billboards, sky, clouds and ground, the tree shadows, the
  time of day with its horizon records and the stage lights, the collision mesh, the start line, and
  the game's frame function (`World_Render`).
- Step 6, started: the car simulation in the original's 16.16 fixed point, function by function from
  the disassembly where the decompiler drops steps. Ported: the car set-up and placement, the controls,
  gearboxes and steering, the tyres, drivetrain and revs, surface grip, suspension and springs, ground
  contact and road following, the integration, the walls along the stage's edges, a loose car's roll and the recovery onto the road. A car parked on fra02's start line rests on the road,
  and on full throttle it drives off and shifts up (`tests/sim_test.c`). With both `--param stage=` and
  `--param car=` the viewer lets you drive it from the keyboard.

- The race around it: the original's process framework and race states, with the countdown, the stage
  time from the go and the finish; the chase camera. The loads and the title are stand-ins.

Next: the rest of the simulation (car-to-car and scenery collisions, damage, the other entities of the
step), the race's cameras, HUD and results,
the car's pose interpolation and wheels, then the race around it.

| Area | State |
|---|---|
| File layer (folders, ISO, BFL, InstallShield cabinet) | Ported and tested against the whole disc |
| Textures, models, sound, music, text, fonts | Decoders ported and tested against the whole disc |
| Stage data (`.tre`, `.hpc`, `.cat`, ...) | `.tre`, `.hpc`, `.csp`, `.hor`, `.cat`, `fin.dat` ported; `.srf`, `.tsc` open |
| Renderer (Direct3D 7 to GLES 3) | Device, state layer, textures, meshes, 2D and text ported; reflections and 16-bit formats open |
| World (objects, stage sections) | Object trees, terrain, scenery, sky, shadows, lights and the frame function ported; the cars' draw pass open |
| Race flow | Process framework, race states and Race_Update ported for one player; the chase camera ported; the other cameras, HUD, replay and results open |
| Front end | Mapped (0x4d0000-0x500000); not started |
| Vehicle simulation | About 65 of 120 functions ported; a car drives on a stage, is held by its walls, rolls and is recovered; car-to-car and scenery collisions, damage and the step's other entities open |
| Movies (Bink 1) | Not started |
| Network (DirectPlay 4) | Located; last step |
