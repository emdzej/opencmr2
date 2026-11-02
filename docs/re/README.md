# Reverse-engineering notes

OpenCMR2 is ported from the Windows program `CMR2.exe` (Colin McRae Rally 2.0, Codemasters, 2000;
version 1.11). These notes record what is known about it, with exact addresses, so that the port can cite
the original function by function.

| Page | What |
|---|---|
| [Architecture](architecture.md) | Main loop, modes and the process framework, timing, files, rendering, sound, input, network |
| [Inventory](inventory.md) | The function map by address range, with the anchor functions of each subsystem |
| [Port plan](port-plan.md) | The order of the work, with the check that ends each step |
| [Challenge](challenge.md) | Mode 4: the record, race state 6, the results' ways out |
| [File formats](../formats/index.md) | The disc, the containers, models, stages, cars, media |

## Tools and set-up

- **The program.** `CMR2.exe` is not on the disc as a file: extract it from the cabinet with
  `tools/iscab.py cd CMR2.exe cmr2.exe` ([extract the CD](../howto/extract-cd.md)). `./cmr2.exe` is the
  Ghidra copy (git-ignored). `tools/exe.py <va> [count] [u32|u16|u8|i32|f32|s]` prints values at virtual
  addresses.
- **Ghidra** through [ghidra-cli](https://github.com/akiselev/ghidra-cli): project `cmr2`, program `cmr2.exe`.

  ```sh
  ghidra import "$PWD/cmr2.exe" --project cmr2 --program cmr2.exe        # once, with analysis
  tools/ghidra/apply-names.sh                                            # names.tsv -> Ghidra, then the dumps
  ghidra decompile 0x4a9720 --project cmr2 --program cmr2.exe
  ghidra x-ref to 0x4aa220 --project cmr2 --program cmr2.exe
  ```

  `apply-names.sh` runs `FillGaps.java` (functions the analysis missed), `ApplyNames.java`
  (`tools/ghidra/names.tsv`), then writes `re/all.c` (every function decompiled, for grepping),
  `re/meta.tsv` (address, name, size, callers, callees, imports, strings) and `re/funcs.txt`. `re/` is
  git-ignored: it is derived from the original.
- **Names.** `tools/ghidra/names.tsv` is the record: address, name, module, confidence (high = read and
  understood, med = from calls and strings, low = guess), description. Rename there, re-run the script.
- **Data.** `tools/bfl.py <file> [outdir]` lists or extracts any file of the disc; `tools/cmr2fmt.py
  png|wav|obj <file[:member]>` exports a texture, sound or model (`obj` places a car's parts by their
  matrices and writes the textures as PNGs; for a stage pass the country's `<country>hit.bfl` as the
  texture archive). Decoded data goes to `out/` (git-ignored).

## Rules for the notes

- Describe layouts, formulas and addresses in your own words. No decompiler output in the notes or the
  source.
- Mark what is confirmed and what is inferred; give the address that would confirm it.
- Community knowledge (CMR2 modding tools and notes) may help with a format; the exe is the authority and
  no code is copied.
