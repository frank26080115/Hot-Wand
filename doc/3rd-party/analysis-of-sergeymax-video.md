# Analysis of SergeyMax's Soldering-Station Video

This document is a technical companion to SergeyMax's video about building, debugging, modifying, and using a homemade 13.56 MHz induction soldering station. It extracts the lessons that are easy to miss when watching the video casually, especially the changes made after the original assembly did not work reliably.

The analysis is based on the local [English subtitle transcript](sergeymax_video_subtitles.txt), with approximate times taken from the accompanying [SRT file](sergeymax_video_subtitles.srt). It concentrates on the electronics, RF construction, thermal design, enclosure, and operating experience rather than the opening soldering demonstration or the extended survey of cartridges and handpieces.

## Source limitations

The subtitles appear to be an automatic translation from Russian. They contain mistranscriptions, inconsistent terminology, and at least one obvious numerical error. For example, they say that dividing a 27.12 MHz crystal by two produces 15 MHz; the intended carrier is 13.56 MHz. Terms translated as "clamp," "saw," and "container" often appear to mean cartridge/tip and capacitor, while "lezendrat" means Litz wire.

The video also distinguishes the narrator's build from an earlier article and PCB design. The narrator says that he sent corrections to the article's author and that the article was updated, but the subtitles do not provide a revision manifest. They do not establish which corrections entered the written article, schematic, PCB, BOM, or downloadable manufacturing files.

This document therefore distinguishes between:

- **Video report:** something the narrator says happened on his unit.
- **Engineering interpretation:** a likely electrical explanation for the reported behavior, which may be clearer than the translated wording.
- **Unresolved provenance:** a change visible or described on the working unit that should not automatically be attributed to any particular published-file revision.

## Executive summary

The video is not simply an assembly guide or a favorable long-term review. Its central lesson is that the published project was not initially a straightforward build-and-run design. The narrator describes it as raw and says that substantial debugging and physical modification were required before it became dependable. After those changes, however, his unit reportedly survived roughly three years of very heavy professional use.

The most important lessons are:

1. A 27.12 MHz crystal must be selected by mode, load capacitance, ESR, drive level, and oscillator compatibility—not frequency marking alone.
2. The crystal and its load-capacitor return form a sensitive local RF loop. Sharing their return path with power-switching current can destabilize the entire controller.
3. A long, fast-edged MCU-to-gate-driver clock trace can ring and may require series damping. Resistor placement matters as much as resistor presence.
4. Marketplace gate-driver ICs that appear functional at low frequency may be counterfeit, remarked, or simply incapable of operating correctly at 13.56 MHz.
5. Current-transformer polarity is safety-critical when its signal participates in power regulation. Reversed phase can turn negative feedback into positive feedback.
6. The original enclosure and passive cooling were inadequate for the narrator's use. External heatsinking, ventilation, and ultimately forced air were needed.
7. RF ground, connector, cable, metal enclosure, and protective-earth relationships must be intentional. The narrator observed severe interference when operating without suitable shielding and grounding.
8. High-voltage RF capacitors must be selected for dielectric, RF loss, RMS current, parasitics, availability, and measured peak voltage—not capacitance and DC voltage rating alone.
9. The compact resonant mains converter imposed difficult sourcing, transformer, thermal, and safety burdens. The narrator concluded that he would replace it with an external DC supply in a future build.
10. The corrected physical unit is evidence that the architecture can be made reliable; it is not proof that an untouched set of published files will reproduce that result.

## Modifications visible or described on the working unit

These changes are especially important because a reader could easily study a schematic or PCB without realizing that the long-lived unit in the video was physically different.

