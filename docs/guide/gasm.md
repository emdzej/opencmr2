# Running on gasm

OpenCMR2 is built as one module, `opencmr2.wasm`, for [gasm](https://gasm.emdzej.pl), a portable game
runtime: native runners (`gasm-run`) for macOS, Linux and Windows, and a browser runner. The same module
runs everywhere and produces the same frames.

<p><a class="gasm-badge" href="https://gasm.emdzej.pl"><img class="gasm-badge-light" src="https://gasm.emdzej.pl/badge/built-for-gasm-light.svg" alt="Built for gasm" width="120" height="44"><img class="gasm-badge-dark" src="https://gasm.emdzej.pl/badge/built-for-gasm-dark.svg" alt="Built for gasm" width="120" height="44"></a></p>

What OpenCMR2 uses from gasm (0.16.0):

| gasm | For |
|---|---|
| `gasm` core | Frame loop at 60 Hz, audio push (44,100 Hz stereo), raw keyboard, gamepads, assets, launch parameters |
| `gasm:gl` | OpenGL ES 3.0 (WebGL 2 in browsers): the Direct3D 7 renderer of the original, emulated |
| `gasm:storage` | Options, controller bindings, saves, high scores |
| `gasm:net` | Network play (in progress) |

Data is given as an asset: `--asset cd=CMR2.ISO` for the disc image, or `--asset-dir <folder>` for the CD
copied to a folder.

```sh
gasm-run opencmr2.wasm --asset cd=CMR2.ISO
gasm-run opencmr2.wasm --asset-dir cd --headless 600 --screenshot /tmp/frame.png   # unattended test run
```

In the browser: the [player](/play/){target="_self"} ([Playing in the browser](playing.md)). Releases
come as ready bundles with the runner: a macOS app (universal), Linux folders (x86_64, arm64) with
`opencmr2.sh`, and a Windows folder with `OpenCMR2.cmd`. Each launcher asks for the CD image or a folder
with the CD files once and remembers it (`--change-data` picks another, `--help` lists the options);
anything after the data goes to gasm-run. They will be on the
[releases page](https://github.com/emdzej/opencmr2/releases); none is published yet. Until then, build the
module or a bundle yourself: [Build from source](../howto/build.md).
