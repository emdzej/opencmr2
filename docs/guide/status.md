# Status

**Step 3 of the port.** Nothing is playable yet. Done so far:

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

Next: the original's model and stage drawing on that device.

| Area | State |
|---|---|
| File layer (folders, ISO, BFL, InstallShield cabinet) | Ported and tested against the whole disc |
| Textures, models, sound, music, text, fonts | Decoders ported and tested against the whole disc |
| Stage data (`.tre`, `.hpc`, `.cat`, ...) | Loaders found; `.hpc` and `.tre` containers known, fields open |
| Renderer (Direct3D 7 to GLES 3) | Device emulation and state layer done; cars and stages draw in a viewer |
| Front end | Mapped (0x4d0000-0x500000); not started |
| Vehicle simulation | Located (0x428000-0x445000, 16.16 fixed point), step model known (25 Hz fixed); not started |
| Movies (Bink 1) | Not started |
| Network (DirectPlay 4) | Located; last step |
