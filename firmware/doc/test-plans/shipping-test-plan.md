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
M:100      currently applied 100/75/50 W buck-stage power ceiling
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

V/A/W have one decimal place; temperatures are whole degrees. The five maxima are independent and may occur at different times. **Peak power is the maximum calculated power sample, not maximum voltage multiplied by maximum current.** These are foreground-observed, filtered peaks, not captured switching spikes. Messages and warning overlays take display priority; peak collection continues. The high-voltage overlay can temporarily cover lower MAX readings.

OLED watts are neither whole-board input power nor RF power delivered to the load. At a recorded instant, estimate input watts as the supply's simultaneous voltage indication multiplied by its current indication. Record the input current's highest *observed* value separately: OLED MAX does not capture input voltage or input current, and human observation cannot guarantee their true peaks.

For rough comparisons, a conservative uncertainty envelope for either input indication `x` is `max(0, 0.9*x - 0.05)` through `1.1*x + 0.05`. Multiply the voltage endpoints by the current endpoints for an input-power interval. This allows for both stated percentage error and display rounding; it is not an instrument calibration. Do not calculate precise conversion efficiency or MOSFET dissipation from the difference between input watts and OLED watts.

## Acceptance limits and stop rules

Apply these throughout every test. For a time-qualified threshold, watch NOW and use the timer; MAX alone cannot establish how long an excursion lasted. Do not keep a unit running to find out whether firmware eventually shuts it down.

| Observation | NORMAL / PASS band | UNDERPERFORMING / HOLD | STOP immediately |
| --- | --- | --- | --- |
| THERM1 and THERM2 | Both plausible when cold; each <=70 C during the loaded/idle tests; final five-minute rise <=2 C | Either >70 but <80 C, failure to stabilize, or a sensor that stays implausibly fixed | Either >=80 C; thermal rise accelerating; lost/broken sensor attachment or sensor warning |
| Buck voltage | <=24.0 V | >24.0 but <25.0 V: insufficient screening margin; investigate | >=25.0 V on NOW or MAX, or `VOLT SPIKE FAULT` |
| Buck current | Live reading normally <=5.2 A; record brief higher peaks | Brief peaks >5.2 but <5.8 A, with no sustained excess: review before release | NOW >5.5 A for 1 s, MAX >=5.8 A, or `SHORT CIRKT FAULT` |
| Buck power at applied ceiling L | After settling, <=1.05*L | Repeated settled readings >1.05*L but <=1.10*L; any isolated MAX >1.25*L needs review | NOW >1.10*L for 1 s, indicating suspect regulation; other stop rules still apply below this power |
| Supply and input behavior | Supply remains in constant-voltage operation, without a falling-voltage trend | Current limiting, supply droop, an implausible input indication, or incompatible instruments: test invalid; do not blame the unit until supply/cabling are checked | Input current indication >=4.5 A for 1 s, or any supply trip: remove power; do not raise the 5 A limit |
| Physical behavior | No new damage or abnormal behavior; fan behaves according to the saved profile | Fan setting unsuitable for the enclosure, unexplained noise, intermittent connector/display behavior | Smoke, electrical smell, arcing, discoloration, softening, stalled required fan, or uncontrolled tip heating |

The 80 C bead-temperature and 4.5 A indicated-input stops are conservative **operator limits for this screen**, not component absolute maximum ratings. The latter reserves approximate headroom for the coarse input-current reading; even a normal 100 W attempt may be supply-limited and therefore inconclusive. Do not exceed these operator limits to obtain a full-power number.

Power ceilings and comparison bands, rounded to the display precision, are:

| Applied mode | Ceiling L | Settled review boundary, 1.05*L | Sustained stop, >1.10*L for 1 s | Peak requiring review, >1.25*L |
| --- | ---: | ---: | ---: | ---: |
| M:50 | 50 W | 52.5 W | 55.0 W | 62.5 W |
| M:75 | 75 W | 78.8 W | 82.5 W | 93.8 W |
| M:100 | 100 W | 105.0 W | 110.0 W | 125.0 W |

Clear peaks after selecting a new mode, so an earlier higher-mode peak is not mistaken for a new violation. During cold-cartridge warm-up, record peaks and live settling separately; a brief overshoot is not evidence of a sustained limiter failure. The proposed percentages allow screening tolerance, not a new firmware regulation specification or a calibrated RF-power tolerance.

