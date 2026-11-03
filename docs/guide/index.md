# Introduction

OpenCMR2 is a reimplementation of **Colin McRae Rally 2.0** (Codemasters, 2000), the Windows version, in
C11. It is ported function by function from the original program, `CMR2.exe` version 1.11, and keeps its
behaviour: the handling model in the original's 16.16 fixed-point arithmetic, the co-driver, the AI, the
game modes and their quirks.

It is not an emulator and not a remake. It contains no code or data from the original: it runs the game
from your own CD. Even the tables inside `CMR2.exe` are read from your copy at run time.

OpenCMR2 is one WebAssembly module for the [gasm](https://gasm.emdzej.pl) game runtime. gasm runs it
natively on macOS, Linux and Windows, and in the browser. The original's Direct3D 7 rendering goes to
OpenGL ES 3.0 (WebGL 2 in the browser).

- [Status](status.md): what works today (every offline mode; the network game is next).
- [Game data](game-data.md): what you need from your copy of the game.
- [Playing in the browser](playing.md): the player on this site, and the keys.
- [Running on gasm](gasm.md): how OpenCMR2 is packaged and run.

To build it yourself: [Build from source](../howto/build.md).

## Sibling projects

OpenCMR2 follows the conventions of [OpenRF](https://openrf.emdzej.pl) (Return Fire, complete) and
[OpenNFSPU](https://opennfspu.emdzej.pl) (Need for Speed: Porsche Unleashed, in progress).
