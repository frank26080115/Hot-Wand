# Automated functional testing (13.56 MHz Hot-Wand)

## Status and scope

**Implemented host runner and native companions, 2026-10-22; target execution awaits fixture validation.** This is the
"rest of it" suite: normal startup, runtime controls, display, inactivity, normal battery
operation, fan control, and application of saved settings. Fault diagnoses, recovery,
warnings, and protective limits retain their contracts in [faults-test.md](faults-test.md).
Menu navigation, editing, save/discard, and menu inactivity retain their existing suite in
[setup-menu.md](setup-menu.md), subject to the obsolete-build references called out below.

Use one automated target image, `stm32f042_test`, for menu, fault, and functional testing.
Keep RF physically inhibited and buck attenuation GPIO-high/minimum throughout every
automated target case. The requested logical attenuation remains testable; the load must
not be energized to make a software test pass. A connected dummy load does not turn
simulation results into electrical qualification. Camera use is optional and off by default.

This plan separates:

- **T: target simulation.** Actual application on the MCU, real ADC interrupt pacing,
  simulated ADC/button/tip inputs, and the completed sent-framebuffer mirror.
- **N: native/component.** Actual firmware C with controlled time and peripheral models,
  including production branches excluded or deferred in the shared target image.
- **E: electrical/manual.** Physical inputs, output voltages/waveforms, fan motion,
  UART transmission, and loaded regulation. These remain separately reported and BLOCKED
  without fixture evidence; they are not a reason to enable outputs in the shared image.

Existing passing fault/menu runs are regression evidence, not automatic passes for the
new cases. Each case must identify which behavior it independently establishes.


Run from the firmware directory after building the shared image:

```powershell
pio run -e stm32f042_test
python tools/auto_testing/functional_test.py --case FUN.boot.healthy.0 --camera off
python tools/auto_testing/functional_test.py --native --camera off
python tools/auto_testing/functional_test.py --native-only
```

The runner flashes an already-built ELF by default. `--no-flash` verifies the existing
image instead. Close other debugger sessions first. Outputs go to a new
`functional-TIMESTAMP` directory; open `index.html` for the gallery, `report.txt` for
chronological assertions, and `results.json` for full coverage and native check mappings.
`--long` includes 15/30-minute sleep, the sleep-off control, activity near expiry and
endurance; allow over an hour for the entire extended selection. The default target
selection includes the real five-minute sleep case and repeated dimming delays, so it
also takes several minutes. `--case FUN.sleep.auto.3` selects the 30-minute case directly.

There are 61 target cases, 38 native coverage groups, eight electrical exclusions,
one optional deferred plant model, and one unresolved boot-timing requirement. An
implemented case is not a hardware PASS. Native coverage includes the actual main loop
with mocked peripheral boundaries; a native non-local return ends observations at its
watchdog-feed boundary without rewriting its infinite loop or advancing a real target
clock. The OLED contrast test compiles the exact current public function body in
isolation; the ADC native model maps the factory calibration address to native RAM.

The default functional cases preserve NVM byte-for-byte. Fan active profiles and UART
transmission remain native/electrical because the shared image preserves SWD. Ordinary
splash dismissal/expiry is exercised natively, and actual production splash pixels remain
electrical/manual. The optional responsive-plant case is explicitly deferred. See the
[tool README](../../tools/auto_testing/README.md) for dependencies and file responsibilities.

## Expectations and conflicts to settle

The following are source findings, not silent changes to the design specification.
Where wording is ambiguous, the proposed contract follows current code and is explicitly
subject to review. An unresolved requirement must be reported as such rather than made
green by copying the implementation into the expected result.

