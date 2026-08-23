# Analysis of SergeyMax's Soldering-Station Video

This document extracts the design and construction lessons from the local [English subtitle transcript](sergeymax_video_subtitles.txt). Approximate video times come from the accompanying [SRT file](sergeymax_video_subtitles.srt). The discussion below is concerned with the homemade 13.56 MHz station rather than the opening soldering demonstration or the extended review of cartridges and handpieces.

## Source limitations

The subtitles appear to be an automatic translation from Russian. They contain mistranscriptions, inconsistent terminology, and at least one obvious numerical error. For example, they say that dividing a 27.12 MHz crystal by two produces 15 MHz; the intended carrier is 13.56 MHz. Terms such as "clamp," "saw," and "container" usually mean cartridge/tip and capacitor, while "lezendrat" means Litz wire.

The transcript also distinguishes the narrator's build from an earlier article and board design. It is therefore unsafe to assume that every modification shown in the video was incorporated into every published schematic, PCB, or repository revision. The narrator says that he sent corrections to the article's author and that the article was updated, but the subtitles do not identify which corrections were merged or which downloadable design files were regenerated.

Consequently, this document uses three levels of confidence:

- **Video report:** something the narrator says happened on his unit.
- **Engineering interpretation:** the likely electrical explanation, which may be clearer than the translated wording.
- **Hot-Wand status:** what can presently be seen in this repository's [schematic](../../electrical/hot-wand.sch), [board](../../electrical/hot-wand.brd), firmware, and documentation.

## Executive summary

The video is not merely an assembly review. Its central message is that the published design was not a straightforward build-and-run project. The narrator describes it as initially raw and says substantial debugging was required before it became dependable. After the changes, however, his single unit reportedly survived about three years of very heavy service.

The most consequential lessons are:

1. Use a known fundamental-mode 27.12 MHz crystal and treat its load-capacitor return as a sensitive local RF circuit.
2. Dampen the long microcontroller-to-gate-driver clock trace with a series resistor placed as a real source termination.
3. Do not trust remarked marketplace gate-driver ICs at 13.56 MHz.
4. Treat current-transformer polarity as safety-critical; the wrong phase can command more power when it should command less.
5. Provide substantially more cooling than the original enclosure apparently did.
6. Bond the RF ground and metal enclosure deliberately, and test radiated/conducted interference in every intended power configuration.
7. Prefer an external certified DC supply over reproducing the compact mains resonant supply unless compactness justifies the sourcing, transformer, thermal, and safety burden.
8. Validate every high-voltage RF capacitor by voltage rating, dielectric, RF loss, availability, and the actual measured waveform—not capacitance alone.

### Hand modifications that must not be assumed to exist in published files

The video visibly or verbally identifies the following changes to the narrator's physical unit. The transcript does not establish that any particular downloadable PCB revision includes them:

| Modification on the working unit | Likely status in design files |
|:---------------------------------|:------------------------------|
| Series damping resistor cut into the MCU-to-driver clock trace | May have been added only as a board rework; verify placement, not just schematic presence |
| Crystal load capacitors disconnected from the noisy polygon and returned directly to MCU ground | Explicit hand rework; do not assume an older PCB incorporates it |
| Genuine MAX17602 fitted in a different package | Procurement/package substitution, with added thermal coupling rather than a clean PCB revision |
| Extra external heatsink attached to both power MOSFETs | Mechanical modification absent from the original design |
| Vent holes and forced-air fan | Enclosure modifications absent from the original design |
| Input/output physical arrangement reversed | Builder-specific mechanical/layout change |
| A displaced capacitor connected by wires after the rearrangement | Builder-specific workaround, not a generally approved RF-layout practice |
| RF connector/chassis bonded to protective earth | System-wiring requirement that a PCB file alone cannot capture |
| External 32 V printer supply kept as a replacement for the resonant mains section | Proposed architectural replacement, not necessarily the supply used during every reported test |

