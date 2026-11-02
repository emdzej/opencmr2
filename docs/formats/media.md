# Textures, sound, music, movies, text

## Textures

| Format | Where | Notes |
|---|---|---|
| TGA, image type 2 (uncompressed true colour), 24 or 32 bpp | Non-`C` archives, car sets `c`-`f`, loose `FrontEnd/exittexture.tga` | Standard Truevision header (18 bytes), no colour map, no RLE seen. Fonts are 256x128 32-bit atlases |
| DDS, `DXT5` | `C` archives, car sets `a`, all track textures, `Game/Bigfiles/common.bfl` effects | Standard DirectDraw surface header (`DDS `, 124-byte `DDSURFACEDESC2`), FourCC `DXT5`. The header sets `DDSD_MIPMAPCOUNT` but the count is 1 in every file checked: no mip chain on disc |

No other texture format appears. `Tex_Reload` 0x4a5da0 ("couldn't find texture: %s") finds the member
([bfl.md](bfl.md#texture-lookup)) and picks the loader by the `DDS ` magic:

- `Tex_LoadTga` 0x4a6710 validates with `Tga_Header` 0x4a6670: colour-map type 0, image type 2, x and y
  origin 0, descriptor bits 4-6 clear (so rows bottom-up, left to right: a top-down TGA is refused), 24
  bits, or 32 bits with 8 alpha bits. The colour-map fields are not looked at. The pixels follow the
  18-byte header and the image ID. `Tga_Pixel` 0x4a60d0 reads pixel (x, y) from row `height-1-y` as
  B, G, R(, A) and optionally applies a brightness offset and a colour matrix (the texture-adjust
  options); the loader converts it into the surface's pixel format (masks and shifts from the surface
  description), 16 or 32 bits depending on the mode ("Set windows desktop to 16 bit" warns in window
  mode).
- `Tex_LoadDds` 0x4a6e30 passes the 124-byte surface description after the magic to `CreateSurface` as
  it is and copies the data from 0x80 into the surface; FourCC `DXT1` and `DXT5` are recognised. A width
  or height that is not a power of two from 2 to 2048 only makes it note the name.
- Name suffixes and prefixes (`DIGIT`, `REVCT` and two-letter tags) set texture flags for the renderer.

All 7,804 DDS and 4,003 TGA members of the disc pass these checks. The game also has a texture-adjust dialog ("Alter texture characteristics": brightness, contrast,
saturation) in its debug resources.

Port (`src/image.c`): both decoded on the CPU into RGBA8, rows top first (deterministic, works on every
GLES 3 / WebGL 2 device); the surface format and the adjust options are the renderer's. DXT colours are
the end points widened by bit replication and the 1/3, 2/3 mixes truncated; DXT5 alpha the 6- or
4-value ramp. An S3TC upload path can come later as an option.

## Sound effects and speech

RIFF WAVE, format tag 1 (PCM), mono, 16-bit:

| Set | Rate | Count |
|---|---|---|
| Speech (pace notes, `CountrySpecific`) | 11,025 Hz | 295 |
| Front-end and menu sounds | 22,050 Hz | 25 |
| In-game sounds (`Game/Bigfiles/common.bfl`) | 11,025 / 22,050 / 44,100 Hz | 44 |
| Surface sounds (`Sounds/surface.bfl`) | 22,050 Hz | |
| Engine sets (`Sounds/Car`) | 22,050 / 32,000 Hz | 240 |

`Snd_Load` 0x4a1f50 checks `WAVE` at +8 and then reads the canonical 44-byte header at fixed offsets
(channels +0x16, rate +0x18, bits +0x22, data size +0x28, data at +0x2c) without walking the chunks;
every WAV on the disc has that header. It never looks at the member's size: the five `cut.wav` members
of the speech archives are empty and sit at the offset of `ditch.wav`, so they would play "ditch", but no
pace note asks for `cut` (the exe's speech list has no CUT). Loaded sounds are counted up to 200
(`Snd_LoadNamed` 0x4b76c0).

The original plays them through DirectSound with optional DirectSound3D (the options list "DSound3D
Hardware", "Software - HRTF Full", "HRTF Light", "Pan and Volume"). Engine sounds are pitched loops.

## Music (.adp)

`Sounds/Music/arcade1..8.adp` and `select1.adp`, `select3.adp`: RIFF WAVE with format tag 2 (Microsoft
ADPCM), stereo, 44,100 Hz, 4 bits per sample, block align 2,048, the standard 7 coefficient pairs in the
`fmt ` extension, and a `fact` chunk. 8-14 MB each, not compressed further. `Music_Open` 0x4a28d0 opens
the file, converts it with the ACM (`Msadp32.acm`) into a DirectSound streaming buffer and `Music_Update`
0x4a2bd0 refills it. Port: a small MS ADPCM decoder streaming through the file layer.

## Movies (.bik)

`FrontEnd/Videos/cm.bik` (5 MB, the Codemasters logo, 549 frames at 25 fps), `intro.bik` (21 MB, 2,760
frames at 30 fps) and one movie a rally in `CountrySpecific/<region>/` (`FIN`, `GRE`, `FRA`, `SWE`,
`AUS`, `KEN`, `ITA`, `UK`; 660 frames at 30 fps with five sound tracks, one a language). All are Bink 1,
revision `i`, 640 x 480, played through `binkw32.dll` (`BinkOpen`, `BinkDoFrame`, `BinkCopyToBuffer`,
`BinkBufferBlit`, `BinkWait`, `BinkOpenDirectSound`).

The container: a 44-byte header (`BIKi`, the file's size less 8, the frame count, the largest frame, the
width and height, the frame rate as a fraction, the video flags, the number of sound tracks); per track
its largest decoded size, its rate and flags (0x2000 stereo, 0x1000 the older DCT coding) and its id;
then the frames' offsets (one more than the frames, bit 0 marking the key frames). A frame holds each
track's packet (a 32-bit size, then the data) and the video after them.

The video is coded in planes (Y, then the two chroma planes at half size; in this revision the second
plane is V), each starting on a 32-bit boundary after a 32-bit word at the frame's start. A plane is
read in rows of 8 x 8 blocks of ten kinds: skip (the last frame's block), a 16 x 16 block scaled from an
8 x 8 one, motion (a block of the last frame moved), runs of colours along one of sixteen patterns,
motion with a residue, intra and inter DCT blocks, a fill, a two-colour pattern and raw pixels. Their
values come from nine bundles (block kinds, sub-kinds, colours, patterns, the motion's x and y, the
intra and inter DCs, run lengths), each refilled a row at a time from Huffman codes: sixteen fixed code
sets, the symbols' order given per bundle in the stream. The DCT coefficients are sent by bit planes,
then dequantised by one of sixteen matrices and put through an 8 x 8 integer inverse DCT. The audio is
coded in blocks: two floats, a quantiser per critical band, then the coefficients in runs of one bit
width each; an inverse real DFT gives the samples (stereo interleaved before the transform), each block
overlapping the last by a sixteenth.

Port: `src/bink.c`, written from the format, with the codec's tables (the code sets, the patterns, the
coefficient order, the quantisers, the bands and run lengths) read from the user's `binkw32.dll` at run
time. `tests/bink_test.c` checks every frame of `cm.bik` against a reference decoder
(`tools/bink-ref.sh`); the other movies and every sound track were compared the same way. The player
is `src/fe/movie.c`; select ends a movie, as in the original.

## Text

`<language>text.bfl` holds three files, one string per CRLF line, Latin-1, no header:

| File | Used by | Strings |
|---|---|---|
| `f<language>.txt` | Front end (`FUN_004f4b90`) | 533 (0x215) |
| `s<language>.txt` | Stage, service and results screens (`FUN_0050f240`) | 326 (0x146) |
| `g<language>.txt` | In-game HUD and messages (`FUN_00478a20`) | 260 (0x104) |

There is one string table at a time. `Text_Build` 0x4a3c90 takes N files (the front end loads one per
language it offers) of a fixed number of strings, cutting them in place with `Text_Next` 0x4a3d50: a
string ends at the first byte below 0x20 (which becomes NUL) and the next starts at the next byte of 0x20
or more, so **empty lines are skipped**, not counted. There is no end test: a file with fewer strings
would run on into whatever follows it in memory. `Text_Select` 0x4a3c30 picks the file, `Text_Get`
0x4a3c60 returns string i of it or `INVALID TEXT STRING` out of range, `Text_Free` 0x4a3d80 drops the
table. Port: `src/text.c` (on copies of the files).

Languages on this disc (`CountrySpecific/Europe`): English, French, German, Italian, Spanish. The exe
also knows EngUSA, FrenchUSA, Polish (other releases). `Text_ToFontCase` 0x4a9f90 upper-cases a string and
maps a few Latin-1 symbols (`£`, `©`, `®`, `±`, `½`, `¼`, `¾`, `×`, `ó`) onto the font's glyph slots.

`CMR2.0 help.chm` and `ReadMe.txt` (installed from `data1.cab`) are documentation, not used by the game;
`Game/Info/Osdinfo.dat` (14,868 bytes, from `data1.cab`) is not named in the exe's strings; whether and how it
is used is open.

## Fonts (.pcf + .tga)

Each font is a pair `<name>.pcf` + `<name>.tga` (or `.dds` in the `C` archives) in the same archive
(`hel_12pt`, `hel_13pt`, `hel_15pt`, `hel_20pt`, `hel_36pt`, `lcd_14pt`, `lcd_640`, `ocr_12pt`,
`ocr_60pt`, `dot`, `debug`, `handel`), found by `Font_Load` 0x40b4f0 (`"%s\%s.pcf"`) and installed into
a font slot (0x18 bytes each, table at `DAT_0053223c`) by `Font_Set` 0x40b350.

| Offset | Type | Field |
|---|---|---|
| 0x00 | u32 | 3 |
| 0x04 | u32 | glyph count (`hel_12pt`: 174) |
| 0x08 | u32 | line height |
| 0x0c | u32 | space width |
| 0x10 | u32 | tracking, added after every glyph |
| 0x14 | u32 | 0 |
| 0x18 | u16[256] | glyph index per character, `0xffff` none |
| 0x218 | 16 bytes each | glyphs: u16 x, y, width, height in the atlas; u8 character; u8 kerning pair count; u16 unused; u32 file offset of the kerning pairs (relocated by `Font_Set`) |
| after | 4 bytes each | kerning pairs: u8 next character, u8 unused, i16 adjustment, ascending |

`Font_TextWidth` 0x40b5b0 measures a string: a space adds the space width; a glyph adds its width and the
tracking, or the pair's adjustment plus the tracking when the next character has a glyph and a pair (the
pairs are searched up to the first one not below the next character); characters without a glyph add
nothing. It stops at the first `\n` or `^`, so it returns the first line's width, in 16-bit arithmetic.

`Font_Print` 0x40b880 lays a string out. Flags 4 and 2 right-align and centre each line; 0x10 and 0x20
raise the baseline by the first glyph's byte +0x0a and by the height. A space adds the space width, and
`\n` or `^` starts a line one height plus the line gap (+0x14) lower. A glyph adds its width, the
tracking and its kerning pair. `Font_TextHeight` 0x40b730 is the line count times the height plus one,
minus one, plus the gap. The fonts live in slots (`Font_InitSlots` 0x40b220; `Font_LoadSlot` 0x40b430
finds `<name>.tga` as a texture and `<name>.pcf` beside it).

One record is garbage: in `Game/Bigfiles/com1024.bfl` and `com1024C.bfl`, `hel_20pt` maps `Í` (0xcd) to
a glyph that is all `0xcd` bytes (uninitialised memory in the font tool), kerning offset included; the
original would read far past the file when measuring `Í` before another glyph. The port gives that glyph
no kerning (`src/font.c`). All 72 fonts' other glyphs lie inside their atlases.

## Front-end stage files (.dmd)

`FrontEnd/common.bfl` has one 324-byte `<stage>.dmd` per stage (`jap11.dmd`, `aus02.dmd`, ...;
`cha%.2d.dmd` for challenges), read by 0x4d5ca0 for the stage-select "diamond" screen. Layout open.