| ID | Finding and source | Proposed test contract / decision needed |
| --- | --- | --- |
| C01 | [fw-design.md](../fw-design.md) says a short press "cycles" modes. `pwrmgt_change_pwr_lvl()` in `src/pwrmgt.c` moves back and forth: 100, 75, 50, 75, 100 W. Direction is initially downward, including when the saved startup level is 75 W. | Accepted by the user and documented beside the code: use the back-and-forth sequence, not a 100, 75, 50, 100 wrap. Startup at 50 W moves upward on its first press. |
| C02 | `button.c` emits a runtime short event on the initial press edge, then a long event at 1000 ms. A long runtime gesture therefore can change the desired power once before entering sleep. Setup uses release-based short events instead. | Accepted by the user: the initial power change before sleep is an expected operation quirk and must not cause a test failure. Preserve immediate short action and subsequent sleep. Do not write a test assuming that short and long events are mutually exclusive in runtime. |
| C03 | `enter_sleep_mode()` calls `show_fault("ZZZZZ", true)`. This is a watchdog-fed terminal screen, with RF stopped, minimum buck output, fan stopped, continuing ADC interrupts, and button reset. It does not enter a hardware STOP/sleep instruction or resume the suspended runtime. | Accepted by the user: sleep means safe standby until reboot. Neither reduced MCU consumption nor direct runtime resume is required. The sleep-entry hold must be released before a new press may reset. |
| C04 | Inactivity in `pwrmgt.c` is elapsed time since measured buck power was at least the idle threshold or a mode-change request occurred. It is not motion, tip use, temperature change, or a pause in RF-start requests. Equality counts as active; low power is strictly below threshold. | Test this power-based definition for dimming/sleep. A dummy load plus synthetic low power must still become inactive; synthetic power above threshold keeps it active with RF physically off. Confirm this is the intended user experience. |
| C05 | `fw-design.md` now describes waiting up to 300 ms for stable boot power. `boot_wait_for_power_stable()` returns after 100 ms without a new measured maximum, can show `.....` after 300 ms, and continues after a two-second timeout. A falling voltage also satisfies the no-new-maximum rule. | Test these as separate timing rules, not a mandatory 300 ms delay or a true flat-voltage detector. The up-to-300-ms wording still differs from the two-second timeout; that requirement remains BLOCKED for review. Existing protections still apply after the wait. |
| C06 | Resolved: `main_render_display()` now alternates voltage and `!HOT!` in one-second phases while the temperature attenuation reason is active, including hysteresis recovery. | Native tests check exact text and phase boundaries; target tests retain both top-line images and recovery. Thermal limit detection remains in the fault suite. |
| C07 | Resolved: the user updated the design to 5 Hz, matching `UART_DEBUG_TASK_PERIOD_MS` of 200 ms. UART is entirely suppressed by `HOT_WAND_SWD_DEBUG` to preserve PA14/SWCLK. | Target checks that PA14 stays with SWD. Native tests can characterize the existing 200 ms schedule and boot permission. Use 5 Hz as the accepted contract; physical UART remains E. |
| C08 | The shared image defers fan startup by 4294967295 ms (about 49.7 days). `fan_task()` returns before its profile calculations during this hold. PA13 is also SWDIO. | Normal shared-image runs cannot validate any active fan profile, even as a logical command. Cover the real fan controller in N; do not shorten this hold or claim target fan control passed. Native time-wrap tests must not turn a target run into PA13 takeover. |
| C09 | Splash assets, renderer, and menu option are compiled out in the shared image. The saved splash bit remains in the six-byte record. Its first-press dismissal path and two-second display exist only in normal firmware. | Native/component and production-image electrical/visual checks cover these branches. No new automated target image. Do not count optional camera fiducial dismissal as splash testing. |
| C10 | Runtime dimming uses the RAM boot setting, but `fault_get_auto_dim_delay_ms()` rereads NVM when entering a terminal screen, including `ZZZZZ`. | A RAM-selected dim delay is not evidence of the same sleep-screen delay. Test and record both settings; use native tests for arbitrary terminal dim choices unless the observation/configuration interface is extended deliberately. |
| C11 | The current RAM boot settings interface exposes battery, startup power, calibration, sleep, dim, and initial tip absence; it does not override idle threshold, fan profile, or polarity. `setup_menu()` separately loads NVM. | Extend configuration only if necessary and budget permits, or use native tests and existing NVM/menu evidence. Writing a new boot setting must not be presumed to configure the setup menu. Default target suite must not save settings merely to prepare cases. |
| C12 | Current telemetry exports applied power, not desired power, direction, last activity, or graph peaks. RF start attempts repeat every foreground pass because physical RF remains off. Brightness is a controller contrast setting, not a framebuffer pixel change. | A repeated RF-attempt count is healthy in this test image. Assert final restored preferences by behavior; add minimal read-only telemetry only where needed. A PNG alone cannot prove dimming, and equality of whole screenshots cannot prove unchanged graph data. |
| C13 | The older setup plan names removed `stm32f042_setup_swd`, eleven pages, and a required camera arrangement. The current shared PWM image has ten pages without splash, optional camera, and an ADC boot gate. | Reuse the implemented menu runner/shared build and compiled item count. The old plan is historical where it conflicts; this change does not edit it. |
| C14 | Calibration option names are not a simple percentage of the displayed voltage. `input_voltage_calib_table` uses Q10 slopes plus offsets (for example P1: 1029/1024 and +5 mV). Calibration affects DC input only, not buck-derived power. | Test the affine conversion with its integer rounding, all eleven options, and DC-only scope. Do not label P1 as a one-percent correction without a separate approved definition. |
| C15 | Battery startup chooses the optimistic (fewest plausible) count. An out-of-range voltage can deliberately produce a count above the supported maximum to force cutoff, rather than simply clamp to the maximum. LiFe has a 6S floor. | Propose preserving these conservative failure semantics; cover guessing independently from the already-tested fault/override UI. Do not assume every returned guess represents a supported healthy pack. |
| C16 | The design's ECO promise says it must never fail with a 65 W USB supply. Software limits measured buck-output power to 50 W; input losses, USB negotiation, transients, and a responsive load are not simulated by a fixed ADC waveform. | Logical 50 W regulation can pass T/N, but the supply compatibility promise remains E until physically measured. Current limit, thermal model, and input derating remain enabled. |
| C17 | Fan profiles use the hottest of NTC1, NTC2, and MCU temperature, excluding the leaky-bucket virtual temperature. Adaptive gains of 25..150 percent are gains, not fixed fan duty. Startup boost and dwell can temporarily override the steady-state curve. | Propose this exact sensor/model distinction. Test profile output after accounting for boost/ramp/dwell, rather than expecting an immediate duty percentage from temperature alone. |

