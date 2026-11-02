OpenCMR2 needs your own copy of Colin McRae Rally 2.0: the PC CD, as an `.iso` image or a folder with its files
(`data1.hdr`, `data1.cab`, `Game`, `FrontEnd`). No game data is included. See the
[status](https://opencmr2.emdzej.pl/guide/status) for what works.

- **macOS**: unzip and open `Colin McRae Rally 2.0 (gasm).app` (ad hoc signed, not notarized: the first time, right-click, Open).
- **Linux** (x86_64, arm64): unpack and run `./opencmr2.sh` (`--install-desktop` adds a menu entry).
- **Windows** (x86_64): unzip and run `OpenCMR2.cmd`.
- **Any system**: `gasm-run opencmr2-<version>.wasm --asset cd=CMR2.ISO` with gasm 0.16.0 or newer.

The launchers ask for the game data once and remember it (`--change-data` picks another). Every file has a
`.sha256` next to it. Guide: https://opencmr2.emdzej.pl/guide/game-data

Or play in the browser: https://opencmr2.emdzej.pl/play/
