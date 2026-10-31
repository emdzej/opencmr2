# File formats

What the game reads from the disc and writes to the disk, as far as known. Each page gives the layout,
the loader in `CMR2.exe` and what is still open. The reference decoders are in `tools/` (stdlib Python):
`bfl.py` reads the containers. Community knowledge may inform the notes; the exe is the authority.

| Page | Formats |
|---|---|
| [BFL containers](bfl.md) | gzip + `CMPR` wrapper, `.bfl` archives, archive slots, texture lookup |
| [PP_F models](c3d.md) | `.c3d` (cars, stage mesh), `.sky`, `.gro`, `.obj`, `.sht` |
| [Stages](stage.md) | The 25 members of a stage archive, surfaces, replays (`.rpl`, `.grp`) |
| [Cars](cars.md) | Car codes, model and texture sets, `.cin`, engine sounds |
| [Textures, sound, music, movies, text](media.md) | TGA, DDS/DXT5, PCM WAV, MS ADPCM `.adp`, Bink `.bik`, text files, fonts (`.pcf`), `.dmd` |

## The disc

The CD (`CMR2.ISO`, ISO 9660, volume `CMR2`, 683,599,872 bytes) is an InstallShield setup ([cabinet format](#installshield-cabinet) below). The game data is
on the disc as files and is read from there or from a copy on the hard disk; the executables and a few small
files are only in the setup cabinets:

| Path | What |
|---|---|
| `data1.cab`, `data2.cab`, `data1.hdr`, `layout.bin`, `setup.*`, `ikernel.ex_` | InstallShield 6. The cabinet holds `CMR2.exe`, `CMR2Network.exe`, `binkw32.dll`, `Msadp32.acm`, `ReadMe.txt`, `CMR2.0 help.chm`, `Game/Bigfiles/*.bfl` (copies of the disc's), `Game/DemoRuns/*.rpl`, `Game/Info/Osdinfo.dat` |
| `Launcher.exe` | Autorun launcher (install / play), not part of the game |
| `dx7a*.exe` | DirectX 7a redistributables |
| `FrontEnd/`, `Game/`, `SetupRep/`, `Sounds/`, `CountrySpecific/` | Game data ([bfl.md](bfl.md#what-is-where)) |
| `Manual/CMR2-PC-UK.pdf`, `Acrobat Reader/` | Manual |

The program is **`CMR2.exe` version 1.11** (version resource "Colin McRae Rally 2.0", Codemasters 2001):
1,228,800 bytes, CRC-32 `44c022f4`, SHA-1 `299a29409e92242b4f66f50388a5866d46babf02`, PE32 i386, image base
0x400000, sections `.text` 0x1000 (1,111,798 bytes), `.rdata` 0x111000, `.data` 0x116000 (3.2 MB virtual,
80 KB initialised), `.rsrc` 0x432000. Not packed or protected. `CMR2Network.exe` is the same size but a
different build (network play); the port targets `CMR2.exe`.

## InstallShield cabinet

The installer's files are in one cabinet split into volumes: `data1.hdr` (the file table), `data1.cab` and
`data2.cab` (the data). OpenCMR2 reads it in place (`src/vfs_cab.c`, reference reader `tools/iscab.py`), so
the user gives just the CD or its image. Layout (InstallShield 6; version field 0x0100600c):

Both kinds of file start with a 20-byte common header: `ISc(`, `u32` version, `u32` volume info, `u32`
offset and `u32` size of the cabinet descriptor (0x200 in `data1.hdr`).

| Where | Offset | Content |
|---|---|---|
| Descriptor | +0x0c | `u32` offset of the file table, from the descriptor |
| | +0x1c | `u32` directory count (`CMR2`: 31) |
| | +0x28 | `u32` file count (964) |
| | +0x2c | `u32` offset of the file descriptors, from the file table |
| File table | +0 | One `u32` per directory: the offset of its name (from the file table) |
| File descriptor | 0x57 bytes | `+0x00 u16` flags (1 split across volumes, 2 obfuscated, 4 compressed, 8 invalid), `+0x02 u64` size, `+0x0a u64` compressed size, `+0x12 u64` data offset in its volume, `+0x3a u32` name offset, `+0x3e u16` directory, `+0x55 u16` first volume |
| `data<N>.cab` after the common header | +0x1c | `u32` index of the first and (+0x20) last file in this volume |
| | +0x24 | `u64` offset and (+0x34) compressed size of the first file's part in this volume |
| | +0x3c | `u64` offset and (+0x4c) compressed size of the last file's part |

A file's data starts in its volume at its offset; if that volume ends first (it is the volume's last file
and the volume's last-file size is smaller), the rest is at the first-file offset of the next volume.
Compressed data is a sequence of chunks, each a `u16` length and an independent raw deflate stream.
`CMR2.exe` is stored uncompressed in `data2.cab`. The file table has stale duplicates (a second
`Game\Bigfiles\common.bfl` with data offset 0): the first entry of a name is the real one. Directory names are the
installed paths (`Game\DemoRuns`, empty for the install root), so the cabinet's files appear at the paths
an installed game has.

## Paths and registry

The original reads `HKLM\SOFTWARE\Codemasters\Colin McRae Rally 2` (`Registry_GetString` 0x4aa720):
`Language` (`English`, `French`, `German`, `Italian`, `Spanish`, `Polish`, `American`), `Game_HDPath`,
`Game_CDPath`, `Install_Version` (`Minimum`, `Medium`, `Network Only`, ...). `Paths_Init` 0x40e8d0 builds the
data roots from them: `%s\Game\Cars`, `%s\Game\Tracks`, `%s\Game\BigFiles`, `%s\sounds`, `%s\sounds\music`,
`%s\FrontEnd`, `%s\FrontEnd\Videos`, `%s\CountrySpecific`, `%s\SetupRep`, `%s\CMR2Saves`. A minimum install
leaves the data on the CD.

The port has no registry: the language and the data root are launch parameters, and every name resolves
case-insensitively in the file layer (the disc mixes `Fra02Hi.bfl` and `fra01hi.bfl`).

## Files the game writes

| Path | What |
|---|---|
| `CMR2Saves\Configuration\Controller.rcf` | Controller bindings (`FUN_0040bf70` / `0040bff0`) |
| `CMR2Saves\Configuration\GameInfo.rcf` | Options (`FUN_004ea5e0` / `004ea840`) |
| `CMR2Saves\gamesave\game_<n>.rcs` | Championship / game saves (`FUN_004f4ef0`, `FUN_004f5150`) |
| `CMR2Saves\NetworkLeaderboards\leaderboards.nlb` | Network leaderboards |
| `replays\<stage>H/L` | Saved replays |
| `c:\error.txt` | Debug log ("FILE PRINT DEBUG INFORMATION", "Program finished normally") |

In the port these go to `gasm:storage` keys with the same names. Their layouts are open; they matter only
for compatibility with old saves, which is not a goal.