The first implementation should preserve existing firmware behavior while making these
differences visible. Product decisions that change behavior need corresponding code and
contract changes before the affected cases become acceptance tests.

## Coverage ownership

| Area | Existing owner | Functional addition |
| --- | --- | --- |
| All F01-F11, W01-W02, L01-L03, R01-R04 | Fault runner and native companions | Healthy-side integration, normal mode selection, and continued service; reference existing IDs for fault outcomes. |
| Menu navigation, value cycles, save/discard, timeout | Setup menu runner | Confirm saved settings actually affect runtime; no second exhaustive menu traversal. |
| ADC FIFO ordering, repeats, round boundaries, raw fault checks | ADC/fault native tests | Normal conversion, calibration, signal settling, display and power consumers. |
| Physical button/tip, RF/buck/fan waveforms | Electrical fixture/manual tests | Separate manifest entries; never substitute the virtual inputs. |
| Normal startup, runtime UI, activity and sleep | New functional runner | Primary new target coverage below. |
| Fan algorithms, UART, normal splash branch | New native companions | Actual C under mocked time/peripherals; shared-image omissions remain explicit. |

## Common execution and evidence contract

Implemented entry point: `tools/auto_testing/functional_test.py`, with a separate case-definition
module and reuse of existing transport, ADC, image, waveform, metadata, and reporting helpers.
Do not clone the fault runner wholesale. Extract common lifecycle/report code where that
reduces duplication and keep both existing suites passing. Options follow the fault
runner: `--case`, `--camera off|rx0`, `--native`, `--native-only`, `--no-flash`, `--elf`, and
`--output-dir`. Use `--list` to inspect stable parameterized IDs before selecting cases.

1. Validate image hash, required harnesses/ABI, flash load bounds below NVM, calibration,
   and physical-output inhibition. Record board identity, fixture description, firmware
   revision, dirty-tree status, selected contracts, and effective settings.
2. Back up the 1 KiB NVM page before flashing and preserve it byte-for-byte for default
   functional cases. Flash only application ranges. Tests must not directly patch NVM or
   arbitrary internal variables; use supported mailboxes and read-only observations.
3. Reset for each independent case. At the boot gate choose RAM settings and enqueue one
   healthy complete frame before requesting start. Wait for the relevant application stage.
   Cases testing cell guessing leave the forced count zero. For runtime waveforms, wait for
   runtime readiness first; ADC continues during initialization and terminal/menu screens.
4. Require RF GPIO low/TIM1 output disabled and PA6 GPIO high/no TIM3 pin ownership before
   and after each stimulus/capture. Assert these even during logical PWM changes. No test
   command may release the physical clamp. Reset-time pin bias remains a hardware property.
5. Encode requests with the compiled ADC lookup table. Record requested and representable
   raw/engineering values and per-device calibration. Seven-channel frames must keep the
   reference and unrelated channels healthy. Avoid accidental power-loss detection on DC
   ramps and accidental thermal-bucket shutdown on sustained high power.