### Intentional derating versus poor performance

Current defaults in [conf.h](../../inc/conf.h) are a 5.2 A current ceiling, 5.8 A short-circuit threshold qualified for five seconds, a nominal 25 V raw buck-voltage trip, thermal derating above 80 C, and thermal shutdown above 110 C qualified for one second. The shipping stops above act sooner where appropriate. The displayed sensors do not expose all supervision inputs.

The optional **virtual thermal/energy limiter is disabled by default** (`LEAKY_BUCKET_ENABLED=0`). Normal firmware therefore does not force M:50 merely because a modeled power-energy budget was exceeded; measured thermistor/MCU temperatures, the selected power ceiling, and current limits remain active. If explicitly enabled, the model can derate sustained buck power near 100 W in about eight seconds or near 80 W in about 20 seconds, even with cool THERM1/THERM2 readings. Clearing MAX does not reset that optional model. Retain this procedure's short higher-mode observations as screening steps; they are not a model-imposed runtime limit in the default build.

At 28 V there should be no high-input-voltage warning (default starts at 32 V). Any terminal fault during an ordinary connected-load run is a HOLD/failed run; remove power and record it. A reset is not a repair. A trip associated with voltage, current, temperature, or abnormal physical behavior is a STOP condition. A `POWER / LOSS / FAULT` screen now automatically retries only after filtered input has remained strictly above 14 V for one continuous second; a dip to 14 V or less restarts the wait. Repeated recovery is possible without a button press. Any such trip during an ordinary shipping-test load transition remains a HOLD for supply-path/regulation investigation, even if operation resumes. Keep the input OFF control available: the stopped screen is no longer a persistent output-off lockout. The sustained-decline power-loss detector is also disabled by default (`PWRMGT_POWER_LOSS_SUSTAINED_ENABLED=0`); the independent fast-drop detector remains controlled by `PWRMGT_POWER_LOSS_FAST_DROP_ADC_COUNTS`. The absolute undervoltage screen `LOW / VOLT / FAULT` also automatically recovers after the same continuous >14 V / one-second interval. A USB source can therefore drop out and retry repeatedly; a recovered event remains a HOLD for this shipping screen rather than evidence that the supplied source/cable passed the intended load.

## Procedure

### S1. Cold inspection and startup

1. With power off, inspect the finished enclosure, insulated MOSFET mounting, thermistor bonds, fuse, RF connector, cable strain relief, fan clearance, and accessible metal. Do not proceed with visible damage or loose hardware.
2. Fit the default dummy load. Start with the supply at **28.0 V / 5.0 A**. Enter debug promptly, select M:50, and allow startup messages to finish. If necessary, select the intended fan setting in setup with power removed between configuration and this test; never improve cooling only for testing.
3. Record both initially cold thermistor readings. They should be within approximately 10 C of the estimated room temperature and each other. A cold 0 C, grossly high reading, missing-sensor message, or visibly detached sensor is a HOLD; do not rely on subsequent temperature limits.
4. Verify that the input indication is compatible with the supply within its coarse uncertainty and the OLED's whole-volt rounding. Expect `>28V`, but do not require exactly that string as a calibration check.

### S2. Default dummy-load operation

#### Expected power draw

Use **M:50 as the main numerical acceptance test**, with the supply set to **28.0 V / 5.0 A** and the dummy load in its default 50 ohm + 146 nH configuration. The expected operating target is approximately **45-50 W on the OLED buck-power reading**. For this shipping screen, use **40.0-52.5 W as the normal settled buck-power band** and approximately **1.6-2.7 A indicated input current**, or **45-76 W indicated whole-board input power at 28 V**. The input band is deliberately broad because it includes the gate driver, fan, buck-converter losses, and coarse supply metering.

These are **proposed acceptance targets for this fixture**, not previously measured Hot Wand results or calibrated RF-output predictions. The 40 W lower bound requires at least 80% of the selected 50 W buck-power ceiling; the upper bound is the existing 5% settled-power allowance. The input-current band is a provisional board-level expectation, not a component rating or an efficiency measurement. Confirm these bands against recorded healthy-unit results as the first batch is tested; do not silently relax a failing unit's criteria. If the fixture's natural feedback operating point is below 40 W, this screen classifies the combination as below the chosen shipping target until the fixture/reference has been reviewed.

