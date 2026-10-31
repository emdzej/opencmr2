# OpenCMR2

A faithful reimplementation of **Colin McRae Rally 2.0** (Codemasters, 2000), the Windows version, in C11,
ported function by function from the original program `CMR2.exe` (v1.11). It runs the game from your own
CD, as one WebAssembly module for the [gasm](https://gasm.emdzej.pl) game runtime: natively on macOS, Linux
and Windows, and in the browser.

[![Built for gasm](https://gasm.emdzej.pl/badge/built-for-gasm-flat.svg)](https://gasm.emdzej.pl)

**Status: step 1 of the port.** The disc and the program are analysed, the formats documented, the port
planned, and the file layer runs on gasm from the CD or its image. Nothing is playable yet.

Site: [opencmr2.emdzej.pl](https://opencmr2.emdzej.pl) (sources in `docs/`):

- [Status](docs/guide/status.md) and [game data](docs/guide/game-data.md)
- [Architecture of CMR2.exe](docs/re/architecture.md), [function inventory](docs/re/inventory.md),
  [port plan](docs/re/port-plan.md)
- [File formats](docs/formats/index.md)

## Data

OpenCMR2 contains no code or data from the game and never will. You need your own Colin McRae Rally 2.0
CD; see [extract the CD](docs/howto/extract-cd.md). `tools/bfl.py` lists and extracts the game's
containers.

## Sibling projects

[OpenRF](https://openrf.emdzej.pl) (Return Fire) and [OpenNFSPU](https://opennfspu.emdzej.pl) (Need for
Speed: Porsche Unleashed), with the same conventions.

## License

GPL-3.0 ([LICENSE](LICENSE)). Colin McRae Rally 2.0 is © 2000 The Codemasters Software Company Limited.
OpenCMR2 is not affiliated with Codemasters.