6. Express ADC sequences in rounds/repeats. Use target milliseconds for inactivity,
   overlays, button durations, and controller updates. Separate debugger-halt durations
   from running time. Allow ADC filtering to settle before engineering-value assertions;
   exact timing boundaries and tick wrap belong in N. Never write the target clock forward.
7. Preserve raw framebuffer, native 32 x 128 PNG, enlarged PNG, state/ADC snapshots, and
   timestamps at baseline, transition, and final states. Capture an existing coherent
   nonzero even generation for static screens in the current boot; require a later
   generation for actual display transitions. Never reuse a previous boot's pixels.
8. Every ADC-driven case saves `adc_waveform.png` with time on X, sequence zooms as needed,
   and CSV/JSON source values. Reuse `adc_waveform.py`, including held-last intervals and
   inferred timing uncertainty. Plot after safe cleanup, outside time-sensitive gestures.
9. Camera-off mode imports no camera dependencies. Optional photos add optical evidence;
   synthetic PNGs always remain available when OLED transmission succeeds. OLED hardware
   is still required. Contrast commands cannot be represented by the framebuffer image.
10. Reset to the watchdog-fed safe boot gate on completion/interruption, verify NVM,
    retain partial artifacts, and close the debugger. Abort the run on output-inhibit or
    NVM-preservation failure rather than continuing into other cases.

Results use PASS, FAIL, ERROR, BLOCKED, and NOT_RUN, with visual review PENDING/APPROVED
separate from automatic assertions. `report.txt`, `results.json`, and `index.html` provide
case summaries and linked images/graphs. A blocked selected requirement prevents a claim
of full coverage. Native, target, and electrical results are separate even for one family.

## Target and component case matrix

Each table row is a family. Parameter combinations and boundary controls must receive
stable child IDs and separate results in the implemented manifest.

### Startup and settings application

| ID / level | Stimulus | Expected behavior and evidence |
| --- | --- | --- |
| FUN.boot.healthy / T | Released button, virtual tip present, held healthy frame, battery disabled; boot each startup power setting. | Reach runtime with the requested applied power and calibrated measurements, no terminal diagnosis or unintended setup entry. ADC rounds, frame generation and foreground tick advance beyond a watchdog interval. Logical attenuation can change; physical outputs remain clamped. |
| FUN.boot.wait / N + T where observable | Stable, increasing, and decreasing input; increasing input lasting beyond two seconds; ramps around the 300 ms message point. | Characterize C05 exactly: stable return after 100 ms without a new maximum, waiting dots only when still waiting at 300 ms, timeout continues. N controls entry time and every new maximum; T retains the actual ADC waveform/stage times, without assuming host release equals wait entry. |
| FUN.boot.hold / T + N | Boot held; release comfortably before three seconds, hold past entry, and boundary release/debounce cases in N. | Capture hold text and increasing 32-pixel progress bar, early-release runtime, and successful setup entry. Release creates no leaked runtime mode-change/sleep event or menu navigation. Menu details remain owned by its suite. |
| FUN.settings.runtime / T + N | RAM overrides for power, battery, calibration, sleep and dim; separately boot using saved settings without the override-valid bit. | Runtime consumes the specified settings. Record the saved baseline and effective RAM values. Read NVM without changing it; verify other saved settings remain honored. Native blank/invalid-record tests establish explicit defaults without erasing the board. |
| FUN.settings.defaults / N | No valid journal record; valid latest record; invalid field/checksum/magic; several records. | Defaults are SPORT, COOL fan, normal polarity, sleep/dim off, 10 W idle threshold, battery off, splash enabled, calibration none. Latest valid record wins. Reuse journal error coverage from fault/storage tests; add missing normal/default cases only. |
| FUN.settings.journal / N | Save/read repeated identical values, changed values, full journal rollover, reinitialization, factory reset and save after reset, using a RAM flash page. | Verify normal append/readback, unchanged-save avoids flash writes, rollover retains the new record, factory reset leaves no saved settings, and defaults apply at next boot. Reuse existing component fixtures and never factory-reset the connected board as routine test preparation. |
| FUN.boot.splash / N + E | Normal-build splash enabled/disabled; timer expiry; first short dismissal; transient message replacement. | Ordinary splash lasts up to 2000 ms while visible; a dismissal press does not also change mode, and a later independent press does. Cover dismissal during boot wait and runtime. Confirm actual pixels in production E; shared T explicitly excludes this branch. |
| FUN.boot.random / N + T | Repeated resets with known synthetic startup values, including otherwise-zero seed inputs in N. | Simulator guarantees a usable nonzero seed. Runtime voltage text stays inside allowed startup shift bounds X 0..3, Y 0..3. Do not require different seeds or shifts on every reset: simulated input can be deterministic. |

