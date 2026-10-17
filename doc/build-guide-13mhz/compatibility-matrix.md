# 13.56 MHz Handpiece and Cartridge Compatibility

This guide covers ordinary single-tip soldering handpieces for the 13.56 MHz Hot Wand. It does not cover tweezers, desoldering tools, or the 470 kHz Hot Wand Lite.

The important distinction is between **station compatibility** and **cartridge compatibility**. Several handpieces can connect to the same 13.56 MHz source through an F-type coaxial connector, but that does not make their cartridges mechanically interchangeable. In particular, the Hakko FX-1001 can be connected to the Hot Wand, but it accepts Hakko T31 cartridges—not Metcal-style cartridges.

The recommendations below favor broad chisel tips. A chisel approximately 1.5 to 2.5 mm wide is more useful for general bench work than a needle conical tip: it can solder common through-hole joints, wires, and most SMD pads while presenting enough surface area for good heat transfer.

## Handpieces

### H1 — Thermaltronics SHP-1

**Hot Wand connection:** direct, standard F-type, 13.56 MHz  
**Cartridges:** Thermaltronics M series; Metcal STTC and SMTC series

This is the most straightforward new handpiece for the shared Metcal-style cartridge ecosystem. Thermaltronics explicitly specifies the SHP-1 for its TMT-9000S and for Metcal MX-500, MX-5000, and MX-5200 systems, and specifies M-, STTC-, and SMTC-series cartridges. See the [official SHP-1 datasheet](https://www.thermaltronics.com/downloads/SHP-1_Datasheet.pdf).

Use H1 for the broadest choice among the Metcal, Thermaltronics, and legacy EasyBraid cartridges recommended below.

### H2 — Metcal MX-RM3E or MX-H1-AV

**Hot Wand connection:** direct, standard F-type, 13.56 MHz  
**Cartridges:** Metcal STTC and SMTC series; equivalent M-style replacements where the replacement manufacturer explicitly lists the applicable MX system

The MX-RM3E is the conventional general-purpose choice; the MX-H1-AV is the more expensive advanced handle. Metcal specifies both for STTC and SMTC cartridges. Its MX documentation identifies the handpiece connector as F-type and the system frequency as 13.56 MHz. See the [MX-RM3E datasheet](https://www.metcal.com/wp-content/uploads/2023/01/Metcal-MX-RM3E-Data-Sheet-EN.pdf) and [Metcal cartridge catalog](https://www.metcal.com/wp-content/uploads/2023/09/Metcal-Catalog-2023.pdf).

Older handles bearing **OK International** or **OKi** branding may be the same Metcal MX family, but buy by exact part number rather than by appearance. Do not confuse MX handles with Metcal MFR, PS-900, or SP200 handles; those are different, lower-frequency systems.

### H3 — EasyBraid EBSHP-1 / SHP-1 (legacy)

**Hot Wand connection:** conditional; verify a standard F-type plug before buying  
**Cartridges:** EasyBraid EBM series and their documented Metcal STTC equivalents

EasyBraid's EB-9000S was a 13.56 MHz system and its manual identifies the handle as `EBSHP-1`; some older literature shortens this to `SHP-1`. The system shipped with an EBM cartridge. See the [EB-9000S manual](https://media.digikey.com/pdf/Data%20Sheets/Easy%20Braid%20PDFs/EB9000S_Manual.pdf).

This is now mainly a used or old-stock option. EasyBraid used the `SHP-1` name for this handle while other vendors also used similar names, and surviving listings often omit the connector specification. Confirm from a clear seller photograph that the cable ends in the same screw-on male F connector used by the Hot Wand. Do not substitute EasyBraid `SHP-P`, `SHP-K`, `SHP-KM`, or `SHP-S`; those belong to other cartridge and frequency families.

### H4 — Hakko FX-1001

**Hot Wand connection:** direct, standard F-type, 13.56 MHz  
**Cartridges:** Hakko T31 and T31B only

Hakko expressly says that the FX-1001 with T31 cartridges may be used on a 13.56 MHz Metcal supply having a standard F-type connector. This makes the complete Hakko handle-and-cartridge combination a reasonable Hot Wand option. See [Hakko's compatibility note](https://kb.hakkousa.com/Knowledgebase/10637/Can-I-use-my-HAKKO-FX1001-Soldering-Iron-Handpiece-and-T31-Series-Tips-with-my-Metcal%C2%AE-Power-Supply) and the [FX-1001 product page](https://hakkousa.com/products/soldering/fx-1001-rf-induction-heating-soldering-iron-handpiece-only.html).

**What can go into the Hakko handpiece?** Use a T31 or its current T31B successor. Do not insert an STTC, SMTC, Thermaltronics M, or EasyBraid EBM cartridge. Those cartridges may operate from the same kind of RF source when fitted to their own handles, but they are not specified as mechanically interchangeable with T31.

The T31 temperature code is easy to misread: `-01` is 450 °C, `-02` is 400 °C, and `-03` is 350 °C. For the Hakko handpiece, this guide recommends the `-02` series. Hakko publishes these values in the [FX-100 specifications](https://www.hakko.com/english/products/hakko_fx100_spec.html).

### Other brands and systems

A brand name, an induction-heating claim, or an F-looking connector is not enough by itself. Use a handpiece only when all three of these facts are known:

1. it is intended for 13.56 MHz operation;
2. it has the standard F-type RF plug and appropriate grounding; and
3. its intended cartridge family and temperature series are known.

This excludes common JBC, Weller, Pace, and conventional Hakko resistance-heated handles. It also excludes the lower-frequency Metcal/OKi MFR, PS-900, and SP200 families; Thermaltronics TMT-2000 handpieces; and EasyBraid EB-2000/EB-5000 handpieces. Similar-looking cartridges from these systems are not substitutes for a 13.56 MHz cartridge.

## Recommended general-purpose cartridges

Curie-regulated cartridges do not have an adjustable setpoint. Published temperatures are series ratings, and the actual idle temperature varies with geometry and measurement method. Thermaltronics and EasyBraid publish a 350–398 °C range for their 700-type cartridges. Metcal currently calls its 700 series nominally 390 °C, while also publishing a 413 °C maximum heater temperature and noting that cartridge idle temperature can be up to 15 °C lower. Treat these as selection bands, not precision limits.

### C1 — Thermaltronics M7CH176

**Use with:** H1; H2 where the applicable MX system is listed  
**Geometry:** 30° chisel, 1.78 mm wide, 10 mm long  
**Published temperature:** 350–398 °C

This is the best default recommendation. It is wide enough to transfer heat well but still comfortable on ordinary SMD pads. Thermaltronics identifies it as equivalent to `STTC-137` and explicitly lists the MX-500, MX-5000, and MX-5200 systems. See the [M7CH176 datasheet](https://www.thermaltronics.com/downloads/datasheets/M7CH176_Technical_Datasheet.pdf).

### C2 — Metcal STTC-137

**Use with:** H1 or H2  
**Geometry:** 30° chisel, 1.78 mm wide, approximately 10 mm long  
**Published temperature:** 700 series, nominally about 390 °C

Choose this when an original Metcal cartridge is preferred. It is the canonical Metcal version of the same general-purpose geometry as C1. Metcal recommends the 700 series for most applications; see the [official STTC-137 listing](https://store.metcal.com/en-us/shop/cartridges-tips/STTC-137). Be aware of the temperature-rating nuance above if “under 400 °C” is a strict process requirement rather than a desired nominal working temperature.

### C3 — EasyBraid EBM7CH176 (legacy alternative)

**Use with:** H3; also listed for Metcal MX-500/MX-5000 systems and therefore a sourcing alternative for H1/H2  
**Geometry:** 30° chisel, 1.78 mm wide, 10 mm long  
**Published temperature:** 350–398 °C

EasyBraid identifies this cartridge as equivalent to `STTC-137`. It is a sensible purchase only when genuine old stock is available at a good price; distributor listings now commonly mark it discontinued. The archived [EasyBraid replacement-tip guide](https://media.digikey.com/pdf/Data%20Sheets/Easy%20Braid%20PDFs/Replacement_Soldering_Tips_Stations.pdf) gives the cross-reference.

### C4 — Hakko T31B-02D24

**Use with:** H4 only  
**Geometry:** 2.4 mm D-shape chisel  
**Published temperature:** 400 °C

This is the general-purpose choice for the Hakko FX-1001. The 2.4 mm face gives better heat transfer than a needle tip and remains small enough for routine PCB work. `T31B-02D24` is the newer-shape successor to `T31-02D24`; either is appropriate if genuine stock is available. Hakko lists both versions in the [official T31 cartridge list](https://www.hakko.com/english/products/hakko_fx100_tips.html).

The otherwise similar `T31B-03D24` is the cooler 350 °C version, while `T31B-01D24` is the hotter 450 °C version.

## Quick choice

- For the easiest new, cross-brand setup, buy **H1 (Thermaltronics SHP-1)** with **C1 (M7CH176)**.
- If you already own a Metcal MX handle, use **H2** with **C2 (STTC-137)** or **C1**.
- Buy **H3/C3** only when the legacy EasyBraid parts and connector can be positively identified.
- If you want the Hakko handle, buy **H4 (FX-1001)** with **C4 (T31B-02D24)**. Nothing in the STTC/M/EBM group should be put into that handle.

The Hot Wand matching network and cartridge-disconnect detector were developed around an STTC cartridge. Even when a handle is frequency- and connector-compatible, first bring-up with a different cartridge family—especially Hakko T31—should be supervised at low power while checking heating, RF-stage temperature, and disconnect detection.