The narrator's statement that corrections were sent to the article author is useful provenance, but it is not a revision manifest. Only inspection of a dated schematic, PCB, BOM, and assembly guide can show whether a given correction was incorporated.

## Architecture described in the video

The narrator describes the station as several interacting power converters:

- a compact approximately 80 W resonant mains supply;
- a variable DC supply for the main RF amplifier, reportedly moving from roughly 8 V to 22 V as the cartridge load changes;
- auxiliary conversion for control circuitry;
- a microcontroller clocked from 27.12 MHz and producing a 13.56 MHz square wave;
- a gate driver and a small first RF power stage;
- a resonant drive network around L8 that recirculates the main MOSFET's gate energy;
- the main class-E RF stage and a multi-section output matching/filter network; and
- current-derived feedback plus cartridge-disconnect protection.

This agrees in broad outline with the architecture already discussed in [design-study.md](../design-study.md), [gate-driver-method-comparison.md](../gate-driver-method-comparison.md), and [design-study-current-transformer.md](../design-study-current-transformer.md).

## Problems, fixes, and design implications

### 1. The published project was not turnkey

At approximately 03:10-03:35, the narrator says the design looked reproducible from the article and video but required a long period of correction before it was reliably usable. He calls the original implementation extremely raw, while also crediting its author for a large and difficult body of work.

This is important context for every following item: values and topology copied from the files are starting points, not proof of a production-ready implementation. A successful build requires staged bring-up, current limiting, oscilloscope measurements, thermal measurements, and RF tuning.

### 2. High-voltage RF capacitors were difficult to source

At approximately 06:30-07:25, the narrator discusses the 47 pF and 100 pF capacitors in the resonant network. The translation reports a 1500 V rating and says that suitable parts were difficult to buy in small quantities. Later, at approximately 10:25, he calls out a missing original-design capacitor identified as C47 in his files: 10 pF, 500 V, nominally 1206, which he salvaged from other equipment.

The reference designator is revision-specific. It should not be mapped directly to Hot-Wand's present C47. The analogous 10 pF/500 V component in the current schematic is associated with the current-transformer network under a different reference designator.

Engineering implications:

- RF capacitors need an appropriate dielectric, low loss at 13.56 MHz, adequate RMS current, and adequate peak-voltage margin.
- A DC voltage rating alone does not establish RF suitability.
- A substitute with the same nominal capacitance can change the matching network through tolerance, parasitic inductance, and dielectric behavior.
- Parts that are only available in institutional reel quantities are poor choices for a reproducible hobby project.

**Hot-Wand status:** the present schematic explicitly selects 47 pF and optional 100 pF 0805 parts rated 1000 V, rather than the 1500 V stated in the video. This is a deliberate or availability-driven difference, not a faithful copy. It needs to be justified by measured peak voltage and temperature in the tuned and fault cases. The 10 pF/500 V current-feedback capacitor remains a special/manual or DNP-class item rather than an ordinary JLC-assembled component.

### 3. The 27.12 MHz crystal must be fundamental-mode

At approximately 07:30-08:30, the narrator says that generic 27.12 MHz crystals often oscillated on the wrong mode and that he ultimately ordered a known part in quantity. The translated claim that common parts start on a "second" or "third" harmonic is imprecise, but the underlying warning is valid: crystal frequency alone is not a sufficient specification. Oscillation mode, load capacitance, ESR, drive level, and the MCU oscillator's supported range matter.

**Hot-Wand status:** the design names a specific SMD crystal, `ABM8-27.120MHZ-10-D1G-T`, rather than leaving a generic 27.12 MHz part. That is a substantial improvement in reproducibility. It still needs startup testing across voltage, temperature, and component tolerance; firmware checking that HSE is ready and selected cannot prove that an unintended frequency is correct without an independent timing reference.

### 4. Counterfeit or remarked MAX17602 drivers failed at RF

