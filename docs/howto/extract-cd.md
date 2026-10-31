# Extract the CD

OpenCMR2 needs the data from your own copy of Colin McRae Rally 2.0 (PC CD-ROM): the CD itself, an image
of it (`CMR2.ISO`), or a folder with the CD's files. The game data is on the disc as plain files; the
program `CMR2.exe` and the demo runs are inside the InstallShield cabinet (`data1.hdr`, `data1.cab`,
`data2.cab`), which OpenCMR2 reads in place. No installation and no extraction tool is needed.

## What you need

- The disc, or an image of it (`CMR2.ISO`, 683,599,872 bytes for the release used here).
- For a folder copy: `bsdtar` or `7z` for the image, or copy the mounted CD.

## A folder copy

```sh
mkdir -p cd
bsdtar -xf /path/to/CMR2.ISO -C cd          # or copy the mounted CD
chmod -R u+rwX cd
```

Running from the image works as well; the folder is only faster to work with.

## For development

The reverse-engineering tools want the program as a file. `tools/iscab.py` lists and extracts the cabinet:

```sh
mkdir -p game
python3 tools/iscab.py cd CMR2.exe game/CMR2.exe
python3 -c "import zlib;d=open('game/CMR2.exe','rb').read();print(len(d),hex(zlib.crc32(d)))"
# 1228800 0x44c022f4
cp game/CMR2.exe cmr2.exe                   # the Ghidra copy
```

`cd/`, `game/` and `cmr2.exe` are git-ignored. Never commit them or anything decoded from them.

Tests and tools read `OPENCMR2_DATA=<dir|image>` (default `./cd`).
