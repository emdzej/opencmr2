# Porting conventions

How code is written in OpenCMR2. The same conventions as [OpenRF](https://openrf.emdzej.pl) and
[OpenNFSPU](https://opennfspu.emdzej.pl).

## Function by function

- Each ported function names its original: `/* Bfl_Find 0x4a9c70 */` above the definition. Use the name
  from `tools/ghidra/names.tsv`; add it there first if it is new.
- Keep the original's structure: the same functions, in the same order of calls, with the same arithmetic.
  Split a function only when the original inlined something the port needs twice, and say so.
- Reproduce quirks and bugs. If something looks wrong, check the disassembly; if the original does it,
  keep it with a comment. Only deviations of the port are fixed.
- 16.16 fixed point stays 16.16: the same multiplies (`(int64_t)a * b >> 16`), the same rounding, the same
  overflow. Use `int32_t`/`uint32_t` where the original is 32-bit, and make wrap-around explicit.
- MSVC `rand()` is ported as the original's linear congruential generator (seeded where the original
  seeds it), so runs are reproducible.

## Data

- Everything comes from the user's data at run time: the disc files, and the tables inside `CMR2.exe`
  through `src/exe.c` (which checks the size, 1,228,800 bytes, and CRC-32 `44c022f4`). Generators may emit
  addresses, counts, types and loader code; never the tables.
- All file access goes through the file layer (`src/vfs.h`): no `fopen` in the core. Names are
  case-insensitive with `/` or `\`.
- In-place data with offsets (PP_F, stage tables) is read through accessors that resolve offsets against the
  loaded block, never by writing host pointers into it.

## Platform

- The core is portable C11 and includes no platform headers. `src/platform.h` is the contract a backend
  implements: video (GLES 3 through `src/render/`), audio push, input (raw keys as `DIK_*`, pads),
  storage, the clock, errors.
- `src/platform_gasm.c` is the only file that includes `gasm.h`; `src/render/d3d7.c` the only one that calls
  GL.
- The frame clock replaces `timeGetTime()`; nothing in the simulation reads wall-clock time.
- Never add a blocking loop: the original's process states run one step per frame and so does the port.

## Checks

- Native tests (`ctest`) run against the user's data and skip without it.
- Headless `gasm-run` prints `video_fnv32` / `audio_fnv32`: the folder and the disc image must give the same
  line, wasmtime and the Node runner too. A change that alters a hash must be explained by the change.
- Unattended runs are always `--headless`. Never use macOS `screencapture`.
