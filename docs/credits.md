# Credits and licences

**OpenCMR2** is released under the [GNU General Public License v3.0](https://github.com/emdzej/opencmr2/blob/main/LICENSE).
It contains no code, art, sound or data from the original game: everything is read from your own copy at run
time.

**Colin McRae Rally 2.0** is © 2000 The Codemasters Software Company Limited. OpenCMR2 is not affiliated with them.

## Published and community sources

- **Published Bink 1 format descriptions** — the basis for the movie decoder; its codec tables are read at
  run time from the `binkw32.dll` on your own disc.
- **Community modding notes for the game** — allowed as hints; the exe is the authority and no code is copied.
- **RFC 1951 / 1952** — the project's own inflate, which replaces the zlib 1.1.3 linked into `CMR2.exe`.

## Reference implementations

- [FFmpeg](https://ffmpeg.org) — every Bink movie is checked bit-exact against its decoder.
- Python's `zlib` — checks the project's inflate over the whole disc.

## Tools

[Ghidra](https://ghidra-sre.org), [ghidra-cli](https://github.com/akiselev/ghidra-cli), the [Unicorn](https://www.unicorn-engine.org) emulator (the differential harness that runs `CMR2.exe`'s own code), [wasi-sdk](https://github.com/WebAssembly/wasi-sdk), [VitePress](https://vitepress.dev); [ANGLE](https://chromium.googlesource.com/angle/angle) and SwiftShader, shipped in the desktop bundles with `gasm-run` (their notices are in `ANGLE-NOTICES.txt`).

## Runtime

[gasm](https://gasm.emdzej.pl) (MIT): `gasm-run`, the C SDK, and on this site `@emdzej/gasm-host`. OpenCMR2 is part of
[ENCORE](https://encore.emdzej.pl), with its sibling reimplementations.

If we have missed someone whose work this project relied on, please
[open an issue](https://github.com/emdzej/opencmr2/issues) — gaps in these credits are bugs.
