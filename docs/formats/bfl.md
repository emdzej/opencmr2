# BFL containers

Almost every data file of the game (`.bfl`, but also the loose `.c3d`, `.cin`, `.rpl` and `.tga` files) uses
the same two wrapper layers, and most `.bfl` files are archives of named member files. Reference reader:
`tools/bfl.py` (list, or extract with an output directory).

## Layers

| Layer | Detection | Content |
|---|---|---|
| gzip | bytes `1f 8b` | RFC 1952 gzip, one member, deflate, no file name. The game reads every file through zlib 1.1.3's `gzopen`/`gzread`, which passes non-gzip files through unchanged, so the gzip layer is optional. |
| CMPR | ASCII `CMPR` | `u32` payload size (little endian), then exactly that many payload bytes. |
| payload | | One file, or an archive (below). |

`File_Load` 0x4aa220 opens the file with `gzopen`, reads 8 bytes, and if they are `CMPR` + size, allocates the
size and reads the payload. Otherwise it closes the gz stream and reads the file as it is with `ReadFile`.
On a failed open it shows the "Make sure the CMR2 CD is in the CD-ROM drive" dialog and retries.

Every file on the disc except `Sounds/surface.bfl` (a plain RIFF WAVE) and the `.adp` music has both layers.

## Archive

An archive payload is the member files back to back, followed by a directory and a final `u32`:

```
member data ...                    (each member starts 4-byte aligned in practice)
directory:
  repeat:
    u32 size
    u32 offset                     (from the start of the payload)
    u32 name_length                (without terminator)
    char name[name_length]         (padded with zeros to a multiple of 4)
u32 directory_offset               (last 4 bytes of the payload)
```

The directory runs from `directory_offset` to `payload_size - 4`. There is no entry count. Names are lower
case, without directories (`move.wav`, `t_1a.dds`, `fra02.tre`).

`Bfl_Find` 0x4a9c70 lower-cases the requested name and walks the directory linearly with a byte compare; the
first match wins and the size is returned through an out parameter. `Bfl_FindFile` 0x4a9df0 strips the
directory part of the requested path first (after the last `/` or `\`), so the game can ask for
`\NEWIMAGE\Exhaust\exhst02.tga` and get the member `exhst02.tga`.

### Texture lookup

`Bfl_FindTexture` 0x4a9e60 strips the extension and tries `<name>.dds`, then `<name>.tga`. Archives
therefore come in pairs: one with uncompressed TGA members and a `C` variant with DXT5 DDS members of the
same names (`res640.bfl` / `res640C.bfl`, `Com640.bfl` / `Com640C.bfl`, `ukDay1.bfl` / `ukDay1C.bfl`).
Which one is mounted depends on the texture-compression setting (`FUN_004a9700`, true when the device
reports DXT support and the option is on). Track textures (`<country>hit.bfl`, `<country>lot.bfl`) and the
car `a` sets exist **only** as DXT5, so the port needs a DXT decoder whatever the setting.

## Archive slots

Mounted archives live in fixed slots `{u8 *data, u32 size, u32 loaded}` (16 bytes) that the code reaches
through accessors:

| Accessor | Slot | Mounted from |
|---|---|---|
| `FUN_00455290` | 0x541f08 | `Game\BigFiles\Common.bfl` (sounds, effect textures) |
| `FUN_004552a0` | 0x541f18 | `Game\BigFiles\Com<640\|1024>[C].bfl` (HUD textures and fonts) |
| `FUN_004552b0` | 0x541f28 | `CountrySpecific\<region>\<language>speech.bfl` |
| `FUN_004552c0` | 0x541f38 | the stage archive `Game\Tracks\<country>\<stage><Hi\|Lo>.bfl` |
| `FUN_004552d0` | 0x541f48 | `Game\BigFiles\FireWork.bfl` (only when fireworks are enabled) |
| `FUN_004552e0` | | `Sounds\surface.bfl` |
| `FUN_004552f0` | | `CountrySpecific\<region>\<language>text.bfl` |

`Bfl_MountCommon` 0x455080 mounts the common set; `Bfl_MountStage` 0x455260 the stage. The resolution in
`Com<n>` is 1024 when the configured screen width (`FUN_00405c10`) is at least 1024 and two capability
checks pass (`FUN_004b7560(1024)`, `FUN_004b7590(1024)`; not read yet), else 640.

## What is where

| Path | Archive members |
|---|---|
| `FrontEnd/res640[C].bfl`, `res1024[C].bfl` | Front-end screens, car pictures (`f99b01.tga`, `f99f01.tga`), country flags and backgrounds, fonts (`hel_12pt.pcf` + `.tga`) |
| `FrontEnd/common.bfl` | Stage "diamond" files `<stage>.dmd` (324 bytes each), menu sounds |
| `FrontEnd/exittexture.tga` | Single file: the exit screen |
| `CountrySpecific/Europe/<language>text.bfl` | `f<language>.txt` (front end), `s<language>.txt` (stage screens), `g<language>.txt` (in game) |
| `CountrySpecific/Europe/<language>speech.bfl` | Co-driver pace-note samples (`100.wav`, `left.wav`, ...) |
| `CountrySpecific/Europe/FEres640E[C].bfl` | Localised front-end textures (copyright screens) |
| `Game/Bigfiles/common.bfl` | In-game sounds (collisions, rolls, landings, rain), effect textures (`rain.dds`, `splat.dds`, lights, skid marks) |
| `Game/Bigfiles/Com<res>[C].bfl` | HUD: rev counter, speedo, pace-note arrows, fonts (`hel_13pt`, `lcd_14pt`, `ocr_60pt`) |
| `Game/Bigfiles/FireWork.bfl` | Fireworks textures and sounds (championship end) |
| `Game/Cars/<car><set><n>.bfl` | Car textures for one model set ([cars.md](cars.md)) |
| `Game/Tracks/<country>/<stage>Hi.bfl`, `Lo.bfl` | One stage ([stage.md](stage.md)) |
| `Game/Tracks/<country>/<country>hit.bfl`, `lot.bfl` | Track textures for the country, high and low detail (DXT5 only) |
| `Game/Tracks/Clouds/pc/cloud_<light\|med\|heavy>.bfl` | Sky/cloud textures (`PCLow/` for low detail) |
| `SetupRep/<640\|1024>/<country>Day<n>[C].bfl` | Service-area and stage-map screens per rally day; `common[C].bfl` |
| `Sounds/Car/<engine>.bfl` | Engine sound sets (`205.bfl`, `focus.bfl`, ...) |
| `Sounds/surface.bfl` | Not an archive: one RIFF WAVE |

Member counts over the whole disc: 7,804 DDS, 4,003 TGA, 609 WAV (5 of them empty), 94 `.dmd`, 72 `.pcf`, 20 `.txt`, and
the stage member types (188 of each, two per stage).