At approximately 09:05-10:10, the narrator reports that marketplace MAX17602 devices worked around 1 MHz but merely heated at 13 MHz. He concluded that they had been remarked. A genuine device in a different package worked, although losing the intended exposed-pad thermal path forced him to couple heat into the enclosure.

This illustrates why a low-frequency logic test is not a meaningful authenticity or performance test for an RF gate driver. A suspect part can toggle slowly while having excessive output resistance, transition time, shoot-through, or internal dissipation at 13.56 MHz.

**Hot-Wand status:** Hot-Wand does not use the MAX17602. It substitutes a JLC-sourced 1EDN8511B and documents the associated timing and power questions in [gate-driver-method-comparison.md](../gate-driver-method-comparison.md). This avoids the exact counterfeit-MAX17602 problem but does not make RF validation optional. U2 temperature, its supply current, both gate-resistor temperatures, and the IRF510 gate waveform must still be checked at continuous carrier operation.

### 5. The toroid material matters, but the narrator's substitutes worked

At approximately 11:00, the narrator describes powdered-iron rings with relative permeability around 8.5, referred to as material number 6. Imported originals were expensive, so he bought inexpensive substitutes and reports no trouble with them in his unit.

This is useful evidence, but it is evidence for one batch and one build—not proof that an arbitrary ring of the same color or marketplace description is equivalent. Core dimensions, permeability, loss versus frequency, saturation behavior, and temperature coefficient all matter. Paint color is not a material specification.

**Hot-Wand status:** the large RF inductors specify Amidon T130-6 cores. The smaller chokes and current transformer use a documented Fair-Rite material substitute and include winding instructions in [assembly-instructions.md](../assembly-instructions.md). This is more controlled than choosing an anonymous ring, but the finished inductance, core temperature, and current-transformer phase behavior still require measurement.

### 6. The compact resonant mains supply was not worth repeating

At approximately 11:45-14:30, the narrator praises the compact approximately 80 W mains supply but describes several costs:

- an expensive and difficult-to-source resonant-controller IC;
- unexplained controller overheating at idle, improved by adding a heatsink;
- a transformer that depended on specific core and Litz-wire choices;
- impractical small-quantity sourcing of the required Litz constructions; and
- much more design and safety burden than using an external supply.

His conclusion is unusually direct: if rebuilding the station, he would remove that section and use an external approximately 32 V printer supply.

**Hot-Wand status:** Hot-Wand already follows the architectural recommendation by omitting the mains converter and accepting external DC/USB-PD power. The video's 32 V suggestion belongs to his version and must not be copied blindly: Hot-Wand's documented operating limits and component ratings govern its allowable input voltage.

### 7. The MCU-to-driver trace rang and required series damping

At approximately 15:12-15:25, the narrator says a resistor had to be inserted into the long microcontroller-to-driver trace because it rang. The subtitles call this a low-frequency output, but it is the fast-edged 13.56 MHz RF clock. Its edge rate, not merely its fundamental frequency, makes a long PCB trace a transmission line.

The likely remedy is source-series termination: place a resistor close to the MCU output so its resistance plus the MCU's output impedance approximates the trace impedance. The outgoing edge is launched at reduced amplitude, and the reflection from the high-impedance receiver returns to the source and is absorbed instead of being re-reflected. A resistor placed only at the receiving end can isolate the input or limit current but is not conventional source termination.

**Hot-Wand status — only partial:** the schematic contains a 100 ohm series resistor in the RF-clock net. On the current PCB, however, that resistor is beside the gate driver, after the long route from the MCU. The trace crosses much of the board before reaching it. This does not implement the video fix as a conventional source termination. The layout should be reviewed with a fast probe at both the MCU pin and U2 input; if the resistor is intended to terminate the source, it should be relocated close to the MCU and its value tuned from measured edge behavior.

### 8. Crystal load-capacitor grounding was the most important stability fix

