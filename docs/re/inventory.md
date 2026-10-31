# Function inventory

First-pass map of `CMR2.exe` by address range. Ghidra's analysis plus `tools/ghidra/FillGaps.java` finds
3,583 functions, 1,023 KB of code. The ranges below are drawn around **anchors**: functions whose purpose
is certain from the strings, imports or data they use (listed per range). Boundaries between neighbouring
ranges are approximate until the functions in between are read; refine this page as names are added to
`tools/ghidra/names.tsv`.

"fn" is the number of functions, "KB" the code size. Size is the better effort estimate: the low ranges
are full of 2-5 instruction accessors, while the simulation is 120 functions of 1 KB on average.

| Range | fn | KB | Subsystem |
|---|---|---|---|
| 0x401000-0x40b000 | 348 | 37 | Game setup and state accessors (game mode, players, car, stage, options) |
| 0x40b000-0x40f000 | 151 | 15 | Config, controller and game-info files, paths |
| 0x40f000-0x416000 | 61 | 29 | Race draw states, HUD/OSD, debug overlay |
| 0x416000-0x41b000 | 49 | 18 | Co-driver pace notes, collision and surface sounds |
| 0x41b000-0x420800 | 80 | 21 | Race flow (race state machine), replays, stage loading |
| 0x420800-0x428000 | 110 | 30 | Not mapped yet (network frame handling at 0x425a90) |
| 0x428000-0x445000 | 120 | 116 | Vehicle simulation, 16.16 fixed point |
| 0x445000-0x455000 | 130 | 60 | In-car view, HUD timing, results tables |
| 0x455000-0x459000 | 103 | 14 | Archive mounts, car loading |
| 0x459000-0x467000 | 135 | 56 | Effects: particles, weather, car lights, skid marks |
| 0x467000-0x478000 | 187 | 64 | Sky and clouds, scenery objects, car LOD, HUD digits |
| 0x478000-0x47c000 | 39 | 15 | Text, fonts, menu sounds |
| 0x47c000-0x490000 | 131 | 80 | AI drivers, fireworks |
| 0x490000-0x49c000 | 91 | 46 | Track data (`fin.dat`, `.hpc`), misc |
| 0x49c000-0x4a0000 | 92 | 15 | Process framework, render-state setup, benchmark (`gamegauge`), input |
| 0x4a0000-0x4a3000 | 98 | 11 | DirectPlay session code, music streaming |
| 0x4a3000-0x4ab000 | 147 | 30 | Text strings, texture manager, window, `WinMain`, file layer, registry |
| 0x4ab000-0x4bdf00 | 312 | 73 | Renderer: device, vertex buffers, models (`C3d_*`), shadows, debug log |
| 0x4bdf00-0x4c4800 | 34 | 17 | zlib 1.1.3 (not ported: the port uses its own inflate or a vetted one) |
| 0x4c4800-0x4d0000 | 261 | 36 | D3DX 7 and DirectX import thunks (not ported: replaced by the GLES backend) |
| 0x4d0000-0x500000 | 697 | 178 | Front end: menus, car and stage select, options, saves, credits, network lobby |
| 0x500000-0x510000 | 185 | 60 | World mode: weather set-up, environment, stage map and forecast, service area, results, loading screens |
| 0x510000-0x520000 | 22 | 1 | CRT tail |

To port: about 3,250 functions and 950 KB of game code, without zlib, D3DX and the CRT.

## Anchors

| Range | Anchors |
|---|---|
| Setup | `Game_GetMode` 0x4057d0; config defaults 0x405830 ("Setting configuration to defaults"); AI driver names 0x407ec0 (`mcrae`, `erikson`, `dugas`, ...) |
| Config | `Controller.rcf` 0x40bf70 / 0x40bff0; `Paths_Init` 0x40e8d0; leaderboards 0x40e3f0 |
| Race draw | debug overlay 0x40f0c0, 0x410c80; challenge HUD 0x411280; timer text 0x415bd0; pace-note arrows 0x4165e0 |
| Co-driver | `Codriver_LoadCod` 0x416720; speech table 0x416770; collision sounds 0x4188c0; surface sounds 0x418ff0 |
| Race flow | `Race_Frame` 0x41b060; race states 0x41bf80-0x41f4e0 ([architecture](architecture.md#modes-and-processes)); surfaces 0x41b460; CPU runs 0x41c1b0-0x41c5a0; stage loaders 0x41f560-0x420630 |
| Simulation | wheel/tyre loops in 0x4387a0, 0x43b100, 0x43c7f0, 0x441500 (car state `DAT_0053cc18`, parameters `DAT_0053cc1c`) |
| In-car, HUD | rev counter and speedo textures 0x445a60; split and stage times 0x44bcd0 |
| Loading | `Bfl_MountCommon` 0x455080, `Bfl_MountStage` 0x455260, `Stage_LoadTm` 0x455300, `Stage_LoadCsp` 0x456c10, `Car_Load` 0x457000 |
| Effects | particle textures 0x459c80; rain/snow 0x45eb50; brake/head lights 0x463360, 0x463d60; skids 0x465600 |
| Sky, objects | clouds 0x46ef50; `Stage_LoadSkyGround` 0x46f060; `Stage_LoadObj` 0x472830, `Stage_LoadSht` 0x472870; car interior LOD 0x4760a0 |
| Text | in-game text 0x478a20; fonts 0x478b50 |
| AI | `Ai_LoadLines` 0x47c2f0, 0x48ca60; fireworks sounds 0x47e4d0 |
| Track data | `Stage_LoadFinDat` 0x490c30, `.hpc` reader 0x490d50 |
| Framework | `Proc_*` 0x49c150-0x49c370; render states 0x49d3f0, 0x49df90; `gamegauge` 0x49de40; `Input_InitDefaults` 0x49f0e0 |
| Net, music | DirectPlay calls 0x4a1c50; `Music_Open` 0x4a28d0, `Music_Update` 0x4a2bd0 |
| Platform | text lookup 0x4a3c60; textures 0x4a5da0; window 0x4a8140, 0x4a8450; `WinMain` 0x4a9720; file layer 0x4a9c30-0x4aa220; registry 0x4aa480, 0x4aa720; DirectPlay creation 0x4aaaf0 |
| Renderer | vertex buffers 0x4b1d00; shadows 0x4b4aa0 (`SHAD%d`); model load failure 0x4b76c0; `C3d_Load` 0x4b9380 |
| Front end | `Game_Frame` 0x4d0780; FE resources 0x4d08d0, 0x4d21e0, 0x4d2380; car select 0x4d2590; stage select 0x4d5ca0; options 0x4ea5e0; cheat codes 0x4eb290; saves 0x4f4ef0-0x4f5190; credits 0x4f4910 |
| World | `World_Frame` 0x4ff450; weather 0x500c00; environment maps 0x501520; stage maps and weather symbols 0x503ea0; service area (`TYRES`, `TURBO`, `BRAKES`, ...) 0x50a080; forecast 0x50c130; loading screens 0x50f4f0 |

## Data the port reads from the exe

Tables found so far that the port loads from `CMR2.exe` at run time (never copied into the source):

| Address | What |
|---|---|
| 0x523bc8, 0x526ee0, 0x5190b0 | Process state tables (code addresses: replaced by the port's own tables in the same order) and their transition tables 0x523c18, 0x526f18, 0x519120 |
| 0x519198 | Surface and sound-type name table |
| 0x5250fc | Cheat codes (20) |
| 0x516xxx | AI driver names, weather names |
| `.rdata` / `.data` | Car parameters, surface physics, camera tables: to be located in the simulation step |