| Modification on the working unit | What the video establishes |
|:---------------------------------|:----------------------------|
| Series damping resistor cut into the MCU-to-driver clock trace | The original trace rang and required rework; the subtitles do not identify a published PCB revision containing the fix |
| Crystal load capacitors disconnected from their previous ground region and returned directly to MCU ground | Explicit hand rework associated with eliminating oscillator instability and controller crashes |
| Genuine MAX17602 installed in a different package | A procurement and package substitution after marketplace parts failed at RF; thermal coupling had to be improvised |
| External heatsink coupled to both power MOSFETs | Mechanical correction to inadequate original cooling |
| Ventilation holes and a fan added | Additional enclosure corrections after passive ventilation remained insufficient |
| Input and output physical arrangement reversed | Builder-specific wiring and enclosure change intended to improve physical routing |
| A capacitor moved off-board and connected by wires | Workaround necessitated by the rearrangement; not automatically a generally acceptable RF-layout practice |
| RF connector/chassis ground bonded to protective earth | System-level wiring decision that cannot be inferred from a PCB file alone |
| External printer-style DC supply proposed in place of the compact mains converter | The narrator's preferred architecture for a future rebuild, not necessarily the supply used during all demonstrations |

The statement that corrections were sent to the article author is useful provenance, but it is not enough to identify which downloadable files contain which changes. Anyone reproducing the project should inspect dated files and compare them with the physical corrections shown in the video.

## Architecture described in the video

The station is best understood as several interacting power converters and control loops:

- a compact, approximately 80 W resonant mains supply;
- a variable DC supply for the main RF amplifier, reportedly changing from roughly 8 V to 22 V with operating conditions;
- auxiliary conversion for the controller and gate-drive circuitry;
- a microcontroller clocked from 27.12 MHz and generating a 13.56 MHz square wave;
- a gate driver and a relatively small first RF transistor stage;
- a resonant network that recirculates much of the main MOSFET's gate energy;
- the main class-E RF stage and a multi-section output matching/filter network;
- current-derived feedback; and
- cartridge-disconnect protection.

Problems in one block can therefore appear elsewhere. Oscillator instability can crash the display and controller; current-transformer phase can drive the power converter in the wrong direction; mistuning can increase capacitor and transistor stress; and enclosure wiring can determine whether RF current remains local or becomes radiated interference.

## Problems, corrections, and design implications

### 1. The published project was not turnkey

At approximately 03:10-03:35, the narrator says that the design appeared reproducible from the article and video but required a long period of correction before it became reliably usable. He calls the original implementation extremely raw while also crediting its author for undertaking a large and difficult project.

This frames everything that follows. Component values and topology copied from the files should be treated as starting points, not proof of a production-ready implementation. A credible reproduction requires staged bring-up, current limiting, oscilloscope measurements, thermal measurements, feedback-polarity verification, and RF tuning.

### 2. Suitable high-voltage RF capacitors were difficult to source

At approximately 06:30-07:25, the narrator discusses 47 pF and 100 pF capacitors in the resonant network. The translation reports a 1500 V rating and says that suitable parts were difficult to obtain in small quantities. At approximately 10:25, he also identifies a missing 10 pF, 500 V, nominally 1206 capacitor, referred to as C47 in his files, which he salvaged from other equipment.

The exact reference designators are revision-dependent, but the broader engineering point is stable:

- A DC voltage rating does not establish RF suitability.
- Dielectric type and loss at 13.56 MHz matter.
- RMS circulating current may heat a capacitor even when peak voltage is acceptable.
- Package and mounting inductance can alter the matching network.
- Substituting the same nominal capacitance can change tuning through tolerance and parasitics.
- A part available only by the reel or through uncertain surplus channels damages reproducibility.

The video does not provide enough waveform data to derive the required voltage margin. Builders must measure the tuned circuit and relevant fault conditions rather than treating the quoted ratings as universally sufficient.

### 3. The 27.12 MHz crystal must be a suitable fundamental-mode part

At approximately 07:30-08:30, the narrator says that generic 27.12 MHz crystals often operated on the wrong mode and that he ultimately ordered a known part in quantity. The translation's language about second or third harmonics is imprecise, but the warning is valid: the frequency printed on a crystal is not a complete oscillator specification.

Important parameters include:

- fundamental versus overtone mode;
- specified load capacitance;
- equivalent series resistance;
- allowable drive level;
- shunt and motional capacitance;
- startup margin; and
- compatibility with the microcontroller oscillator circuit.

A controller can report that its external oscillator is running without proving that the frequency and mode are correct. Frequency, startup, and stability should be checked independently across supply, temperature, and component tolerance.

### 4. Counterfeit or remarked MAX17602 drivers failed at RF