### Runtime button and power selection

| ID / level | Stimulus | Expected behavior and evidence |
| --- | --- | --- |
| FUN.button.mode / T + N | Well-separated short presses from each saved startup level, with cool sensors and DC above recovery threshold. | Accepted C01 sequence: from 100 W, 75/50/75/100/75; from initial 75 W, 50/75/100; from 50 W, 75/100/75. Applied mode and graph ceiling change; no persistence write. Reset restores startup preference rather than the last runtime choice. |
| FUN.button.edges / N + T | Runtime press held long enough to observe before release; 999/1000 ms exact in N; 850/1150 ms target controls; release bounce, repeated hold servicing, fast valid presses. | Short event is immediate on press, release adds no second short in runtime, long fires once at threshold, and release debounce is 50 ms. Pin bounce belongs to N/E rather than invented target mailbox pulses. Allow the initial power action of a long gesture per accepted C02; it must not cause a test failure. |
| FUN.button.repeat / T + N | Six consecutive short presses inside five-second gaps; then gaps at 5000 and 5001 ms in N; another six. | Sixth mode action still occurs, exactly one allowed one-second novelty message appears, and the consecutive count resets. Assert membership of the five existing messages, not a specific random choice. N verifies 4999/5000 ms remain consecutive, 5001 ms resets the sequence, and a long event resets the counter. |
| FUN.power.request / T + N | Select 50/75/100 W; supply buck/current combinations below, at, and above the ceiling without crossing 5.2 A. | Logical attenuation relaxes under/equal limit and increases above it, with previous-sample carryover. N verifies jump to 16, 2 ms increase, 50 ms decrease, saturation 0..31 and forced minimum 32. T uses representable values and generous observation windows; never expects the fixed synthetic power to fall on its own. |
| FUN.power.preference / T + N | Select each preference; temporarily impose low-DC or thermal derating, recover safely; press while blocked and hold for sleep while blocked. | Effective power becomes 50 W, preference is restored after hysteresis recovery, blocked presses do not change it, and a long gesture still enters sleep. Verify delayed release-based blocked messages and repeated-attempt count only where not already covered by faults. Under both reasons, temperature has message priority. |
| FUN.power.plant / N initially; T optional | Use a declared deterministic software plant mapping logical attenuation to the next measured buck/current values. | Demonstrate controller convergence qualitatively and recovery after a load step within declared tolerance/settling limits of that model. Record plant equations/delay and avoid current/thermal interference. A host model is not proof of real supply stability; retain E coverage. This is optional extended coverage, not a prerequisite for fixed-waveform controls. |

### Activity, dimming, and sleep

| ID / level | Stimulus | Expected behavior and evidence |
| --- | --- | --- |
| FUN.idle.threshold / N + T baseline | All seven idle thresholds: 1/2/5/10/20/30/40 W; below/equal/above, input filtering settling, renewed power after idle. | Equality and above reset inactivity; below accumulates it. A mode request resets last activity, including a blocked request. Track idle-duration and time-since-last-activity as different quantities. T covers saved threshold; N covers values not exposed by RAM setup. |
| FUN.dim.delay / T + N | Dim off, 15/30/60 seconds; low power, then short press or active power; repeat after another idle interval. | No early dim, dim at the configured inactivity threshold, immediate subsequent brightening on activity, repeatable timer reset. Use OLED contrast/dim-state observation, not PNG darkness. N verifies contrast 32/255, no redundant command when unchanged, and failure-return behavior. |
| FUN.sleep.manual / T + N | Runtime hold above one second, remain held beyond entry, release, then independent press. | Enter `ZZZZZ`, fan stopped, safe outputs, no automatic runtime return. Remaining hold cannot reset; a debounced release then new press reboots into the ADC gate. Save sleep framebuffer before reset and reset cause afterward. ADC and watchdog service continue while sleeping. |
| FUN.sleep.auto / T real-time + N | Low power with auto-sleep off, 5/15/30 minutes; dim both on/off. | Off stays in runtime. Enabled settings enter sleep at their own inactivity thresholds, never just because dimming happened. Native covers all boundaries; target long group covers each configured real duration. Do not replace a 30-minute observation with an accelerated target clock. |
| FUN.sleep.activity / T + N | Activity near timeout from power or a short request; sustained active power; return to low power. | Activity restarts the full inactivity interval. Active power prevents sleep; final low-power interval permits it. Use native exact scheduling to decide simultaneous activity/expiry order; allow target margins for filtering and debugger pauses. |
| FUN.sleep.display / T + N | Remain on `ZZZZZ` for multiple five-second shift intervals and beyond the saved NVM dim delay. | Animation remains on-screen and watchdog-fed; no RF restart attempts from the terminal loop. Sleep-screen dim follows saved NVM, not necessarily the runtime RAM override (C10). PNGs show geometric motion only; telemetry establishes contrast. |