At approximately 15:25-16:44, the narrator calls the crystal area his most important correction. In his board, the two load capacitors returned through a ground region carrying noisy power/RF current. The oscillator became unstable, the display showed garbage, and the station stopped. He cut the capacitors away from that return, mounted them directly at the crystal, and connected their common return directly to the MCU ground pin. The faults then disappeared.

The engineering lesson is not to create a separately floating "clean ground." It is to keep the crystal loop tiny and ensure its return joins MCU ground locally, without sharing appreciable impedance with gate-driver, buck-converter, or RF power currents.

**Hot-Wand status — improved but not proven:** Hot-Wand uses an SMD crystal and nearby SMD load capacitors in the MCU region, well away from the RF power stage. Their ground connection is nevertheless represented primarily by the common ground plane rather than an explicit point-to-point local return visible in the netlist. The layout should be inspected for return-current paths and tested for oscillator stability during cold start, maximum RF output, tip removal, fan transitions, and input-voltage changes.

### 9. The original thermal design was inadequate

At approximately 16:50-17:55, the narrator says the original case did not remove enough heat. He added an external heatsink coupled to the two power transistors, drilled ventilation holes, found passive ventilation insufficient, and finally added forced-air cooling. The result was acceptable temperature but required opening the unit and removing dust roughly every six months.

This leads to several design requirements:

- heatsinking must be evaluated with the enclosure assembled;
- MOSFET case temperature alone is insufficient—driver, buck converter, RF capacitors, resistors, and magnetic components also need measurement;
- forced air creates a maintenance requirement and a failure mode when the fan or airflow becomes obstructed; and
- the controller should detect excessive temperature independently of whether the fan was commanded on.

**Hot-Wand status:** the design presses MOSFET heatsinks against the aluminum enclosure, includes a fan, and includes temperature sensing and fan-control firmware. Those choices directly address the video. Hardware validation still needs a fan-stalled test, a dusty/obstructed-airflow test, and thermal equilibrium measurements after cold-tip warmup and sustained operation.

### 10. Connector and enclosure layout affect RF behavior and serviceability

At approximately 17:55-18:55, the narrator explains that he reversed the physical arrangement of the input switch and RF output because the original arrangement forced wiring across the enclosure. His rearrangement shortened or simplified some wiring but caused a large capacitor to no longer fit the PCB, so it was wired remotely.

The general lesson is more valuable than the exact rearrangement:

- keep the RF output connector adjacent to the matching network;
- keep mains or DC input wiring away from RF output structures;
- verify component and lid clearance in the final enclosure before ordering boards; and
- avoid adding long leads to capacitors in switching or RF-current paths unless their current is demonstrably small.

The transcript says the remotely wired capacitor carried little stress, but it does not identify the node clearly enough to generalize that choice.

### 11. Chassis bonding and earth connection were essential on his unit

At approximately 18:55-19:20, the narrator says he bonded ground directly to the RF output connector and to protective earth. He describes the metal case and grounding as essential, reporting severe interference—including monitors going blank—when the generator was operated unshielded or ungrounded.

This is an anecdotal EMC observation, not a compliance test, but it is a serious warning. A 13.56 MHz, tens-of-watts generator and its cable can be an effective transmitter when common-mode current is uncontrolled.

**Hot-Wand status — different/partial:** board ground is connected to mounting hardware and can be bonded to the metal enclosure, but USB-PD and battery operation provide no protective-earth conductor. Chassis bonding can still reduce internal electric-field coupling, but it is not equivalent to the narrator's earth bond. Hot-Wand needs conducted and radiated interference testing in battery, floating USB, and earth-referenced bench-supply configurations. If an optional earth point is added, it should be intentional and its effect on output common-mode current and ESD safety should be measured rather than assumed.

### 12. Current-transformer phase can create a dangerous positive-feedback condition

Near 35:40, the narrator mentions that he initially reversed the phase of the current transformer. The translated account is awkward, but the reported result is clear: the cartridge heated to a dull red before he intervened.