At approximately 09:05-10:10, the narrator reports that marketplace MAX17602 devices appeared to work near 1 MHz but merely heated at approximately 13 MHz. He concluded that they had been remarked. A genuine device in a different package worked, although loss of the intended exposed-pad thermal path forced him to couple heat into the enclosure by another method.

This is a useful warning about both procurement and testing. A device that toggles a logic waveform at low frequency may still have excessive output resistance, transition time, internal cross-conduction, propagation asymmetry, or dynamic dissipation at 13.56 MHz. Package markings and a low-frequency bench test do not establish authenticity or RF performance.

For any gate-driver substitution, the meaningful checks are the driven waveform, supply current, driver temperature, switching symmetry, propagation behavior, and performance under the real capacitive load at continuous carrier operation.

### 5. Core material matters even when inexpensive substitutes appear to work

At approximately 11:00, the narrator describes powdered-iron rings with relative permeability around 8.5, referred to as material number 6. Imported originals were expensive, so he bought inexpensive substitutes and reports no trouble with them in his unit.

That is evidence for one batch and one finished assembly, not proof that any similarly colored marketplace core is equivalent. Relevant properties include dimensions, permeability, loss versus frequency, saturation behavior, temperature coefficient, and winding construction. Paint color is not a material specification.

Substitute cores should be validated by finished inductance, waveform, temperature, efficiency, and operating margin. A core that produces the correct low-signal inductance can still be unsuitable under RF voltage and current.

### 6. The compact resonant mains supply was impressive but unattractive to reproduce

At approximately 11:45-14:30, the narrator praises the compact approximately 80 W supply while describing several practical costs:

- an expensive and difficult-to-source resonant-controller IC;
- unexplained controller heating at idle, improved with an added heatsink;
- dependence on particular transformer core and winding choices;
- impractical small-quantity sourcing of the required Litz constructions; and
- substantially greater design, insulation, and mains-safety burden than an external supply.

His conclusion is unusually direct: if rebuilding the station, he would omit that section and use an external approximately 32 V printer supply.

This should be read as a system-design lesson, not a universal 32 V recommendation. An external certified supply can move much of the mains isolation, thermal, sourcing, and compliance problem out of a small RF enclosure. Its voltage and transient behavior must still suit the downstream design.

### 7. The MCU-to-driver clock trace rang and needed damping

At approximately 15:12-15:25, the narrator says that a resistor had to be inserted into the long microcontroller-to-driver trace because it rang. The subtitles call this a low-frequency output, but it carries the fast-edged 13.56 MHz clock. Signal-integrity behavior is governed chiefly by edge rate and interconnect delay, not merely by the fundamental frequency.

The likely correction is series damping. If the resistor is intended as a conventional source termination, it belongs close to the MCU output so that the resistor plus the driver's output impedance approximately matches the trace impedance. The initial wave is launched at a reduced amplitude; the reflection from the high-impedance receiver returns to the source and is absorbed rather than repeatedly reflected.

A resistor placed near the receiving input can still be useful. It isolates input capacitance and can reduce the ringing actually seen by the receiver. It is not electrically identical to source termination, however, and may leave larger reflections on the trace itself. The appropriate topology and resistance should be chosen from measurements at both ends of the interconnect.

The video establishes that the narrator added a resistor to his board. It does not establish its exact value, its physical relationship to the source pin, or which published PCB revisions include it.

### 8. Crystal load-capacitor grounding was the most important stability correction

At approximately 15:25-16:44, the narrator calls the crystal region his most important correction. On his board, the two load capacitors returned through a ground region carrying noisy power or RF current. The oscillator became unstable, the display showed garbage, and the station stopped operating.

He cut the capacitors away from that return, mounted them directly at the crystal, and connected their common return directly to the microcontroller ground pin. He reports that the faults then disappeared.

The engineering lesson is not to create an isolated ground island with no controlled reference. It is to make the crystal loop physically small and ensure that its return joins MCU ground locally, without sharing appreciable impedance with gate-driver, converter, or RF power currents. A net called `GND` can still have enough local inductance and voltage gradient to disturb a small-signal oscillator.

Useful validation includes cold start, repeated reset, supply variation, temperature variation, maximum RF output, abrupt load changes, display activity, and converter transitions. A stable oscillator on an idle bench is not the worst case.

