This directory contains electrical design files for the Hot Wand project

The main `hot-wand` project is the 13.56 MHz version, while the `hot-wand-lite` is the 470 kHz version meant to be less expensive.

The schematic and PCB are designed with CadSoft EAGLE 7. PDF versions of the schematic and image previews of the PCB are provided for convenience.

The `mfg` directory will contain the BOM and gerber files, along with some helpers that aid in generating these files.

The BOM, CPL files, and the design rules, are all meant for use with JLCPCB's PCBA service.

## Release Procedure

0. Run a DRU check, and also run `drill_sizes_report.bat` to catch stray drill sizes
1. Edit the symbol and footprint for the version to reflect the latest version designation
2. Generate the schematic preview PDF file using `eagle_schematic_preview.py`
3. Generate the gerber files using `generate_gerbers.bat` (this also generates the gerber preview)
4. Generate the BOM and CPL files using `generate_bom_cpl.bat`