This is one of the most important safety findings in the video. The current transformer is not merely a display sensor. Its phase-sensitive network influences the main buck converter. Reversing a winding can turn negative feedback into positive feedback, causing the controller to increase RF power as the load condition calls for less.

**Hot-Wand status — documentation gap:** the [current-transformer study](../design-study-current-transformer.md) explains the phase detector, and the [assembly instructions](../assembly-instructions.md) specify a 1:14:14 bifilar winding. They do not currently make the start/finish polarity, dot convention, low-power polarity test, or reversed-feedback symptom sufficiently explicit. Before full-power operation, the detector output and buck response should be checked with a current-limited supply and a known load. Increasing the sensed "hot" condition must reduce commanded RF supply voltage. Firmware shutdown is not a substitute because this feedback path is analog and can react between firmware observations.

### 13. Tip removal was supported, but it is electrically stressful

At approximately 24:25, the narrator demonstrates live cartridge removal. His station detects the open cartridge, reports an error, and resumes after reinsertion. He says the protection worked reliably during his ownership.

The video demonstrates successful protection on that occasion; it does not show that live removal is harmless. The output network already contains stored RF energy when the load disappears, so voltage can rise before any detector or firmware responds.

**Hot-Wand status:** Hot-Wand is deliberately more conservative. The disconnect fault is latched and requires user acknowledgement after the cartridge is restored, as documented in [tip-detector.md](../tip-detector.md). The operating instructions recommend powering down before changing cartridges. The output clamp network and the disconnect detector still need fault testing because even a correct shutdown cannot prevent the first unloaded transient.

### 14. Real duty cycle was much lower than the nameplate power suggests

The narrator reports:

- under 10% displayed power when idling with the handpiece active;
- approximately 2-3% in magnetic-stand sleep mode;
- 100% mainly during cold start or recovery after leaving the stand; and
- no more than about 50% during his ordinary soldering work.

These observations explain why an 80 W source can coexist with much lower long-term average dissipation. They must not be used to underrate components. The RF path, gate driver, current-sense network, and power supply must survive the full-power warmup interval and abnormal loads. Thermal tests should include repeated stand removal and cold-cartridge starts, not only steady soldering.

### 15. Heavy service validated the corrected unit, not the untouched files

At approximately 19:25 and again near the end, the narrator says the corrected station had operated for roughly three years in near-continuous professional use without repeated power-transistor failures. This is valuable field evidence for the underlying architecture.

It is not evidence that:

- the original unmodified board is reliable;
- substitute parts or cores are automatically equivalent;
- every published file includes his hand modifications; or
- thermal and EMC behavior will transfer to another enclosure.

The correct conclusion is that the architecture can be made robust, but the hand fixes are part of the validated configuration.

## Operational and mechanical observations

The latter half of the video is less relevant to the PCB, but it contains useful product-level lessons:

- The original handpiece grip and strain-relief rubber degraded after extended use, though the defects were mostly mechanical or cosmetic.
- The soft silicone coaxial cable was heavy enough to pull the light handpiece from the user's hand when hanging over the bench edge. Cable routing and stand placement affect ergonomics.
- Silicone cable resisted soldering heat but collected dirt and required cleaning.
- Genuine Metcal cartridges reportedly heated faster than compatible Thermo-Tronics cartridges in this station.
- A large high-temperature cartridge was rarely useful and took roughly ten seconds to heat versus roughly four seconds for ordinary cartridges.
- The cartridges showed little natural electrical wear after three years. The damaged examples resulted from setup error or contact with a charged power-supply capacitor rather than normal use.
- The charged-capacitor incident is a reminder that service procedures need deliberate discharge verification, not merely switching off input power.
- The magnetic stand reduced idle power substantially. Hot-Wand's current user-controlled sleep behavior does not reproduce that automatic workflow unless a stand sensor is added externally.

## Cross-check against the current Hot-Wand design