### Display and power history

| ID / level | Stimulus | Expected behavior and evidence |
| --- | --- | --- |
| FUN.display.voltage / T + N | Stable healthy DC plateaus and slow ramps at each calibration; values around one-decimal rounding boundaries. | Top line is calibrated DC voltage followed by uppercase V, correctly rounded with no clipping for supported operating voltages. Only the declared text shift is normalized for comparisons; don't normalize away wrong characters. N establishes exact formatter carry/length and extreme utility inputs. |
| FUN.display.graph / T + N | Buck/current plateaus near 0/25/50/75/100 W, safe short over-100 W pulse, then reduced power. | Bottom 100 pixels map 0..100 W with integer floor and clamp; top 28 pixels remain reserved for text. Live center is X15/16; fifteen completed peaks are mirrored outward. Full graph and raw pixels are retained. Over-100 W duration must stay below bucket/current fault conditions. |
| FUN.display.peaks / N + T | Pulse within a 250 ms segment, then zero; repeat across at least 17 segments to wrap history. | Completed segment retains its peak rather than only its final sample; center follows current filtered power. Correct mirrored order, recycled-slot clearing, and edge eviction; at zero power after history eviction, the 100 W dotted ceiling remains visible. N sets exact segment phase; T logs phase uncertainty and uses long enough plateaus to avoid ambiguous expected pixels. |
| FUN.display.ceiling / T + N | Select each power level on low-power and shaded graphs. | All three modes show the effective ceiling: 100/75/50 W at Y28/Y53/Y78 respectively. One continuous dot/two-space pattern spans the full width, anchored at X0, with no mirroring or center restart. Assert 11 dots at X0,3,...,30, no adjacent center dots, and unchanged positions before, between, and after graph-history updates, including history ring wrap. Assert XOR behavior over both empty and filled regions. Keep at least one full black row between the lowest shifted voltage text and the line (Y27 remains black). Derated ceiling behavior is cross-referenced to fault L cases. |
| FUN.display.service / T + N | Quiet runtime, mode changes, novelty/blocked message, return to runtime, active ADC waveform during message. | Normal display sends at most nominal 15 FPS (67 ms scheduling); short messages own display while active with 100 ms refresh. Supervision/ADC/button handling continue, and live graph returns after expiry. N checks ownership/preemption exactly; T checks completion and no foreground stall without asserting an exact host-observed frame rate. |
| FUN.display.static / T + N | Capture unchanged setup screen, then a button-driven display change and capture again. | Existing coherent current-boot frame is accepted for static evidence; transition capture requires a newer completed generation. Cover zero/odd generations and reset so the F11 screenshot timeout regression cannot recur. |

### Normal ADC, conversion, and battery operation

