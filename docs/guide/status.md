# Status

**Steps 3 to 7 of the port.** The game starts from the CD image, runs through its own menus to a single stage and back, and the stage can be driven. Much is still missing (see the table). Done so far:

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
  time from the go, the splits and the finish (the sectors); the original's stage and car loading, the
  common archives and race fonts, the chase camera, the drawn poses (body on its suspension, wheels),
  the recovery's fade, the HUD's stage clock, split bar, split difference and standings, the interior views and the camera key, and the engine
  sounds, the collision sounds and the co-driver's pace notes through a DirectSound emulation and mixer. 

- The front end, started: the boot screens, the menu engine, all 94 pages built, and the pages from the
  language through the main menu, mode, difficulty, players, driver, tag, car, transmission, country,
  stage and weather to the start ([front end](../re/frontend.md)). Leaving it chooses the rally's
  weather and set-ups and starts the race; after the stage it comes back to the menus. In the race,
  Escape opens the pause menu (sound volumes, restart, exit); after the finish the results show the
  split and stage times and the stage record, and offer a restart or the way out. The set-up and
  service screens between them (mode 2), the movies, the options and the saves are not done yet; the menu music plays.

Next: mode 2's set-up and service screens and the race's results, the rest of the front end's pages,
then the rest of the simulation (car-to-car contacts, knocking scenery down, the other entities of the
step) and the race's cameras and HUD.

| Area | State |
|---|---|
| File layer (folders, ISO, BFL, InstallShield cabinet) | Ported and tested against the whole disc |
| Textures, models, sound, music, text, fonts | Decoders ported and tested against the whole disc |
| Stage data (`.tre`, `.hpc`, `.cat`, ...) | `.tre`, `.hpc`, `.csp`, `.hor`, `.cat`, `fin.dat` ported; `.srf`, `.tsc` open |
| Renderer (Direct3D 7 to GLES 3) | Device, state layer, textures, meshes, 2D and text ported; reflections and 16-bit formats open |
| World (objects, stage sections) | Object trees, terrain, scenery, sky, shadows, lights and the frame function ported; the cars' draw pass open |
| Race flow | Process framework, race states, Race_Update, Stage_Load, Car_Load, sectors and the race clock ported for one player in a rally; the chase camera, the HUD's clock, split bar, split difference and standings, the pause menu and the results; the other cameras, the HUD's messages and the other modes' panels, and the replay open |
| Front end | The process, menu engine, every page's construction and the single-stage path's pages ported; the options, profiles, championship, arcade and network pages, the saves, mode 2's set-up and service screens open |
| Vehicle simulation | About 65 of 120 functions ported; a car drives on a stage, is held by its walls and stopped by the scenery (trees, posts, walls), rolls and is recovered; hits dent the car, wear its handling and break its glass, and are carried to the next stage; a broken engine misfires; car-to-car contacts, knocking scenery down, the loose parts and the step's other entities open |
| Sound | A DirectSound emulation and mixer, the sound layer, the engine loops, the co-driver's calls and the collision sounds (hits, rolls, glass) ported; the menus' sounds and music ported; backfires and surfaces open |
| Movies (Bink 1) | Not started |
| Network (DirectPlay 4) | Located; last step |
