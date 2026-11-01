# AGENTS.md

Guidance for coding agents working in this repository. Humans: see [README.md](README.md) and
[opencmr2.emdzej.pl](https://opencmr2.emdzej.pl).

## What this is

OpenCMR2 is a faithful, from-scratch reimplementation of **Colin McRae Rally 2.0** (Codemasters, 2000), the
Windows program `CMR2.exe` version 1.11 (1,228,800 bytes, CRC-32 `44c022f4`), in C11. It targets
[gasm](https://github.com/emdzej/gasm) **0.16.0** (`opencmr2.wasm`: native runners on macOS, Linux and
Windows, and the browser). A native build exists for tools and headless tests only. Sibling projects with
the same conventions: `../openrf` (Return Fire, complete), `../opennfspu` (Porsche Unleashed, in progress);
copy their proven pieces (file layer, gasm backend, bundles, play page) rather than reinventing them.

It is not an emulator or a wrapper: every subsystem is ported function by function from the original
executable. **Fidelity is the product.** Reproduce the original's behaviour, including its quirks and bugs.
If something looks wrong, check the disassembly; if the original does it, keep it and add a comment. Only
fix deviations *of the port* from the original.

**State:** steps 1-2 done; step 3 mostly (`src/render/`: D3D7 device + GLES 3 back end, state layer,
textures, meshes, 2D queues, text; docs/re/render.md); step 5 started (`src/world/`: object trees,
stage sections and terrain; docs/re/world.md); the simulation and race (`src/sim/`, `src/game/`) and the
front end (`src/fe/`, docs/re/frontend.md): with no parameter the game runs from its menus to a stage.
Viewers: `--param car=` / `--param stage=`. Read `docs/re/port-plan.md` (the order of work,
each step with its check), `docs/re/architecture.md` and `docs/formats/`.

Build and check: `cmake -S . -B build && cmake --build build && ctest --test-dir build` (native tests);
gasm: `tools/fetch-gasm-sdk.sh`, then the `build-gasm` commands in `CMakeLists.txt`, run with
`gasm-run build-gasm/opencmr2.wasm --headless 10 --asset cd=CMR2.ISO` (the runner is in
`.deps/gasm-runner-macos-universal/`; add `--param car=205c1 --screenshot out/car.png` for the viewer). `tools/bfl.py --summary cd` gives the
hash `bfl_test` must match (`OPENCMR2_BFL_SUMMARY`); `tools/cmr2fmt.py --summary cd` the three `fmt_test`
hashes (`OPENCMR2_FMT_IMAGES`, `_MODELS`, `_SOUNDS`; about a minute on all cores). `tools/cmr2fmt.py
png|wav|obj <file[:member]>` exports into `out/`.

## Hard rules

1. **Never commit original data or anything derived from it.** `cd/` (the extracted CD), `game/` (files
   from `data1.cab`), `cmr2.exe` (the Ghidra copy), `re/` (decompiler dumps), `out/` (decoded assets) are
   git-ignored: keep it that way. No `.iso`, `.bfl`, `.c3d`, `.adp`, `.bik`, extracted textures, models,
   sounds or movies.
2. **No data copied from the executable in the source tree.** Tables the game needs from `CMR2.exe` are
   loaded at run time (`src/exe.c`, checks size + CRC). Small algorithmic constants and instruction operands
   are fine; tables, geometry and strings are not.
3. **Cite the original.** Every ported function names its source: `/* Name 0x4xxxxx */`. Keep names
   consistent with the Ghidra project; `tools/ghidra/names.tsv` is the record.
4. **Don't paste decompiler output** into source or docs. Write the port and the docs in your own words;
   describe layouts, formulas and addresses.
5. Community knowledge (CMR2 modding tools and notes) may inform format understanding; the exe is the
   authority and no code is copied.

## Game data

The user's disc image: `/Volumes/dropbox/CMR2.ISO` (ISO 9660, 683,599,872 bytes; a network volume, slow: use the local copy `~/Downloads/CMR2.ISO` for tests). Development copies:
`./cd` (`bsdtar -xf CMR2.ISO -C cd`) and `./game` (files from the cabinet: `CMR2.exe`, `Game/DemoRuns`).
The exe exists only inside the InstallShield cabinet; `src/vfs_cab.c` layers the cabinet over the mounted
CD, so the port needs only the CD (`tools/iscab.py` is the reference reader). See `docs/howto/extract-cd.md`.
Tests and tools read `OPENCMR2_DATA=<dir|image>` (default `./cd`) and skip without data.

Every data file is gzip + `CMPR` + payload, and most payloads are archives with a trailing directory
(`docs/formats/bfl.md`). `tools/bfl.py <file> [outdir]` lists or extracts one.

## Reverse engineering

- Ghidra project `cmr2`, program `cmr2.exe` (a copy of `game/CMR2.exe`, base 0x400000), via ghidra-cli:
  `ghidra decompile 0xADDR --project cmr2 --program cmr2.exe`, plus `x-ref to`, `disasm`, `memory`.
- `tools/ghidra/apply-names.sh` applies `names.tsv` and regenerates `re/all.c` (all functions, for
  grepping), `re/meta.tsv` (size, callers, callees, strings) and `re/funcs.txt`. 3,583 functions.
- Record findings in the matching `docs/` page with exact layouts and addresses; mark inferred vs confirmed.
- Device calls: the D3D7 device is `*(PTR_DAT_00520b78 + 4)`; vtable offsets are listed in
  `docs/re/architecture.md#rendering`.

## Docs site

VitePress in `docs/` (pnpm: `cd docs && pnpm install && pnpm build`), published to
https://opencmr2.emdzej.pl by `.github/workflows/pages.yml` (CNAME in `docs/public/CNAME`). Theme
`docs/.vitepress/theme/cmr2.css` (the game's menus: sage, mint, white, red). **No emojis**, no
version menu. Don't overstate the state (`docs/guide/status.md`); don't host or link copies of the game. The
"Built for gasm" badge is gasm's official one, hotlinked from
`https://gasm.emdzej.pl/badge/built-for-gasm-{light,dark,flat}.svg` (rules: https://gasm.emdzej.pl/dev/badge;
don't recolor, stretch or crop). New pages go in the sidebar in `docs/.vitepress/config.ts`; dead links fail
the build.

The browser player is the static page `docs/public/play/` (copied from `../opennfspu`): `docs/scripts/copy-wasm.sh`
puts `opencmr2.wasm` there and `docs/scripts/vendor-web.sh` the pinned gasm/csfs runtime (both git-ignored; the
Pages workflow builds the module). `node tools/web-play-test.mjs [cd] [out] [image]` checks it in headless
Chrome against `gasm-run`.

## Unattended runs

Always `--headless` for gasm-run; never open a window, never use macOS `screencapture`.

## Commits

Imperative subject, body explaining why when non-obvious. Never commit `cd/`, `game/`, `cmr2.exe`, `re/`,
`out/`, `build*/`, `.deps/`, `docs/node_modules/`.