| ID / level | Stimulus | Expected behavior and evidence |
| --- | --- | --- |
| FUN.adc.conversions / N + T | Independently vary DC, buck, current, NTC1, NTC2, MCU temperature, and plausible VREFINT; keep all protections clear. | Correct channel identity, filtered step direction/settling and engineering values; no mixed-frame channels. Voltage divider 11, reference scaling, current scale, and integer rounding are independently calculated. Sparse VREF updates affect related conversions at their actual cadence. FIFO mechanics remain owned by existing tests. |
| FUN.adc.sensors / N | NTC table points and interpolated values across the supported range; calibrated MCU temperature points; invalid channel arguments. | Verify monotonic conversion, integer interpolation/clamping, reference compensation where implemented, and invalid getter return values. Reuse existing ADC startup/round tests instead of bypassing acquisition in target cases. |
| FUN.adc.power / N + T | Change DC only, then buck/current independently; power near zero and ordinary levels. | Power is rounded buck millivolts times milliamps divided by 1000, not DC input power. DC calibration changes displayed/input-supervision voltage without altering buck or measured power. |
| FUN.adc.calibration / N + T | None, P1..P5, N1..N5 across several DC points; invalid selector native-only. | Q10 affine calculation and rounding match the reviewed table; monotonic correction direction, no correction to other channels, safe saturation, invalid selector falls back to none. Verify runtime uses saved/RAM selection and menu preview uses the same path. |
| FUN.battery.guess / N + T | All six chemistry modes, ordinary packs, ambiguous ranges, supported min/max counts; target leaves forced cell count zero. | Optimistic count is ceiling(pack/max-cell), pessimistic is floor(pack/min-cell), with configured floors/caps and conservative invalid-range handling. Native covers every boundary; target covers representative valid guesses without relying on the ADC producer's own implementation as sole oracle. |
| FUN.battery.cutoff / N + T healthy side | Stable voltage at/above inferred cutoff for each chemistry; battery-disabled mode at healthy hardware voltage. | Battery passes at equality; disabled monitoring produces count zero and no battery diagnosis. Native tests bracket exact cutoffs and unsupported inputs; fault F06 owns below-cutoff UI, override, and one-shot correction. |
| FUN.battery.reboot / T | Boot valid pack; vary voltage slowly within healthy range, reset and reboot at a different stable pack voltage. | Cell count remains fixed during runtime; reboot re-guesses from new boot voltage rather than retaining stale RAM count. Preserve NVM. Capture resulting count and actual calibrated voltage, avoiding unintended low-voltage or power-loss paths. |

### Fan, UART, and production-only branches

| ID / level | Stimulus | Expected behavior and evidence |
| --- | --- | --- |
| FUN.fan.fixed / N | OFF, ramped 100%, fixed 25/50/75%, both signal polarities; normal startup hold. | OFF remains zero, ramped 100 rises at 25 percentage points/s, fixed profiles boost to 100 for 500 ms before their set duty. Do not apply the boost to ramped profiles. Native PWM mock records controller commands; hardware polarity is E. |
| FUN.fan.binary / N | Six automatic binary profiles: 25/50/100% at COOL 50 C and QUIET 70 C, rising/falling temperature and dwell interruptions. | Activate at equality, deactivate strictly below threshold minus 2 C, obey 10 s minimum on and 5 s minimum off. Initial startup hold replaces the first off dwell. Ramped 100% profiles finish ramp-down before starting subsequent off dwell; boost behavior matches fixed/automatic non-ramped paths. |
| FUN.fan.adaptive / N | All five gains at 40 C threshold, 80 C and clamped high target, including temperature changes within one second. | Minimum requested duty 25%; after boost, update at one-second intervals using gain curve and nearest integer. At 80 C: 44/63/81/100/100% for gains 25/50/75/100/150. Test hysteresis/dwell and no new boost for a running target change. |
| FUN.fan.sources / N | Hottest NTC1, NTC2, or MCU in turn; high virtual bucket alone; stop/resume/on-wake; invalid mode. | Hottest real sensor controls fan, bucket excluded. Stop forces zero and disables task; resume resets controller. PWM invalid mode normalizes to COOL. Native fallback checks its four supported modes and normalization separately, without adding another automated target environment. |
| FUN.fan.hardware / N + E | Fan PWM peripheral initialization, both topologies, 0/25/50/75/100%, reinitialization and invalid modes. | Mocked register tests verify 25 kHz configuration and command clamping/no effect before init. E measures pin waveform, polarity, fan response and SWD handover using a separate physical procedure. Shared T only proves PA13 remains with SWD during the run. |
| FUN.outputs.production / N + E | Production RF start/stop/repeated calls and tip qualification interlocks; buck initialization, force/release minimum and ordinary logical ramp. | Native mocks exercise the production driver branches as well as the inhibited test branches. Confirm start eligibility, stop idempotence, safe pin/timer ordering and unchanged protection interlocks. Real RF frequency and buck voltage remain E; a shared target RF-attempt counter proves a request only. |
| FUN.uart.permission / N + T suppression | Boot button released/held/early released; runtime presses. | Normal firmware permission is decided from the sampled boot button, not enabled by later runtime presses. Shared T never takes PA14 from SWD even after boot hold. Native covers normal branch; use the accepted 5 Hz schedule. |
| FUN.uart.payload / N + E | Known converted measurements, valid/not-yet-valid reference, all applied power modes and reason flags. | Verify fields, units, VREFINT NA handling, power ceiling/reason bits and CRLF, using captured mock transmissions. Real pin mapping, baud and timing remain E. Do not reconnect a UART onto SWCLK during target tests. |