### 9. The original thermal design was inadequate

At approximately 16:50-17:55, the narrator says that the original enclosure did not remove enough heat. He added an external heatsink coupled to the two power transistors, drilled ventilation holes, found passive ventilation insufficient, and finally added forced-air cooling. The final temperature was acceptable, but dust accumulated and required cleaning approximately every six months.

Several broader requirements follow:

- Thermal behavior must be tested with the final enclosure assembled.
- Power-transistor case temperature alone does not cover gate drivers, converters, RF capacitors, resistors, and magnetic components.
- Forced air creates a maintenance requirement.
- A stalled fan, clogged opening, or dust layer is a foreseeable fault condition.
- Full-power cold-start and recovery intervals can dominate peak temperature even when average operating power is modest.

The long service life reported later in the video applies to the unit after these cooling modifications, not to the original passive enclosure.

### 10. Connector and enclosure arrangement affected wiring and serviceability

At approximately 17:55-18:55, the narrator explains that he reversed the physical arrangement of the input switch and RF output because the original arrangement forced inconvenient wiring across the enclosure. The rearrangement improved some routing but caused a large capacitor to no longer fit the board, so it was connected remotely by wires.

The exact rearrangement is specific to his enclosure, but the design lessons are general:

- Place the RF output connector near the matching network.
- Keep mains or DC input wiring away from RF output structures.
- Check component, heatsink, connector, and lid clearances before ordering boards.
- Include cable bend radius and service access in the layout.
- Avoid remote leads on capacitors carrying large switching or RF current unless measurements demonstrate that the added inductance is harmless.

The transcript says that the displaced capacitor carried little stress, but it does not identify the node clearly enough to generalize the workaround.

### 11. Chassis bonding and protective earth were important on his unit

At approximately 18:55-19:20, the narrator says that he bonded circuit ground directly to the RF output connector and protective earth. He describes the metal enclosure and grounding as essential, reporting severe interference—including monitors going blank—when the generator was operated without appropriate shielding or grounding.

This is an anecdotal EMC observation rather than a compliance test, but it is a serious warning. A 13.56 MHz generator delivering tens of watts, together with its handpiece cable, can become an effective unintended transmitter when common-mode current is not controlled.

The PCB, connector shell, cable shield or return, metal enclosure, protective earth, and DC-supply earth must be treated as one RF system. The best connection scheme depends on whether the supply is floating or earth-referenced and on where common-mode current actually flows. Bonding should be deliberate and measured; merely adding an earth wire somewhere does not guarantee lower emissions.

### 12. Current-transformer phase could create dangerous positive feedback

Near 35:40, the narrator says that he initially reversed the current transformer's phase. The translation is awkward, but the reported result is clear: the cartridge heated to a dull red before he intervened.

This is one of the most safety-critical observations in the video. The current transformer is not merely a display sensor. Its phase-sensitive signal affects power regulation. Reversing a winding can cause a loop intended to reduce power to increase it instead.

The construction information for such a transformer should explicitly define:

- primary direction;
- secondary start and finish leads;
- dot convention;
- connector orientation;
- expected detector polarity; and
- a current-limited, low-power verification procedure.

The decisive test is behavioral: increasing the sensed condition that represents greater heating must drive the power command in the safe direction. Firmware protection should not be assumed to correct a fast analog positive-feedback error.

### 13. Live cartridge removal was supported but remained electrically stressful

At approximately 24:25, the narrator demonstrates removing the cartridge while the station is active. The controller detects the open cartridge, reports an error, and resumes after reinsertion. He says that the protection worked reliably during his ownership.

The demonstration proves that protection operated on that occasion; it does not prove that live removal is harmless. The RF network contains stored energy when the load disappears, so node voltage can rise before a detector or controller responds. Tip-disconnect testing should therefore include the first unloaded transient, not only the later shutdown state.

The user interface should also make an intentional policy choice: automatic recovery is convenient, while latched recovery can reduce the chance of an unexpected restart during handling. The video shows one implementation rather than settling that tradeoff for every design.

### 14. Real operating duty cycle was much lower than the nameplate power

The narrator reports:

- less than 10% displayed power while idling with the handpiece active;
- approximately 2-3% in magnetic-stand sleep mode;
- 100% mainly during cold start or recovery after leaving the stand; and
- no more than approximately 50% during his ordinary soldering work.

These observations help explain why an approximately 80 W source can coexist with much lower long-term enclosure dissipation. They do not justify underrating parts. The RF path, gate driver, feedback network, supply, and thermal system must survive full-power warmup, repeated recovery cycles, and abnormal loads.

### 15. Long service validated the corrected unit, not untouched files

At approximately 19:25 and again near the end, the narrator says that the corrected station operated for roughly three years in near-continuous professional use without repeated power-transistor failures. That is valuable field evidence for the underlying architecture and for his corrected physical implementation.

It is not evidence that:

- the original unmodified board is reliable;
- every published file contains the hand corrections;
- arbitrary replacement crystals, drivers, capacitors, or cores are equivalent;
- the thermal result transfers to a different enclosure; or
- EMC behavior transfers to a different cable, connector, supply, or grounding arrangement.

The defensible conclusion is that the design can be made robust and useful, but the physical fixes, sourcing decisions, tuning, enclosure work, and operating environment are part of the validated configuration.

## Operational and mechanical observations

The latter half of the video contains several product-level lessons beyond the central PCB corrections:

- The original handpiece grip and strain-relief rubber degraded after extended use, although the defects were mostly mechanical or cosmetic.
- The soft silicone coaxial cable was heavy enough to pull the light handpiece from the user's hand when it hung over the bench edge. Cable routing and stand placement affect ergonomics.
- Silicone cable resisted accidental soldering heat but collected dirt and required cleaning.
- Genuine Metcal cartridges reportedly heated faster than compatible Thermo-Tronics cartridges in this station.
- A large high-temperature cartridge was rarely useful and took roughly ten seconds to heat, versus roughly four seconds for ordinary cartridges.
- The cartridges showed little natural electrical wear after three years. The damaged examples resulted from setup error or contact with a charged power-supply capacitor rather than ordinary service.
- The charged-capacitor incident demonstrates the need for a deliberate discharge path and verification procedure during servicing. Switching off input power is not enough.
- The magnetic stand reduced idle power substantially and made aggressive sleep behavior convenient during normal work.

## Questions to carry into any reproduction or derivative design

The video is most useful when it changes what a builder checks. Before treating a reproduction as validated, answer at least the following:

1. Which dated schematic and PCB revision are being used, and which of the narrator's hand corrections are actually present?
2. Is the crystal a documented fundamental-mode part compatible with the oscillator, and has its frequency and startup margin been measured?
3. Do both crystal load capacitors have a short local return that avoids power and RF current?
4. Is the MCU-to-driver clock clean at both the source and receiver, and is any damping resistor placed and valued from measurements?
5. Is the gate driver authentic and thermally credible at continuous 13.56 MHz under its real capacitive load?
6. Are high-voltage capacitors suitable for RF voltage and circulating current, including mistuning and load-removal transients?
7. Are core material, finished inductance, winding geometry, and temperature verified rather than inferred from appearance?
8. Is current-transformer polarity unambiguous, and does a low-power test prove that feedback acts in the safe direction?
9. Have the main stage and matching network been brought up with current limiting and measured waveforms rather than immediately connected at full power?
10. Has thermal equilibrium been tested in the closed enclosure, including fan failure, obstructed airflow, and repeated cold starts?
11. Has EMC been tested with the final cable, connector, enclosure, supply, and earth arrangement?
12. Is stored energy discharged predictably before the unit is handled or serviced?

## Bottom line

SergeyMax's video should be read as the history of a difficult design that became dependable through debugging, rework, careful sourcing, RF measurement, thermal modification, and years of practical use. The success of the finished station is real evidence in favor of the architecture, but it cannot be separated from the corrections made to the physical unit.

The most consequential warnings are the crystal-return layout, fast-clock ringing, counterfeit gate drivers, current-transformer polarity, high-voltage RF capacitor selection, inadequate original cooling, and uncontrolled RF grounding or shielding. Anyone studying the associated article or design files should keep the video beside them and ask which of those lessons the particular file revision actually contains.
