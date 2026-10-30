# Shipping test plan: 13.56 MHz Hot Wand

## Purpose and release decision

Screen each assembled unit before giving it to a friend, using only the default dummy load, real handpieces/cartridges, the OLED debug view, the input supply's voltage/current indications, and a dedicated soldering-tip thermometer. No oscilloscope, multimeter, RF power meter, or additional electrical measurements are required by this procedure.

Test the finished enclosure, heatsink, fan, cables, firmware, and settings that will actually be supplied. A pass applies to the recorded combinations and operating conditions, not to every cartridge that happens to fit a connector.

| Classification | Meaning and disposition |
| --- | --- |
| **NORMAL / PASS** | All applicable acceptance criteria pass, heating and stand behavior are repeatable, and no stop condition occurs. Eligible for gifting within this screening scope. |
| **UNDERPERFORMING / HOLD** | Heating is slow or low, losses or temperatures are elevated, stand behavior is poor, or a result cannot be resolved with the available indications. Do not gift; investigate and repeat the affected tests. |
| **DANGEROUS OR SUSPECTED LIMIT FAILURE / STOP** | A conservative stop threshold, uncontrolled heating, or abnormal physical behavior occurs. Switch the input supply off immediately. Do not continue or gift the unit until repaired and retested. |

The numerical shipping bands below are **proposed conservative screening rules**, not measured component ratings or a claim that these units have already been characterized. In particular, MOSFET bead temperatures are not junction temperatures. A low displayed temperature does not prove a good gate waveform, good thermal attachment, or absence of a local hot spot. This procedure cannot establish RF waveform quality, RF output power, protective-earth continuity, tip leakage, or formal electrical/EMC safety compliance. Do not describe a pass as certification of those properties.

## Equipment and fixed setup