### Endurance and evidence limits

| ID / level | Stimulus | Expected behavior and evidence |
| --- | --- | --- |
| FUN.endurance / T | Extended healthy sequence with modest power, periodic short presses, sub-fault temperature changes, many FIFO refills and graph wraps. | Foreground, ADC and framebuffer keep advancing without unexpected reset/protocol error; inputs stay healthy; intended held-last intervals are labelled. Preserve settings, physical clamp, and artifact availability. Use a declared duration, not a claim of indefinite reliability. |
| FUN.components / N | Formatter bases, negative values and carries, Fletcher checksum known vectors, deterministic PRNG reseeding, ordinary SysTick/watchdog initialization and service. | Test public utility contracts with independent vectors and meaningful boundaries, including values used by display/UART/NVM. Reuse existing tests for watchdog failure paths; normal service must continue during all intentional foreground waits. Avoid duplicating implementation line by line. |
| FUN.time.wrap / N | Millisecond counter near wrap for graph, messages, button, activity/sleep/dim, startup and fan timers. | Unsigned elapsed-time behavior matches ordinary timelines. ADC producer/consumer wrap stays in existing FIFO tests. Do not make the shared target fan acquire SWD by forcing its long hold to expire. |
| FUN.electrical / E | Real switch/tip, RF frequency/start-stop, buck attenuation, loaded power steps, USB 65 W supply, fan/NTC/UART, physical wake/reset. | Separate fixture checklist and measurements. Software simulations cannot establish analog accuracy, heating, fan airflow, noise immunity, reset-time pin levels, or closed-loop stability. No default automatic execution or fabricated PASS. |

## Implementation preparation and acceptance gates

Inspect the ELF and existing public observations before adding firmware instrumentation.
Reuse fault ABI 2 for boot, raw/filtered values, applied power, logical PWM, stage, and reset
cause; reuse the ADC producer and coherent-frame reader. Existing runtime stage is not a
boot-wait entry marker, and existing snapshot tick does not describe every terminal-loop
iteration. Add a marker only if an essential target assertion cannot be made otherwise;
native exact scheduling is preferable to another large on-target trace buffer.

Likely missing observations are desired power/activity timing and OLED contrast state.
Prefer behavioral inference or validated read-only access to existing typed public state
(for example the OLED handle), with ELF layout validation. Do not hard-code unverified
structure offsets or depend on LTO preserving static locals. If an ABI extension is needed,
version it, keep it read-only for observations, and reject mismatched host/image versions.
For idle-threshold coverage, prefer N over target NVM edits; any future RAM configuration
extension must leave saved settings layout unchanged.

Implementation/validation order:

1. Review C01-C17 and freeze independent expected contracts, including undecided UI/rate
   requirements. Implement report/lifecycle reuse and native tests for exact semantics.
2. Add fast target startup, controls, display, conversion, battery and manual-sleep cases;
   validate each with synthetic images and ADC plots before broadening selection.
3. Add 15/30/60-second dim cases, the five-minute automatic-sleep integration, native fan
   controller and production-only branch tests, then extended real-time groups.
4. Keep a `--long` selection for 15/30-minute sleep, long OFF controls, activity-near-expiry
   repeats and endurance. Default report lists these NOT_RUN unless selected. A full
   functional coverage claim requires them or an explicitly accepted coverage exception;
   native timing tests do not silently replace their target integration evidence.
5. Re-run existing fault and setup regression suites after shared host/firmware changes.
   Verify the F11 static framebuffer fix remains intact. Keep camera-off execution fully
   functional and optional camera failures independent from software case assertions.

Recheck flash/RAM limits only when firmware changes are necessary. The current documented
shared image with the new hot indicator has just 60 bytes of flash load headroom before NVM and 400 bytes beyond
the reserved heap/stack area; these are dated measurements, not promised remaining space.
Retain the 128-frame FIFO, framebuffer mirror, heap and stack reservations, all protection
logic, and physical-output clamp. Host-side reuse/native tests must be preferred over
removing safety logic to make instrumentation fit. Check normal production builds as well
as the one shared test build when code changes.

The deliverable after implementation is a run directory whose manifest accounts for
every selected parameterized case, links its screenshots and ADC graphs, identifies
unresolved expectations and electrical exclusions, and distinguishes actual execution
from planned or native-only coverage.
