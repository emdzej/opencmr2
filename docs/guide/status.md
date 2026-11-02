# Status

**Steps 3 to 7 of the port.** The game starts from the CD image, runs through its own menus to a single stage and back, and the stage can be driven. Much is still missing (see the table). Done so far:

- The disc analysed: every data file read by a reference decoder (`tools/bfl.py`), the formats of the
  containers, models, textures, sound and music documented ([file formats](../formats/index.md)).
- The program `CMR2.exe` v1.11 analysed in Ghidra: 3,583 functions, the main loop, the mode and process
  framework, the file layer, the model loader, the renderer's use of Direct3D 7, the input defaults and the
  network layer identified.
- The port plan: eleven steps from the file layer to network play, each ending with
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
  stage and weather to the start. Leaving it chooses the rally's
  weather and set-ups and starts the race; after the stage it comes back to the menus. In the race,
  Escape opens the pause menu (sound volumes, restart, exit); after the finish the results show the
  split and stage times, the stage record, the board and, in a rally, the rally's times, and offer
  the way on, a restart or the way out. A championship goes on from stage to stage, through the
  service area (repairs, set-up, the stage information, saving) after every second stage, with its
  times, points and unlocks kept and the rally's points, result and the championship's table at
  each rally's end; saved championships load from the front end. The page of what a stage opened,
  the movies and the network game are not done yet; the menu music plays.

Next: the rest of the results (what a stage opened, alternation), the service area's car
damage, the rest of the front end's pages, then the rest of the simulation and the network game.

| Area | State |
|---|---|
| File layer (folders, ISO, BFL, InstallShield cabinet) | Ported and tested against the whole disc |
| Textures, models, sound, music, text, fonts | Decoders ported and tested against the whole disc |
| Stage data (`.tre`, `.hpc`, `.cat`, ...) | `.tre`, `.hpc`, `.csp`, `.hor`, `.cat`, `fin.dat` ported; `.srf`, `.tsc` open |
| Renderer (Direct3D 7 to GLES 3) | Device, state layer, textures, meshes, 2D and text ported; reflections and 16-bit formats open |
| World (objects, stage sections) | Object trees, terrain, scenery, sky, shadows, lights, the glows and camera-facing sprites, the start lights, the cars' lamps and lenses, the particle engine, its types and the wheels' grass, leaves, dust, snow, splashes and stones, the rain and snow, the device lights (the sun, the headlights), the skid marks, the cars' shadows, the super special's fireworks, the sky's colours, fog, sun and sun glow by the time of day and the frame function ported; the other particle emitters and the cars' own drawing open |
| Race flow | Process framework, race states, Race_Update, Stage_Load, Car_Load, sectors and the race clock ported for one player in a rally, and the arcade's races (its stages, the computer drivers' cars on a grid, driven by their neural networks from the stage's `.ai` files); the chase, interior, cockpit and trackside cameras, the HUD's clock, split bar, split difference, standings, map, records, messages and gauges, the pace-note arrows, the race intro, the pause menu, the results and the replay (recorded from the go, played again in state 10; the disc's demo runs play, but the car does not yet keep to their path to the finish), the two players' split screen (each view with its own camera, projection and HUD) and the computer drivers' far models; the head to head's CPU runs (the stage's recorded drives, each car on its side of the track, played from the go; they drift from their path as the demo runs do), and won by touching the other car ahead of it; the ghost's and the network game's replays open |
| Front end | The process, menu engine, every page's construction, the single-stage, championship and arcade paths' pages up to the race, the options, high scores, driver profiles and the championship's saves (save in the service area, load from the menus) ported; mode 2's service area ported but for the car's body damage on its stand; the controllers' set-up (devices, key, button and axis bindings, analogue set-up; game controllers through gasm's gamepads) ported; the network pages and the arcade's standings open |
| Vehicle simulation | About 65 of 120 functions ported; a car drives on a stage, is held by its walls and stopped by the scenery (trees, posts, walls) and knocks the movable objects (signs, posts) over, rolls and is recovered; hits dent the car, wear its handling and break its glass, and are carried to the next stage; a broken engine misfires; the car-to-car contacts are ported; the loose parts' four kinds of motion and the whip aerials are ported; the step's other entities open |
| Sound | A DirectSound emulation and mixer, the sound layer, the engine loops, backfires, idle and gearbox whine, the surface sounds, the co-driver's calls and the collision sounds (hits, rolls, glass) ported; the menus' sounds and music, and the replay's Doppler shift ported |
| Movies (Bink 1) | Not started |
| Network (DirectPlay 4) | Located; last step |
