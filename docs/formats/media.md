# Textures, sound, music, movies, text

## Textures

| Format | Where | Notes |
|---|---|---|
| TGA, image type 2 (uncompressed true colour), 24 or 32 bpp | Non-`C` archives, car sets `c`-`f`, loose `FrontEnd/exittexture.tga` | Standard Truevision header (18 bytes), no colour map, no RLE seen. Fonts are 256x128 32-bit atlases |
| DDS, `DXT5` | `C` archives, car sets `a`, all track textures, `Game/Bigfiles/common.bfl` effects | Standard DirectDraw surface header (`DDS `, 124-byte `DDSURFACEDESC2`), FourCC `DXT5`. The header sets `DDSD_MIPMAPCOUNT` but the count is 1 in every file checked: no mip chain on disc |

No other texture format appears. The texture path (`FUN_004a5da0`, "couldn't find texture: %s") picks the
DDS or the TGA member ([bfl.md](bfl.md#texture-lookup)) and uploads into a DirectDraw texture surface
(`Lock`/`Unlock`, 16-bit or 32-bit depending on the mode; "Set windows desktop to 16 bit" warns in window
mode). The game also has a texture-adjust dialog ("Alter texture characteristics": brightness, contrast,
saturation) in its debug resources.

Port: decode DXT5 on the CPU into RGBA8 (deterministic, works on every GLES 3 / WebGL 2 device); an S3TC
upload path can come later as an option.

## Sound effects and speech

RIFF WAVE, format tag 1 (PCM), mono, 16-bit:

| Set | Rate | Count |
|---|---|---|
| Speech (pace notes, `CountrySpecific`) | 11,025 Hz | 295 |
| Front-end and menu sounds | 22,050 Hz | 25 |
| In-game sounds (`Game/Bigfiles/common.bfl`) | 11,025 / 22,050 / 44,100 Hz | 44 |
| Surface sounds (`Sounds/surface.bfl`) | 22,050 Hz | |
| Engine sets (`Sounds/Car`) | 22,050 / 32,000 Hz | 240 |

The original plays them through DirectSound with optional DirectSound3D (the options list "DSound3D
Hardware", "Software - HRTF Full", "HRTF Light", "Pan and Volume"). Engine sounds are pitched loops.

## Music (.adp)

`Sounds/Music/arcade1..8.adp` and `select1.adp`, `select3.adp`: RIFF WAVE with format tag 2 (Microsoft
ADPCM), stereo, 44,100 Hz, 4 bits per sample, block align 2,048, the standard 7 coefficient pairs in the
`fmt ` extension, and a `fact` chunk. 8-14 MB each, not compressed further. `Music_Open` 0x4a28d0 opens
the file, converts it with the ACM (`Msadp32.acm`) into a DirectSound streaming buffer and `Music_Update`
0x4a2bd0 refills it. Port: a small MS ADPCM decoder streaming through the file layer.

## Movies (.bik)

`FrontEnd/Videos/intro.bik` (21 MB) and `cm.bik` (5 MB, the Codemasters logo): Bink video, signature
`BIKi` (Bink 1, revision i), 640x480, played through `binkw32.dll` (`BinkOpen`, `BinkDoFrame`,
`BinkCopyToBuffer`, `BinkOpenDirectSound`). The port needs its own Bink 1 decoder (video DCT/RLE bundles
and the Bink audio DCT/RDFT codec), written from the published format descriptions. Until then movies are
skipped, which the original allows (any key).

## Text

`<language>text.bfl` holds three files, one string per CRLF line, Latin-1, no header; strings are
addressed by line number:

| File | Used by | Lines (English) |
|---|---|---|
| `f<language>.txt` | Front end (`FUN_004f4b90`) | 533 |
| `s<language>.txt` | Stage, service and results screens (`FUN_0050f240`) | 326 |
| `g<language>.txt` | In-game HUD and messages (`FUN_00478a20`) | 261 |

Languages on this disc (`CountrySpecific/Europe`): English, French, German, Italian, Spanish. The exe
also knows EngUSA, FrenchUSA, Polish (other releases). `Text_ToFontCase` 0x4a9f90 upper-cases a string and
maps a few Latin-1 symbols (`£`, `©`, `®`, `±`, `½`, `¼`, `¾`, `×`, `ó`) onto the font's glyph slots.

`CMR2.0 help.chm` and `ReadMe.txt` (installed from `data1.cab`) are documentation, not used by the game;
`Game/Info/Osdinfo.dat` (14,868 bytes, from `data1.cab`) is not named in the exe's strings; whether and how it
is used is open.

## Fonts (.pcf + .tga)

Each font is a pair `<name>.pcf` + `<name>.tga` in the same archive (`hel_12pt`, `hel_13pt`, `hel_15pt`,
`hel_20pt`, `hel_36pt`, `lcd_14pt`, `lcd_640`, `ocr_12pt`, `ocr_60pt`, `dot`, `debug`). The `.pcf` starts
with `u32` 3, a glyph count (0xae = 174 for `hel_12pt`), the height (15), `u32` 3, then a character map of
`u16` glyph indices with `0xffff` for characters the font lacks, then per-glyph metrics into the TGA atlas.
Layout of the metrics open.

## Front-end stage files (.dmd)

`FrontEnd/common.bfl` has one 324-byte `<stage>.dmd` per stage (`jap11.dmd`, `aus02.dmd`, ...;
`cha%.2d.dmd` for challenges), read by 0x4d5ca0 for the stage-select "diamond" screen. Layout open.