- Input supply: **28.0 V setpoint, 5.0 A current limit** at the start of every powered test. Keep these settings throughout. A 5 A setting is a ceiling, not a required current draw. Do not raise it to make a test pass.
- Available input indications: one decimal place and approximately 10% accuracy. Record displayed values as displayed; do not infer extra precision.
- Default dummy load: **50 ohms resistive in series with 146 nH**, with the inductor bypass open and no added capacitors. Use its large heatsink and air cooling. The project describes two 100 ohm, 50 W resistors in parallel; see the [dummy-load construction and study](../../../doc/design-study/current-transformer.md#fooling-around-with-dummy-load). This plan does not require modifying that fixture. Keep any separately powered load fan outside the DUT input-current measurement.
- All intended 13.56 MHz handpiece/cartridge combinations, including each physical cartridge being given away. Use the correct magnetic stand for each combination. Include every intended temperature series and representative tip geometries; retain a separate record for each combination. See the [compatibility matrix](../../../doc/build-guide-13mhz/compatibility-matrix.md).
- Dedicated tip thermometer, with its sensor in good condition; solder for the thermometer contact; timer; and a nonmagnetic, heat-resistant support that leaves the hot tip free in air without touching another object.

Work in an approximately 20-30 C room, with normal ventilation and unobstructed airflow. Record estimated room temperature and the fan setting. Both external thermistors must be fitted and bonded to their intended MOSFETs; record the actual THERM1/THERM2-to-device assignment rather than assuming their order.

Change loads, cartridges, or RF connectors only with supply output off. Normal firmware can generate RF automatically; the button is not an RF-off switch. Keep fingers away from the board, RF output, connector center conductors, and dummy-load conductors whenever powered. Grip only the insulated handpiece. Use supply output OFF as the immediate stop control. Debug mode has no manual or automatic sleep, and a terminal fault can stop the fan as well as RF. Keep every powered interval attended.

## Firmware and debug controls

Use normal production firmware, with no standalone bring-up test selected in `test_run()` and no `stm32f042_test` simulation image. The simulation image inhibits physical RF and cannot qualify an assembled amplifier. This plan does not require changing firmware, adjustment pots, coils, or protection thresholds.

1. Connect the selected load while unpowered, then enable the 28 V / 5 A supply.
2. Dismiss the splash if present. Make ten runtime short presses, with no more than five seconds between them, to enter debug mode. Splash dismissal does not count. The presses still change power selection and can produce the existing six-press message, so entry itself is a powered part of the test.
3. Once debug is active, use short presses to select the desired mode. The sequence is 100 -> 75 -> 50 -> 75 -> 100, repeating. Check the final `M:` indication after any message clears; do not assume ten presses left a particular mode selected.
4. Debug remains active until reset/power cycle. A long hold clears the five stored maxima without changing power selection or entering sleep. Maxima immediately resume accumulation; they need not remain visibly zero.
5. Photograph or transcribe maxima before clearing them, changing loads, or switching power off. A shutdown/reset can make the previous peaks unavailable.

The display is:

```text
>28V       rounded input voltage; > is an input marker, not a comparator
M:100      applied SPORT mode; M:75 is MEDIUM, M:50 is ECO
NOW
20.0V      buck output voltage
3.2A       existing buck current-sensor measurement
64.0W      calculated buck-stage electrical power
42C        THERM1
38C        THERM2
MAX
21.0V      maximum observed filtered buck voltage
3.5A       maximum observed filtered current
70.0W      maximum observed calculated power
45C        maximum THERM1
40C        maximum THERM2
```

With default `PWRLVL_POWER_LIMIT_ENABLED=1`, all three modes request continuous RF while buck PWM enforces measured ceilings of 100 W, 75 W, and 50 W. Short presses retain the same back-and-forth selection sequence. Thermal or low-input derating applies the 50 W ECO ceiling.

The independent 5.2 A current limit and power-rise response share one attenuation value. Full-scale rises relative to the previous completed 250 ms history segment are 20 W in SPORT, 25 W in MEDIUM, and 30 W in ECO. The largest request wins. Attenuation releases one count per 50 ms only below both limits. Optional duty-cycle builds (`PWRLVL_POWER_LIMIT_ENABLED=0`) need separately identified cycling criteria; the default procedure below checks wattage regulation.

V/A/W have one decimal place; temperatures are whole degrees. The five maxima are independent and may occur at different times. **Peak power is the maximum calculated power sample, not maximum voltage multiplied by maximum current.** These are foreground-observed, filtered peaks, not captured switching spikes. Debug refresh is nominally 5 FPS; the 250 ms graph segments and peak collection continue independently. NOW is not a time-averaged power meter. Record settled readings in each mode after attenuation and converter settling; MAX retains transient peaks and does not establish a sustained ceiling violation. Messages and warning overlays take display priority; peak collection continues. The high-voltage overlay can temporarily cover lower MAX readings.

OLED watts are neither whole-board input power nor RF power delivered to the load. At a recorded instant, estimate input watts as the supply's simultaneous voltage indication multiplied by its current indication. Record the input current's highest *observed* value separately: OLED MAX does not capture input voltage or input current, and human observation cannot guarantee their true peaks.

For rough comparisons, a conservative uncertainty envelope for either input indication `x` is `max(0, 0.9*x - 0.05)` through `1.1*x + 0.05`. Multiply the voltage endpoints by the current endpoints for an input-power interval. This allows for both stated percentage error and display rounding; it is not an instrument calibration. Do not calculate precise conversion efficiency or MOSFET dissipation from the difference between input watts and OLED watts.

## Acceptance limits and stop rules

Apply these throughout every test. For a time-qualified threshold, watch NOW and use the timer; MAX alone cannot establish how long an excursion lasted. Do not keep a unit running to find out whether firmware eventually shuts it down.

| Observation | NORMAL / PASS band | UNDERPERFORMING / HOLD | STOP immediately |
| --- | --- | --- | --- |
| THERM1 and THERM2 | Both plausible when cold; each <=70 C during the loaded/idle tests; final five-minute rise <=2 C | Either >70 but <80 C, failure to stabilize, or a sensor that stays implausibly fixed | Either >=80 C; thermal rise accelerating; lost/broken sensor attachment or sensor warning |
| Buck voltage | <=24.0 V | >24.0 but <25.0 V: insufficient screening margin; investigate | >=25.0 V on NOW or MAX, or `VOLT SPIKE FAULT` |
| Buck current | Live reading normally <=5.2 A; record brief higher peaks | Brief peaks >5.2 but <5.8 A, with no sustained excess: review before release | NOW >5.5 A for 1 s, MAX >=5.8 A, or `SHORT CIRKT FAULT` |
| Buck power | Repeatable continuous-SPORT readings within the qualified fixture/reference band below | Unexplained deviation from that band or a persistent failure to regulate the selected 50/75/100 W ceiling | Apply the independent voltage, current, temperature, supply and physical stop rules; investigate persistent excess above the selected ceiling after transient settling |
| Supply and input behavior | Supply remains in constant-voltage operation, without a falling-voltage trend | Current limiting, supply droop, an implausible input indication, or incompatible instruments: test invalid; do not blame the unit until supply/cabling are checked | Input current indication >=4.5 A for 1 s, or any supply trip: remove power; do not raise the 5 A limit |
| Physical behavior | No new damage or abnormal behavior; fan behaves according to the saved profile | Fan setting unsuitable for the enclosure, unexplained noise, intermittent connector/display behavior | Smoke, electrical smell, arcing, discoloration, softening, stalled required fan, or uncontrolled tip heating |

The 80 C bead-temperature and 4.5 A indicated-input stops are conservative **operator limits for this screen**, not component absolute maximum ratings. The latter reserves approximate headroom for the coarse input-current reading; even a normal 100 W attempt may be supply-limited and therefore inconclusive. Do not exceed these operator limits to obtain a full-power number.

Mode-specific observations are:

| Applied mode | Default request | Interpretation for this screen |
| --- | --- | --- |
| M:100 / SPORT | Continuous RF with a 100 W buck-power ceiling while interlocks pass | Use settled readings for the main dummy-load reference comparison; allow initial rise attenuation to settle |
| M:75 / MEDIUM | Continuous RF with a 75 W buck-power ceiling | Verify settled power regulates at or below 75 W within measurement tolerance |
| M:50 / ECO | Continuous RF with a 50 W buck-power ceiling | Verify settled power regulates at or below 50 W within measurement tolerance |

Clear peaks after selecting a mode so earlier readings are not attributed to the new mode. Allow converter and rise-response settling before comparing measured buck power with its selected ceiling. Input power includes converter losses and differs from buck power; fixture capability still requires comparison with a qualified reference.

### Intentional derating versus poor performance

Current defaults in [conf.h](../../inc/conf.h) are a 5.2 A current ceiling, 5.8 A short-circuit threshold qualified for five seconds, a disabled-by-default buck-voltage-spike detector (`PWRMGT_BUCK_VOLTAGE_SPIKE_MV=0`), thermal derating above 80 C, and thermal shutdown above 110 C qualified for one second. The shipping stops above act sooner where appropriate. The displayed sensors do not expose all supervision inputs. The manual 24/25 V screening bands remain operator criteria even though the firmware spike detector is disabled; do not expect a `VOLT SPIKE FAULT` to enforce them.

The optional **virtual thermal/energy limiter is disabled by default** (`LEAKY_BUCKET_ENABLED=0`). Normal firmware therefore does not force M:50 merely because a modeled power-energy budget was exceeded; measured thermistor/MCU temperatures, mode-based wattage ceilings, power-rise attenuation, and current limits remain active. If explicitly enabled, the model can derate sustained buck power near 100 W in about eight seconds or near 80 W in about 20 seconds, even with cool THERM1/THERM2 readings. Clearing MAX does not reset that optional model. The model is optional; its predicted timings are not runtime limits for the default build or guarantees for physical thermal behavior.

At 28 V there should be no high-input-voltage warning (default starts at 32 V). Any terminal fault during an ordinary connected-load run is a HOLD/failed run; remove power and record it. A reset is not a repair. A trip associated with voltage, current, temperature, or abnormal physical behavior is a STOP condition. A `POWER / LOSS / FAULT` screen now automatically retries only after filtered input has remained strictly above 14 V for one continuous second; a dip to 14 V or less restarts the wait. Repeated recovery is possible without a button press. Any such trip during an ordinary shipping-test load transition remains a HOLD for supply-path/regulation investigation, even if operation resumes. Keep the input OFF control available: the stopped screen is no longer a persistent output-off lockout. The sustained-decline power-loss detector is also disabled by default (`PWRMGT_POWER_LOSS_SUSTAINED_ENABLED=0`); the independent fast-drop detector remains controlled by `PWRMGT_POWER_LOSS_FAST_DROP_ADC_COUNTS`. The absolute undervoltage screen `LOW / VOLT / FAULT` also automatically recovers after the same continuous >14 V / one-second interval. A USB source can therefore drop out and retry repeatedly; a recovered event remains a HOLD for this shipping screen rather than evidence that the supplied source/cable passed the intended load.

## Procedure

### S1. Cold inspection and startup

1. With power off, inspect the finished enclosure, insulated MOSFET mounting, thermistor bonds, fuse, RF connector, cable strain relief, fan clearance, and accessible metal. Do not proceed with visible damage or loose hardware.
2. Fit the default dummy load. Start with the supply at **28.0 V / 5.0 A**. Enter debug promptly, select M:50 for the initial checks, and allow startup messages to finish. S2 explicitly selects continuous SPORT for its numerical reference test. If necessary, select the intended fan setting in setup with power removed between configuration and this test; never improve cooling only for testing.
3. Record both initially cold thermistor readings. They should be within approximately 10 C of the estimated room temperature and each other. A cold 0 C, grossly high reading, missing-sensor message, or visibly detached sensor is a HOLD; do not rely on subsequent temperature limits.
4. Verify that the input indication is compatible with the supply within its coarse uncertainty and the OLED's whole-volt rounding. Expect `>28V`, but do not require exactly that string as a calibration check.

### S2. Default dummy-load operation

#### Expected power draw

Use **M:100 / SPORT as the main numerical comparison**, at **28.0 V / 5.0 A** with the default 50 ohm + 146 nH dummy load. A continuous mode makes the available slow input indications and OLED NOW values useful for a settled comparison. Wait for initial power-rise attenuation and converter settling before recording. Current limiting and all other protections remain active.

The former M:50 target of 40.0-52.5 W and input band of 1.6-2.7 A were derived from an assumed enforced 50 W ceiling. They are withdrawn for the current firmware. No validated absolute 28 V power band is established by this document. A healthy-unit observation at another input voltage is not a 28 V acceptance measurement, and the resistor's 100 W rating is not an expected draw.

Before releasing units, record a qualified healthy reference on this exact fixture at 28 V, with the same firmware, buck adjustment, enclosure, fan setting and approximately matched thermal conditions. Record its settled SPORT buck power `P_ref`, simultaneous input voltage/current and thermistor readings. Qualification must include the applicable assembly/protection checks and repeatable passing cartridge tests, not merely designation as a reference. The reference itself must pass all general stop and thermal rules. If no qualified reference or pre-established fixture band is available, collect characterization data but mark numerical power capability **HOLD**; do not invent a band from the mode labels.

| Settled SPORT measurement | NORMAL / PASS | UNDERPERFORMING / HOLD | STOP |
| --- | --- | --- | --- |
| OLED NOW buck power | Within a pre-established fixture band; provisional comparison band **0.8*P_ref through 1.2*P_ref** after repeated settled observations | Below 0.8*P_ref: underperforming; above 1.2*P_ref: unexpected operating point or calibration/loss issue requiring review | Any independent general stop condition; exceeding 100 W alone is not a firmware wattage-limit failure |
| Indicated input current | Repeatable and consistent with the reference, allowing the stated instrument uncertainty and measured input voltage | Repeatable >20% difference that remains unexplained after uncertainty review; high input with normal/low buck watts suggests excess loss | **>=4.5 A for 1 s**, a supply trip, or another general stop condition |
| MOSFET temperatures | Both <=70 C and final five-minute rise <=2 C | >10 C hotter than the matched reference, or failure to stabilize, even if watts appear normal | Any general thermal stop |

The +/-20% comparison band is a provisional shipping rule, not a firmware control tolerance or an RF-output specification. Establish and record the band before judging a candidate; do not relax it to pass that unit. Use simultaneous input voltage/current to compare input watts when actual voltage differs. Near a boundary, repeat measurements and apply the meter uncertainty; unresolved results are HOLD. A brief MAX peak does not establish steady performance.

#### Test sequence

1. After S1, select M:100, allow initial settling, clear maxima, and run for **10 minutes** with fixture air cooling operating. Record simultaneous NOW buck watts and input voltage/current at **30 s, 1 min, 5 min, and 10 min**, plus the applied mode. Record MAX at the end; do not clear peaks mid-run. Stop sooner if any general limit is reached.
2. Compare the settled observations with the reference/band above. NORMAL requires repeatable power and input demand, no unexplained fault, and no continuing drift. At continuous SPORT a persistent large oscillation is a HOLD for investigation; do not choose only favorable samples. If the unit applies ECO because of derating, record that event and do not compare its 50 W limited readings against the SPORT reference.
3. Apply all general thermal limits. Both final MOSFET readings must be **<=70 C**, with a rise **<=2 C over the final five minutes**. If still rising, extend to 20 minutes under supervision; failure to stabilize is a HOLD. Expected power does not excuse excess temperature.
4. Select M:75, clear peaks after messages settle, and observe for **two minutes**. Record NOW, MAX, input indications and temperature trend; verify settled buck power regulates at or below 75 W within measurement tolerance. Repeat at M:50 for **two minutes**, checking its 50 W ceiling. RF should remain continuous while interlocks pass. Transient peaks require investigation separately from sustained regulation; the slow instruments do not characterize transient duration.
5. Compare reduced-mode patterns with the qualified reference under matched conditions. Sustained operation should be reduced without abnormal noise, faults or worsening thermal behavior. Unexpected absence of any reduced-operation pattern, repeated faults, or rising temperature that violates the general rules is a HOLD/STOP as applicable. Do not require exact 0.75/0.5 power ratios: startup and analog feedback can alter the operating point.
6. Return to M:100, allow settling, and confirm the original continuous-mode reading recovers within its comparison band. A persistent unexplained change is a HOLD. Retain observations from each mode separately; mode changes and peak resets do not reset the optional virtual thermal model.

The project study's MX-5200 displayed value of 40 describes a different supply's load-power indication; it is not the basis for equating 40 W of RF output with 40 W on this OLED. Passing the bands above establishes the chosen **buck-stage and whole-board power-draw screen** only. It does not measure RF conversion efficiency or establish 100 W RF output.

### S3. Real cartridge in its magnetic stand

Repeat S3-S5 for every intended handpiece/cartridge combination. Change cartridges with power off and let the unit/cartridge cool before each cold run.

1. Fit the cartridge and place the handpiece fully in its intended magnetic stand. Start again at **28.0 V / 5.0 A**, enter debug, and select M:100. A correct stand should suppress demand without needing a manually low mode.
2. After **30 seconds**, clear maxima and observe for **two minutes**. Record NOW, MAX, and input readings at the beginning and end of this window.
3. Proposed NORMAL stand-idle screen: buck power <=5.0 W, indicated input current <=0.4 A, no sustained temperature rise, and no inappropriate tip fault. These low-demand bands are screening choices, not manufacturer guarantees for every handpiece/stand. Unexpected readings require a HOLD even if a different stand/cartridge combination behaves well.
4. Buck power >5.0 W or input current >0.4 A is a HOLD for ineffective standby or excess driver/board loss. Buck power >10.0 W or input current >=0.8 A persisting for five seconds after settling is a conservative **STOP** for unexplained idle dissipation. Other general stops always take precedence.
5. Do not infer that the cartridge is touch-safe from low power, and do not use a fingertip to check it. A previously hot cartridge can remain hot in stand.

If buck watts are low but input current is high, the extra input power is not explained by measured buck output. If both are high, standby/load feedback is also suspect. Neither observation identifies the faulty component without further diagnosis. Do not assign a high-loss unit to a supposedly easier handpiece as a workaround.

### S4. Lift-out heating and tip-temperature check

1. With the cartridge initially cool and seated in stand, verify that M:100 is actually applied. Clear maxima while it remains in stand, then lift it by the insulated grip and start the timer. Use the nonmagnetic support so the tip is in free air, away from the stand's magnet and without thermal contact.
2. Observe input current and NOW continuously during initial warm-up. Preserve MAX across the complete lift-out/warm-up interval. Record at approximately 5 s, 15 s, 30 s, and 60 s, and when any mode change or warning occurs.
3. Measure the working face using the dedicated thermometer's specified solder-contact method. Record time to reach the applicable lower working temperature bound. Make three repeatable measurements after settling, removing thermometer contact between readings so it does not become the continuous thermal load for the later free-air soak.
4. Proposed warm-up classification for ordinary single-tip cartridges: reach the applicable lower bound within **30 s** = NORMAL; **>30 s through 60 s** = UNDERPERFORMING/HOLD; not reached by 60 s = failed heating/HOLD. An unusual high-mass geometry needs a documented, pre-established model-specific time from a healthy reference, rather than relaxing its limit after it fails.
5. Once the tip is hot, power demand should fall from its cold-heating level and settle. Failure to fall, an ongoing temperature increase, or persistent mode limiting in free air is a HOLD; any stop threshold ends the run. If the optional model is enabled and a short warm-up caused virtual derating, record it and allow supervised recovery; repeated failure to reach temperature still fails this screen.

Use the exact cartridge's published temperature range and the thermometer's stated uncertainty `U`. A reading interval `measured +/- U` entirely inside the published range is a clear temperature PASS; overlap with an edge is HOLD for measurement uncertainty, not an automatic pass. Entirely below the lower bound is UNDERPERFORMING. Entirely above the upper bound, or a continuing increase toward/past it, is a STOP for suspect overtemperature. Repeat a questionable contact reading promptly without continuing an uncontrolled heating run.

Useful manufacturer references, not interchangeable ratings for all brands:

| Cartridge series | Published working-temperature reference |
| --- | --- |
| Hakko T31/T31B-03 | 345 +/-20 C, or 325-365 C |
| Hakko T31/T31B-02 | 395 +/-20 C, or 375-415 C |
| Hakko T31/T31B-01 | 445 +/-20 C, or 425-465 C |
| Thermaltronics M7CH176 | 350-398 C; use the individual datasheet for other M cartridges |
| Metcal STTC/SMTC; EasyBraid EBM | Record the exact series/geometry's published range before testing; do not substitute a Hakko range or interpret a heater maximum as a measured working-face target |

The Hakko bands come from its [induction-cartridge temperature tolerance table](https://kb.hakkousa.com/Knowledgebase/12017/Minimum-Temperature-Accuracy-for-InductionType-Tip-Cartridges). The Thermaltronics range and method/geometry caveat come from the [M7CH176 datasheet](https://www.thermaltronics.com/downloads/datasheets/M7CH176_Technical_Datasheet.pdf). If the cartridge range or thermometer uncertainty cannot be established, record the actual measurements but mark the temperature criterion **HOLD**. Do not claim all 700-series tips must read exactly 390 C or exactly 400 C.

### S5. Free-air MOSFET-temperature soak and return to stand

1. Leave the fully heated tip in free air on the nonmagnetic support, at the intended highest operating mode, for **15 minutes**. Keep the same MAX window as S4 so cold warm-up and soak peaks are retained. Record NOW and input readings at 1, 5, 10, and 15 minutes after initial temperature settling.
2. NORMAL requires tip temperature within its accepted band, decreasing then stable power demand, both MOSFET readings <=70 C, and <=2 C rise between minutes 10 and 15. If temperatures have not stabilized, extend to 20 minutes while attended; continuing rise or thermal derating in settled free air is a HOLD. The general 80 C stop still applies. Do not run longer merely to see whether shutdown occurs.
3. Photograph/transcribe all five MAX readings and the highest observed input current. Briefly recheck tip temperature at the end. Repeatability against a healthy unit is useful, but different cartridge families must have their own reference values. A hot Hakko or Metcal cartridge is not assumed to be an easier amplifier load solely because of its brand.
4. Return the handpiece to its magnetic stand. Power should fall toward the S3 band within **10 seconds**. Clear maxima after that transition, observe another **two minutes**, and confirm the S3 idle limits again. A failure to return to low demand is a HOLD/STOP as defined in S3.
5. Repeat the lift-out/return sequence **three times**, preserving a fresh peak window for each lift-out. One full S5 soak per combination is sufficient; the two additional cycles check repeatable heating/stand transitions. Switch input power off when finished. Do not leave a unit parked indefinitely in debug mode as a substitute for switching it off.

## Recording and final release

Keep one header per unit and one measurement row per phase/combination. Preserve failed and repeated runs rather than replacing them with the successful result.

- Unit/PCB revision, firmware revision, date/operator, fuse, fan mode, estimated room temperature, enclosure/cooling configuration, supply identity/settings.
- THERM1 device assignment and THERM2 device assignment; cold readings.
- Dummy-load identity/default jumper state; handpiece, cartridge part number, temperature series, magnetic stand, and cable identities.
- Tip-thermometer identity, sensor condition, uncertainty U, and chosen cartridge temperature range. Record an optional healthy-reference unit and its results.

| Phase / load / elapsed time | Applied M | Input V / observed A | NOW buck V / A / W | NOW T1 / T2 | MAX buck V / A / W | MAX T1 / T2 | Tip C / warm-up s | Faults / result |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| | | | | | | | | |

Release only when S1-S5 pass for the intended supplied combinations and no HOLD or STOP remains unresolved. If rated power capability was not established, retain that limitation in the record; this is an intended-use functional screen, not a 100 W RF-output acceptance test. If a unit needs a different fan setting, different enclosure, altered input voltage, or different cartridge to hide a failure, it has not passed the original configuration.

After testing, power-cycle out of debug, restore and verify the intended saved settings in normal mode, and confirm normal startup with the supplied handpiece in its stand at the same 28 V / 5 A setup. Note the intended auto-sleep setting; debug testing did not exercise it. Give the recipient the compatible stand, cartridge identification, input-supply requirements, and instructions to keep conductors covered, avoid hot tips/RF contacts, and switch off when unattended.

This plan intentionally uses current-limited 28 V / 5 A operation and does not perform the build guide's separate unlimited-input fault-injection procedures. It does not replace prior assembly/protection checks or deliberately short, disconnect, overheat, or overvolt an energized unit to prove a protection trip.

## Implementation references

- [Runtime display, peak collection, button handling, and sleep exclusion](../../src/main.c).
- [Filtered ADC conversions and calculated buck power](../../src/adc.c).
- [Power/temperature supervision and applied mode](../../src/pwrmgt.c), [buck current/rise attenuation](../../src/pwrlvl.c), and [virtual thermal limiter](../../src/leaky_bucket.c).
- [Current thresholds](../../inc/conf.h), [fan behavior](../fan-modes.md), and [implemented protection behavior](../implemented-faults.md).
