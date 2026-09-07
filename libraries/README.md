# EuroClack project libraries

Open the root KiCad project. Its two library tables register `EuroClack` from this directory. The schematic and symbol library use KiCad 10 format; the PCB remains in its original KiCad 9 format.

- `EuroClack.kicad_sym`: the three custom symbols placed in the current schematic (RP2350_80QFN, PCM1808PWR, PS-8556DVA-6PN), copied from its embedded definitions to preserve the current pins and graphics.
- `EuroClack.pretty/`: 18 used project footprints. Four were recovered with KiCad from the PCB: the three missing Clacktronics passive footprints and the EasyEDA switch. The remaining 14 preserve the existing local footprint definitions.
- `3dmodels/`: consolidated local component models, including existing alternates. These assets remain ignored by Git under the existing redistribution policy; a checkout alone will not include them.

Standard KiCad symbols, footprints and their model references remain dependencies on the installed KiCad libraries. All custom library references now use EuroClack. The MCU's two former footprint aliases resolve to the same retained footprint, and the PCM1808 symbol's default now names the standard footprint already assigned to the placed part. Current board placement, copper, pad numbering and schematic connectivity were preserved.

Twelve unreferenced local footprint files and the unused FXL0630-100-M and TS34056055-0440 symbol definitions were removed. The obsolete EasyEDA import archives also contained an unused USB symbol. The active switch definition was preserved in native KiCad form, so the importer and its configuration are no longer required.

The library retains existing third-party names, metadata and source links. Consolidation does not establish or change their licensing. The root design review and analysis manifest describe an earlier snapshot; their recorded hashes and historical library warnings have not been rewritten.
