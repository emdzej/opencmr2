# Build from source

OpenCMR2 builds into one module, `opencmr2.wasm`, with the [gasm](https://gasm.emdzej.pl) C SDK. You need
CMake 3.24 or newer and a shell (macOS or Linux; on Windows, WSL).

## Build and play

From a clone of [the repository](https://github.com/emdzej/opencmr2):

```sh
./play /path/to/CMR2.ISO
```

`./play` fetches the gasm SDK and runner into `.deps/` the first time, builds `build-gasm/opencmr2.wasm` and
plays it in a window. It also takes a folder with the CD's files. Options after the disc go to `gasm-run`
(`--fullscreen`, `--window 1280x960`).

By hand:

```sh
tools/fetch-gasm-sdk.sh
cmake -S . -B build-gasm -DOPENCMR2_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
cmake --build build-gasm
gasm-run build-gasm/opencmr2.wasm --asset cd=/path/to/CMR2.ISO
```

## A bundle

The release bundles (the module with the runner and a launcher) come from `tools/package-gasm.sh`;
`tools/fetch-gasm-runner.sh` fetches the released runner for a platform:

```sh
tools/package-gasm.sh 0.1.1 macos-universal "$(tools/fetch-gasm-runner.sh macos-universal)" dist
tools/smoke-gasm-bundle.sh dist/opencmr2-gasm-0.1.1-macos-universal.zip   # OPENCMR2_SMOKE_DATA=CMR2.ISO also plays it
```

The platforms are `macos-universal`, `linux-x86_64`, `linux-arm64` and `windows-x86_64`.

## Tests

The native build runs the headless tests against your disc (`OPENCMR2_DATA=<image or folder>`, default
`./cd`; without it they are skipped):

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```
