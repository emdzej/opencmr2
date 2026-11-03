# Playing in the browser

The [player](/play/){target="_self"} runs `opencmr2.wasm` in your browser with gasm's web runtime, in a
Web Worker. You need:

- your Colin McRae Rally 2.0 CD as an `.iso` image, the CD's files in a folder, or an installed copy of the
  game (see [Game data](/guide/game-data));
- a recent Chrome, Edge or Firefox, or Safari 17 and later: WebAssembly, Web Workers, **WebGL 2** and
  **OffscreenCanvas** (the game draws with WebGL 2 inside the worker);
- about 610 MB of browser storage to import the data once, or none to play without importing.

Every offline mode plays: single stages, rallies and championships, the arcade, time trials, the
challenge and the two-player modes. The network game plays too, over a relay ([Network play](/guide/network)); it is still being tested
([Status](/guide/status)).

## Choosing the game

**Choose a disc image** and pick an `.iso` (or raw `.bin`) of the CD ([how to make
one](/howto/extract-cd)), or **Choose the CD's folder** and pick a folder with the CD's files (or an
installed game, or the folder above either). The page checks a folder for `FrontEnd`, `Game` and the
program: the installer cabinet (`data1.hdr`, `data1.cab`, `data2.cab`, which hold `CMR2.exe`) or an
installed `CMR2.exe` of version 1.11. It keeps only the folders the game reads (not the installer, the
manual, Acrobat Reader or DirectX). An image is checked for an ISO 9660 volume; the game itself checks
what is inside. Then:

- **Import and play** copies the files the game reads into the site's private browser storage (OPFS). Later
  visits start at once with **Play**. **Remove imported data** deletes the copy.
- **Play without importing** reads the files from the folder or image you picked. You pick it again next time.

Nothing is uploaded: the files are read in your browser. Options and saves are kept in the browser's
storage for this site (IndexedDB).

The desktop version keeps the options, the controller set-up, the driver profiles and the replays as one
file each in `<data folder>/gasm/opencmr2` (`~/Library/Application Support/gasm/opencmr2` on macOS).
**Export saves** on the [play page](/play/) gives a zip of the same files, and **Import saves** takes
such a zip (or the files themselves), so saves move between browsers and to or from the desktop
version. Importing replaces the saves with the same names and keeps the others.

## While playing

The game reads the keyboard with the original's default keys: the cursor keys and <kbd>Enter</kbd> in the
menus, <kbd>Esc</kbd> to go back, and to pause on a stage.

| Action | Player 1 | Player 2 |
|---|---|---|
| Steer | <kbd>&larr;</kbd> <kbd>&rarr;</kbd> | keypad <kbd>4</kbd> <kbd>6</kbd> |
| Accelerate, brake | <kbd>&uarr;</kbd> <kbd>&darr;</kbd> | keypad <kbd>8</kbd> <kbd>2</kbd> |
| Handbrake | <kbd>Space</kbd> | keypad <kbd>3</kbd> |
| Gear up, down (manual box) | <kbd>]</kbd> <kbd>[</kbd> | <kbd>Page Up</kbd> <kbd>Page Down</kbd> |
| Camera | <kbd>C</kbd> | keypad <kbd>9</kbd> |
| Look back | <kbd>R</kbd> | keypad <kbd>7</kbd> |

Game controllers work through the browser's gamepad support. The keys and controllers are set up in the
game's own options, as in the original. The game draws at the display mode chosen in Options,
Graphics, Advanced Graphics, Display Mode (640 x 480 to 2048 x 1536, 640 x 480 by default, as the
original). The picture is scaled to the size it is shown at: a larger mode is sharper, a smaller one
faster. From 1024 x 768 the game uses its high-resolution menus and dash. Use **Fullscreen** (or
double-click) for the largest picture. Holding <kbd>Esc</kbd> for a second stops the
game; in fullscreen the browser takes <kbd>Esc</kbd> to leave fullscreen first. The line under the game
shows the frames per second.

## For developers

`?hashframes=N` runs N frames on virtual time and prints the same hash line as
`gasm-run --headless N` (with `input=` for scripted input, gasm-run's `--input` syntax, and `size=WxH` for
the drawing size; 1280x720 by default, the size `gasm-run --headless` draws at, so the hashes match). Other
query parameters are passed to the game (`car=`, `stage=`). `tools/web-play-test.mjs` drives this page in
headless Chrome.

The page needs `opencmr2.wasm` and gasm's web runtime next to it, both git-ignored:

```sh
docs/scripts/copy-wasm.sh                          # build-gasm/opencmr2.wasm -> docs/public/play/
cd docs && pnpm install && scripts/vendor-web.sh   # @emdzej/gasm-host and csfs -> docs/public/play/vendor/
pnpm build && pnpm preview
```