| Video lesson | Current repository status | Assessment |
|:-------------|:--------------------------|:-----------|
| Omit the difficult compact mains converter | External DC/USB-PD architecture | Addressed |
| Specify a known 27.12 MHz crystal | Exact Abracon part selected | Addressed, still test startup/mode |
| Keep crystal load-cap return local | SMD parts are near MCU, using common plane | Improved, needs return-path validation |
| Add damping to the long MCU clock trace | 100 ohm resistor exists at driver end | Partial; not conventional source termination |
| Avoid suspect MAX17602 parts | 1EDN8511B substitution from PCBA supply chain | Addressed differently; RF/thermal test required |
| Use RF-suitable high-voltage capacitors | Specified 1 kV parts, versus 1.5 kV in video | Different; confirm measured margin and loss |
| Provide substantial MOSFET and enclosure cooling | Enclosure-coupled heatsinks, fan, thermal sensing | Addressed; validate fan-fault and dust cases |
| Bond and shield RF hardware | Circuit ground can bond through mounting hardware | Partial; no earth in battery/floating USB modes |
| Control current-transformer polarity | Winding ratio documented | Incomplete; polarity and response test need explicit instructions |
| Stop on cartridge disconnect | High-priority detector with latched fault | Addressed more conservatively |
| Treat full power as a transient but valid state | Firmware and thermal design allow warmup | Validate repeated full-power recovery cycles |

## Recommended actions before manufacturing or full-power bring-up

1. Move or duplicate the RF-clock series resistor at the MCU pin, then choose its value from measurements at both ends of the trace. Do not assume the receiver-end resistor solves the ringing reported in the video.
2. Review the crystal layout as a current-return loop. If necessary, give both load capacitors a short local return to the nearest MCU ground connection before joining the main plane.
3. Add transformer dot markings, start/finish lead labels, and a polarity test to the assembly instructions. State explicitly that reversed phase can command excessive power.
4. Establish a staged power-up procedure: current-limited auxiliary supply first, verify 13.56 MHz and U2 output, tune the IRF510/L8 stage, verify feedback polarity, and only then enable the main RF rail at reduced voltage.
5. Measure high-voltage RF capacitor peak voltage and temperature in normal operation, cold-tip warmup, tip removal, and deliberate mistuning. Confirm that the selected 1 kV components have adequate margin relative to the 1.5 kV parts described in the video.
6. Measure U2, both IRF510 gate resistors, both MOSFETs, inductors, damping resistor, buck converter, and RF capacitors after thermal equilibrium. Repeat with obstructed or failed airflow.
7. Test EMI with the final enclosure and cable in all intended supply configurations. Record the required relationship among circuit ground, enclosure, output connector shell, bench-supply earth, and optional protective earth.
8. Add fan and enclosure cleaning to maintenance instructions if forced-air operation remains normal.
9. Verify that stored energy has a safe discharge path and document how a technician confirms discharge before touching the RF and power-supply sections.
10. Treat the final schematic, PCB, BOM, hand-assembly instructions, firmware revision, coil geometry, and tuning results as one validated configuration. Do not infer validation from the video's successful modified unit when any of those items differ.

## Bottom line

The video supports the two-stage 13.56 MHz architecture, but it does not validate a blind reproduction of the published files. Its strongest evidence is that a carefully corrected unit survived years of demanding use. Its strongest warnings are about oscillator layout, long-trace ringing, counterfeit gate drivers, inadequate cooling, grounding/EMC, difficult power-supply construction, and current-transformer polarity.

Hot-Wand already incorporates several of the narrator's practical conclusions: external DC power, a different sourced gate driver, named components, enclosure-assisted MOSFET cooling, forced air, and a conservative latched tip-disconnect fault. The remaining high-priority gaps are physical placement of the RF-clock damping resistor, explicit current-transformer polarity control, crystal-return validation, proof that 1 kV RF capacitors have enough margin, and EMC testing without protective earth.
