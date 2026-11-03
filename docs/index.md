---
layout: home

hero:
  name: OpenCMR2
  text: Colin McRae Rally 2.0, reimplemented
  image:
    src: /hero.svg
    alt: "opencmr2: colin mcrae rally 2.0 reimplemented, the letters running round a curve as in the game's main menu"
  tagline: "A faithful reimplementation of Colin McRae Rally 2.0 (Codemasters, 2000) in C11, ported function by function from the Windows game. It runs the game from your own CD, as one WebAssembly module for the gasm runtime, natively and in the browser. Every offline mode plays; the network game is being finished."
  actions:
    - theme: brand
      text: Play in the browser
      link: /play/
      target: _self
    - theme: alt
      text: Get started
      link: /guide/
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
  - title: Native and in the browser
    details: Play on this site in a recent Chrome, Edge, Firefox or Safari, or natively on macOS, Linux and Windows with gasm-run. Keyboard and game controllers.
    link: /guide/playing
    linkText: Playing
  - title: Every offline mode
    details: Rallies and championships with the service area, the arcade, time trials, the challenge and two players on a split screen.
    link: /guide/status
    linkText: Status
---

<div class="vp-doc" style="max-width: 1152px; margin: 48px auto 0; padding: 0 24px;">

## Screenshots

Renders of OpenCMR2 itself (headless runs of `opencmr2.wasm` on the game's CD at 1024 x 768, made by
`tools/screenshots.sh`).

<div class="shots">
  <figure><img src="/screenshots/menu.jpg" alt="The main menu" loading="lazy"><figcaption>The main menu</figcaption></figure>
  <figure><img src="/screenshots/service.jpg" alt="The service area's stage information in Finland" loading="lazy"><figcaption>The service area: the stage and its weather</figcaption></figure>
  <figure><img src="/screenshots/start-lights.jpg" alt="The start gantry's lights counting down in Finland" loading="lazy"><figcaption>The start lights, Finland</figcaption></figure>
  <figure><img src="/screenshots/stage-start.jpg" alt="Away from the start line" loading="lazy"><figcaption>Green: away from the line</figcaption></figure>
  <figure><img src="/screenshots/demo-1.jpg" alt="A demo run on gravel with the split times" loading="lazy"><figcaption>A demo run on gravel, with the splits</figcaption></figure>
  <figure><img src="/screenshots/demo-2.jpg" alt="A demo run on snow" loading="lazy"><figcaption>A demo run on snow</figcaption></figure>
  <figure><img src="/screenshots/demo-3.jpg" alt="A demo run on tarmac from a trackside camera" loading="lazy"><figcaption>A demo run on tarmac</figcaption></figure>
</div>

## Built for gasm

<p><a class="gasm-badge" href="https://gasm.emdzej.pl"><img class="gasm-badge-light" src="https://gasm.emdzej.pl/badge/built-for-gasm-light.svg" alt="Built for gasm" width="120" height="44"><img class="gasm-badge-dark" src="https://gasm.emdzej.pl/badge/built-for-gasm-dark.svg" alt="Built for gasm" width="120" height="44"></a></p>

OpenCMR2 is one WebAssembly module, `opencmr2.wasm`, running on [gasm](https://gasm.emdzej.pl), a
portable game runtime: the same module natively (`gasm-run`) and in the browser. See
[Running on gasm](/guide/gasm).

## Status

The game runs from your CD or its image, through its own menus, into every offline mode: single stages,
rallies and championships, the arcade, time trials, the challenge and two players on a split screen, with
the replays, the service area, the saves and the movies. The network game is being ported. Details in
[Status](/guide/status).

## Sibling projects

OpenCMR2 follows the conventions of [OpenRF](https://openrf.emdzej.pl) (Return Fire) and
[OpenNFSPU](https://opennfspu.emdzej.pl) (Need for Speed: Porsche Unleashed): a portable C11 core, ported
from the original executable, on gasm.

</div>