| Measurement at settled M:50 | NORMAL / PASS | UNDERPERFORMING / HOLD | STOP |
| --- | --- | --- | --- |
| OLED NOW buck power | **40.0-52.5 W**, preferably about 45-50 W | **<40.0 W**: below the shipping power target; **>52.5 through 55.0 W**: suspect regulation/calibration | **>55.0 W for 1 s**, or any general stop condition |
| Indicated input current at approximately 28 V | **1.6-2.7 A**, approximately **45-76 W** whole-board input | **<1.6 A**: verify low delivered power or inconsistent readings; **>2.7 but <4.5 A**: excessive input demand for this 50 W test | **>=4.5 A for 1 s**, current-limit/trip behavior as specified in the general rules, or abnormal heating |
| Combined interpretation | Buck power, input demand, and MOSFET temperatures must all pass | Normal buck watts with excessive input current is still a HOLD for excess loss; normal input current with low buck watts is still a HOLD for underperformance | Any STOP overrides an otherwise normal watt reading |

Use simultaneous input voltage/current indications to calculate input watts if the indicated voltage differs from 28.0 V. The current and input-watt ranges are approximate descriptions of the same operating expectation, not independent precision measurements. Near an input-band boundary, repeat the reading and apply the stated meter uncertainty; an unresolved boundary result is HOLD, not an automatic hardware diagnosis. A brief MAX power peak does not satisfy the minimum-power criterion: judge expected power from settled NOW readings.

#### Test sequence

1. At M:50, clear maxima and run for **10 minutes** with fixture air cooling operating. Record simultaneous NOW buck watts and input voltage/current at **30 s, 1 min, 5 min, and 10 min**. Record MAX at the end. Do not clear peaks mid-run.
2. Compare the four settled observations with the expected-draw table. NORMAL requires repeatable buck power within **40.0-52.5 W**, input current within the approximate **1.6-2.7 A** band, no unexplained fault, and no monotonic power drift. If PWM makes NOW fluctuate, record its repeating low/high range and several readings instead of selecting only the most favorable number; the typical settled value must satisfy the power band, and the live stop rules still apply.
3. Apply all general thermal limits. Both final MOSFET readings must be **<=70 C**, with a rise **<=2 C over the final five minutes**. If still rising, extend to 20 minutes under supervision; failure to stabilize is a HOLD. Expected power draw does not excuse excess temperature.
4. Compare with a previously recorded healthy unit using this exact fixture, fan mode, supply, and thermal state, if available. A repeatable buck-power reduction >20% is UNDERPERFORMING even if it barely meets the absolute lower bound; a repeatable increase >20% in input current or >10 C in either bead temperature is a HOLD for excess loss. With the coarse input meter, repeat paired comparisons and use the uncertainty envelope before claiming a small difference is real.
5. After at least 60 s at stable M:50, briefly select M:75, clear peaks, and observe for **no more than three further seconds**. Return to M:50 and record. Recover at M:50 for at least 60 s, then repeat for M:100. Include the roughly one-second clearing hold in the total high-mode exposure; do not extend it to chase a reading. Stop sooner for any threshold violation.
6. For the brief higher-mode checks, expect buck power to remain at least near the passing M:50 value, and to increase if M:50 was actually limiting this fixture. A repeatable fall **>20% below the recorded M:50 operating value**, while the higher mode is confirmed applied and no intentional derating is present, is a HOLD. Apply the general **78.8 W settled review boundary / 82.5 W sustained stop** at M:75 and **105.0 W / 110.0 W** at M:100. Higher modes do not require the fixture to draw exactly 75 W or 100 W; the analog feedback/load operating point can limit demand first.

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
- [Power/temperature supervision and applied mode](../../src/pwrmgt.c), [current/power attenuation](../../src/pwrlvl.c), and [virtual thermal limiter](../../src/leaky_bucket.c).
- [Current thresholds](../../inc/conf.h), [fan behavior](../fan-modes.md), and [implemented protection behavior](../implemented-faults.md).
