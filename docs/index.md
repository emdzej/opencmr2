---
layout: home

hero:
  name: OpenCMR2
  text: Colin McRae Rally 2.0, reimplemented
  tagline: "A faithful reimplementation of Colin McRae Rally 2.0 (Codemasters, 2000) in C11, ported function by function from the Windows game. It runs the game from your own CD, as one WebAssembly module for the gasm runtime, natively and in the browser. Early in the port: nothing is playable yet."
  actions:
    - theme: brand
      text: Get started
      link: /guide/
    - theme: alt
      text: Port plan
      link: /re/port-plan
    - theme: alt
      text: Status
      link: /guide/status

features:
  - title: Faithful
    details: Not an emulator and not a remake. The goal is the original's behaviour, its 16.16 fixed-point handling model, co-driver, AI and quirks included, ported from CMR2.exe.
  - title: Your own data
    details: Reads your CD or its image. Even the tables inside the original program are read from your copy at run time. Nothing from the game is in OpenCMR2.
    link: /guide/game-data
    linkText: Game data
  - title: Runs on gasm
    details: One opencmr2.wasm for macOS, Linux, Windows and the browser on the gasm WebAssembly game runtime. The original's Direct3D 7 rendering goes to OpenGL ES 3.0 (WebGL 2 in the browser).
    link: /guide/gasm
    linkText: Running on gasm
  - title: Reverse engineered in the open
    details: The architecture of the original program, its function map and the formats of every file on the disc are published with the code.
    link: /re/
    linkText: Reverse-engineering notes
  - title: Every format, documented
    details: The gzip/CMPR containers and archives, PP_F models, stages, cars, DXT5 textures, MS ADPCM music, Bink movies, text and fonts.
    link: /formats/
    linkText: File formats
  - title: A plan, step by step
    details: Eleven steps from the file layer to network play, each ending with a check that runs headless against your data.
    link: /re/port-plan
    linkText: Port plan
---

<div class="vp-doc" style="max-width: 1152px; margin: 48px auto 0; padding: 0 24px;">

## Built for gasm

<p><a class="gasm-badge" href="https://gasm.emdzej.pl"><img class="gasm-badge-light" src="https://gasm.emdzej.pl/badge/built-for-gasm-light.svg" alt="Built for gasm" width="120" height="44"><img class="gasm-badge-dark" src="https://gasm.emdzej.pl/badge/built-for-gasm-dark.svg" alt="Built for gasm" width="120" height="44"></a></p>

OpenCMR2 will be one WebAssembly module, `opencmr2.wasm`, running on [gasm](https://gasm.emdzej.pl), a
portable game runtime: the same module natively (`gasm-run`) and in the browser. See
[Running on gasm](/guide/gasm).

## Status

The disc and the program are analysed and the plan is written. Steps 1 and 2 are done: the file layer runs
on gasm from the CD or its image, the installer cabinet included, and every texture, sound, font, text
table and model on the disc decodes. Details in [Status](/guide/status).

## Sibling projects

OpenCMR2 follows the conventions of [OpenRF](https://openrf.emdzej.pl) (Return Fire) and
[OpenNFSPU](https://opennfspu.emdzej.pl) (Need for Speed: Porsche Unleashed): a portable C11 core, ported
from the original executable, on gasm.

</div>
