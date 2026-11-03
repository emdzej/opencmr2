# Game data

OpenCMR2 runs the game from your copy of **Colin McRae Rally 2.0 for PC** (CD-ROM, Codemasters). Nothing
from the game is included or downloaded.

What it reads:

- The CD: the folders `FrontEnd`, `Game`, `SetupRep`, `Sounds`, `CountrySpecific` (about 650 MB), as a
  folder or as a disc image (`.iso`).
- The program `CMR2.exe` (version 1.11, 1,228,800 bytes): its tables (cars, surfaces, menus, cheat codes)
  are read at run time. On the CD it is inside the installer cabinet (`data1.hdr`, `data1.cab`,
  `data2.cab`); OpenCMR2 reads it from there, so the disc or its image is all you need
  ([how-to](../howto/extract-cd.md)). An installed copy of the game also has it.

Other releases (the US version, the Polish one, later budget releases) have not been checked yet. The
program is identified by size and checksum; other versions will need their own checks.

## Where the data lives

| Folder | What |
|---|---|
| `FrontEnd` | Menus, car pictures, fonts, the intro movies (`Videos/*.bik`) |
| `Game/Cars` | 22 cars: models (`.c3d`), textures (`.bfl`), car info (`.cin`) |
| `Game/Tracks` | 94 stages in eight rallies, the Challenge stages in Japan, eight Arcade circuits |
| `Game/Bigfiles` | Shared in-game textures, HUD, sounds |
| `SetupRep` | Service-area and stage-map screens |
| `Sounds` | Engines, surfaces, music (`Music/*.adp`) |
| `CountrySpecific/Europe` | Text and co-driver speech in English, French, German, Italian, Spanish |

