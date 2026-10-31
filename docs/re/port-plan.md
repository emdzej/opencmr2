# Port plan

The order to follow from an empty repository to the whole game. Each step ends with something that can be
checked: a native test against the user's data, or a headless `gasm-run` hash or screenshot. Sizes come from
[inventory.md](inventory.md) (KB of original code is the better effort estimate than the function count).
The structure of the original is in [architecture.md](architecture.md), the data in
[the formats](../formats/index.md).

## Ground rules

The rules of OpenRF and OpenNFSPU apply unchanged:

- Port function by function from `CMR2.exe`, cite the address (`/* Race_Frame 0x41b060 */`), keep the
  original's quirks and bugs, and only fix deviations of the port.
- Load the original's tables from the exe at run time (`src/exe.c`, checked by size and CRC-32
  `44c022f4`); never copy tables, strings or geometry into the source.
- Never commit original data or anything derived from it; never paste decompiler output.

What changes in structure, and why it is small here:

- **No blocking loops to unwind.** The original already runs one `Game_Frame` per idle iteration of its
  message loop, and its modes are process groups with `{update, draw}` state tables
  ([architecture](architecture.md#modes-and-processes)). `gasm_frame` calls the ported `Game_Frame` once.
  Loading is synchronous inside a state update, which is fine on gasm (the runner waits for the frame).
- **Time from the frame counter.** `timeGetTime()` becomes a clock advanced by the frame period, so
  headless runs are deterministic. The simulation stays in 16.16 fixed point exactly as the original.
- **Pointers in data.** PP_F files and other in-place data hold 32-bit offsets the original turns into
  pointers. wasm32 has 32-bit pointers, so in-place relocation would work, but the native test build is
  64-bit: keep offsets and resolve them through accessors (or parse into structs) in one place.
- **DirectX to gasm.** Direct3D 7 becomes a D3D7-state emulation on GLES 3 (`gasm:gl`); DirectSound one
  mixer pushed per frame; DirectInput the gasm raw keyboard as `DIK_*` codes plus pads; registry and saves
  `gasm:storage` and launch parameters; DirectPlay `gasm:net` (last).

Target: gasm **0.16.0** (the newest release; `GASM_VERSION` in `tools/fetch-gasm-sdk.sh`, together with
`@emdzej/gasm-host` in `docs/package.json`), C11, CMake, wasi-sdk from the gasm C SDK.

## 0. Research and repository (done)

- Disc analysed, `CMR2.exe` v1.11 extracted from the installer cabinet, Ghidra project `cmr2`
  (3,583 functions), first names in `tools/ghidra/names.tsv`, dumps in `re/` (git-ignored).
- Containers understood and `tools/bfl.py` reads every file on the disc.
- These notes, and the site skeleton for opencmr2.emdzej.pl.

## 1. Skeleton, file layer, exe loader (new code + about 15 KB of original; done)

Copy the proven pieces from the sibling projects, then add what CMR2 needs:

1. CMake project with the two builds (native tests, `-DOPENCMR2_PLATFORM=gasm`), `src/platform.h`,
   `src/app.c`, `src/platform_gasm.c` (the only file including `gasm.h`; title "Colin McRae Rally 2.0",
   60 Hz, raw keyboard, pads, storage, audio push), `tools/fetch-gasm-sdk.sh`,
   `tools/fetch-gasm-runner.sh`.
2. `src/vfs.c` from OpenNFSPU: host directories and ISO 9660 images (the CMR2 disc has long ISO names, no
   Joliet needed but harmless), case-insensitive names, `/` or `\`.
3. **InstallShield cabinet reader** (`src/vfs_cab.c`): `CMR2.exe` and the demo runs exist only inside
   `data1.cab`. Reading the cabinet (the `data1.hdr` file table, zlib-compressed file data) lets the user
   give just the disc image or the CD folder, with no install step. Done: a layer over the mounted CD
   ([format](../formats/index.md#installshield-cabinet)), checked against `tools/iscab.py`.
4. Inflate: a small own inflate (RFC 1951) or a vendored, license-compatible one (zlib license). Test it
   against Python's `zlib` over every file of the disc.
5. `src/bfl.c`: gzip + `CMPR` + archive directory, `Bfl_Find` semantics (lower-case, basename, first
   match), the archive slots, `.dds`-then-`.tga` lookup. Port `File_Load` 0x4aa220, `Bfl_*` 0x4a9c30-0x4a9f50,
   `Bfl_MountCommon` 0x455080, `Bfl_MountStage` 0x455260.
6. `src/exe.c`: map `CMR2.exe` (from the data or the cabinet), check size and CRC, translate VAs to file
   offsets; the first tables read from it are the process transition tables, the surface names and the text
   of the cheat codes.

**Check:** `bfl_test` lists every archive on the disc and compares member names, sizes and CRCs with
`tools/bfl.py` (one summary hash); the same from the folder and the `.ISO`. gasm headless run of a test
screen gives the same hash from `--asset-dir` and `--asset cd=CMR2.ISO`.

## 2. Format decoders and viewers (about 20 KB; done)

- TGA (type 2), DXT5 to RGBA8 on the CPU, PCM WAV, MS ADPCM (`.adp`), text files, fonts (`.pcf` + atlas).
- PP_F reader: header, record arrays, texture slots and paths, vertices ([c3d.md](../formats/c3d.md));
  the relocation pass of `C3d_Fixup` 0x4b93c0 as offset resolution.
- Reference decoders in `tools/cmr2fmt.py` (stdlib), with exports to PNG and OBJ into `out/` (git-ignored)
  for looking at cars and stages outside the game.

**Check:** a native test decodes all 7,804 DDS and 4,003 TGA members and every model; the C and Python
decoders agree (hashes). A car and a stage exported to OBJ look right in a viewer.

**Done:** `src/image.c` (`Tga_Header`, `Tga_Pixel`'s fetch, DXT1/DXT5), `src/sound.c` (`Snd_Load`'s
header, the `.adp` chunks and MS ADPCM blocks), `src/text.c` (`Text_Build` and friends), `src/font.c`
(`Font_Set`, `Font_TextWidth`), `src/c3d.c` (`C3d_Load` with every relocated field checked);
`tools/cmr2fmt.py` (PNG, WAV, OBJ exports, summaries). `fmt_test` decodes all 7,804 DDS and 4,003 TGA
members, 1,211 models, 604 WAVs and the 10 music files, and loads all 72
fonts against their atlases; the three hashes (images `58c9384f`, models `f6bd27a1`, sounds `76f48a11`)
match the Python decoders, from the folder and the `.ISO`. The Peugeot 205 (`205c1.c3d`, parts placed
by their matrices) and the Australia stage 1 mesh render right from the OBJ export. The gasm build loads
a car with its textures, the front-end text and a font at start-up.

## 3. Direct3D 7 on GLES 3 (renderer: about 90 KB of original, plus new code; in progress)

The original's renderer is 0x4ab000-0x4bdf00 with the render-state code at 0x49d3f0-0x49e100. Port it as
it is, on top of a small D3D7 emulation (`src/render/d3d7.c`, the only GL user):

- Device state: render states (z, alpha blend/test, fog, cull, shading), the texture-stage states of two
  stages, transforms (world/view/projection), viewport, material, up to 8 lights. State changes feed a
  shader key; the shaders implement the fixed-function lighting and the two-stage combiner (GLSL ES 3.00,
  WebGL 2 rules).
- Vertex buffers in the PP_F vertex layout (`XYZ|NORMAL|DIFFUSE|SPECULAR|TEX2`), indexed lists, the
  transformed-vertex path for 2D (`DrawPrimitive` with `XYZRHW`).
- Textures from the decoders, the 640 and 1024 resource sets (start with 640x480, the original's default).
- D3DX and DirectDraw reduce to mode bookkeeping.

**Check:** viewers as launch parameters, as in OpenNFSPU: `--param car=f99` (a car turning on a stand) and
`--param stage=fra02` (a fly-through along the stage), headless screenshots and hashes. Without a way to
run the original's renderer for comparison, a reference capture can be taken once from the original running
under Wine and kept outside the repo.

**So far** ([render.md](render.md)): the device emulation (`src/render/d3d7.c`, state with Direct3D 7's
defaults, tested natively; `src/render/gl_dev.c`, the GLES 3 back end adapted from OpenNFSPU's THRASH
device with transform and lighting, fog and generated texture coordinates added), the original's
render-state layer (`Render_InitStates` and the cached setters, `src/render/rstate.c`), and the viewers:
`--param car=205c1` and `--param stage=fra02` render headless with their textures, through the ported
texture records (`C3d_Textures`, `Bfl_FindTexture`, `Tex_Bind`, `Tex_SetStageBlend`) and mesh drawing
(`VB_CreatePool`, `Mesh_AddToVB`, `Mesh_Batches`, `Mesh_Draw` and its variants, `Light_SetMode`). Next:
the reflection passes, fog and the camera (`Camera_Apply` 0x49e1f0), the 2D path, then the object tree
(step 5).

## 4. Front end (about 180 KB)

Mode 0 (`Game_Frame` 0x4d0780, state table 0x523bc8): menus, the options, car and stage select, the
driver name and cheat codes, saves (`gasm:storage`), credits, high scores. Needs the 2D path from step 3,
fonts, text (five languages, `--param language=`), menu sounds and the music stream.

Movies are skipped at this point (the original lets a key skip them): the Bink decoder is step 10.

**Check:** scripted input (`--input`) through the menus to a stage start, hashed; every language renders.

## 5. World mode, stage and car on screen (about 60 + 64 + 56 KB)

Mode 2 (`World_Frame` 0x4ff450) and the race set-up: stage loading (`Stage_Load*`, 0x41f560-0x420630,
0x455300, 0x456c10, 0x46f060, 0x472830), sky and clouds, horizons for the time of day and weather, the
weather effects, scenery objects, the car with its levels of detail and lights, the race cameras. The
stage-member layouts ([stage.md](../formats/stage.md)) are reverse engineered here as their readers are
ported.

**Check:** a parked car on the start line of each of the 94 stages, from each camera, in every weather:
headless screenshots, reproducible.

## 6. Vehicle simulation (about 116 KB, the core of the game)

0x428000-0x445000 plus the surface tables and the stage collision data (`.hpc`, `.tre`, `fin.dat` as they
turn out). Port it in the original's integer arithmetic, function by function, with no float in the
simulation path. Establish the timestep model first ([architecture](architecture.md#timing)).

**Check:**

- Unit tests of the fixed-point helpers and of single functions against values worked out from the
  disassembly.
- Determinism: the same scripted input gives the same per-frame state hash on wasmtime JIT, AOT and V8.
- Replays: the original's demo runs and CPU runs (`Game/DemoRuns/*.rpl`, `CPU_Runs`) play back through the
  ported replay code; once the replay frame format is known, a driven run can be compared against them.
- Optionally, reference traces: the original under Wine with a proxy `dinput.dll` feeding scripted input
  and logging the car state each frame. This is the strongest check of the physics; worth setting up when
  the simulation step starts.

## 7. In race: co-driver, HUD, sound, effects (about 18 + 29 + 60 + 56 KB)

Pace notes (`.cod`, the speech samples, `Codriver_*` 0x416710-0x418550), the OSD (arrows, timer, splits,
speedo, rev counter, damage), engine sounds per RPM, surface and collision sounds, skid marks, particles,
rain and snow, car lights, the in-car view.

**Check:** a scripted drive on one stage per rally, hashes of video and audio; listen to it once.

## 8. Game modes and the race flow (about 21 + 80 KB, plus parts of 4 and 5)

The race state machine (14 states), stage start and finish, results and standings, the service area and
repairs between stages. The rally modes: championship (8 rallies, 16 drivers, points 12-8-6-4-2-1), single
rally, single stage (weather choice), time trial, challenge (knockout on the Super Special Stages, Japan
first, head to head). The arcade modes: championship (classes A and B, three circuits, three laps) and
single race against up to five AI cars on the `Chall/` circuits (`ai0..2`, AI 0x47c000-0x490000), with the
two-player catch-up, lap-points and time-lag variants. Split screen (2) and alternate play (2-4; up to
four processes in `Race_Frame`), replay saving, unlocks, the championship end with fireworks.

**Check:** a complete scripted rally from the front end to the results; a two-player split-screen stage.

## 9. Release: bundles and the browser player

As OpenRF/OpenNFSPU: `opencmr2-<version>.wasm`, gasm bundles for macOS (universal), Linux (x86_64, arm64)
and Windows (with ANGLE for `gasm:gl`), launchers that remember the CD location, `tools/smoke-gasm-bundle.sh`,
and the play page `docs/public/play/` (the user picks the CD folder or `CMR2.ISO`, imported into OPFS with
csfs). Release workflow on plain semver tags.

This can start as soon as step 4 shows the front end, so testers can try each step in a browser.

## 10. Movies (new code)

A Bink 1 decoder (`BIKi`: the video bundles, DCT and RLE blocks, and the Bink audio DCT/RDFT codec),
written from the published format descriptions, for `intro.bik` and `cm.bik`.

**Check:** both movies decode to the end; frames hashed headless.

## 11. Network play

DirectPlay sessions become `gasm:net` messages through a relay (gasm-relay or swsrs). First read how the
original synchronises (`gnNetworkFrame`, 0x425a90; 0x4a0000-0x4a2000) and keep its model if it is
deterministic enough, else lockstep on the input like OpenRF's plan.

## Open questions

Answered (details in the linked pages):

- **Timestep:** fixed, 25 steps per second per car (40 ms), at most 5 per frame, with interpolation of the
  drawn poses ([architecture: timing](architecture.md#timing)). Step 1's clock only has to give the latched
  `timeGetTime()` values; the step count follows.
- **Replays:** input recordings, 4-byte run-length frames, one per step
  ([stages: replays](../formats/stage.md#replays-rpl-grp)). The demo runs are end-to-end physics tests.
- **Handling:** base parameters are immediates in `Car_Init` 0x43c7f0 (14 handling classes), the rest
  from the model and `.cin`; no handling table in `.data` ([cars: handling](../formats/cars.md#handling)).
- **`.hpc`:** collision grid and records, layout of the containers known
  ([stages: .hpc](../formats/stage.md#hpc)); **`.tre`:** renderer-side scenery tree per mesh section
  ([stages: .tre](../formats/stage.md#tre)).

Still open:

- The field meanings inside `.hpc`, `.tre`, `fin.dat`, `.cat`, `.tm0-2` (steps 5 and 6, as their readers
  are ported).
- Which `.cin` sections hold what, beyond section 0.
- Whether the `Hi`/`Lo` stage and `a`/`c` car sets are chosen by options only, or also by hardware caps
  (the port should offer the highest by default).
