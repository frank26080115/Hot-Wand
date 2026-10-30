# Automated fault testing (13.56 MHz Hot-Wand)

## Status and purpose

**Implemented runner and native companions, 2026-10-22.** Use `tools/auto_testing/faults_test.py` with the shared `stm32f042_test` image. See the [tool README](../../tools/auto_testing/README.md) for commands, files, artifacts and evidence limits. Hardware/camera execution remains to be validated on the fixture; an implemented scenario is not a recorded target PASS. The later `rest of it` suite covers normal application behavior outside faults and their associated warnings/limits.

The goal is to exercise every implemented fault diagnosis and its recovery policy without needing to create dangerous or difficult electrical conditions. ADC-based cases must use the real round-robin interrupt, filtering, conversions, history, and foreground supervision. Every display-capable case must leave a reviewable synthetic PNG made from the captured framebuffer. A camera is optional and disabled by default.

The coverage IDs follow [implemented-faults.md](../implemented-faults.md): F01-F11 diagnoses (F05/F07 recoverable; the others terminal), W01-W02 warnings, L01-L03 recoverable limits, and R01-R04 MCU recovery paths. That inventory describes implementation, not an independently approved specification. The independent case contracts in `faults_cases.py` follow the source and the decisions below; remaining design wording differences are retained for review.

**Physical-output policy:** the shared test image now inhibits RF unconditionally and holds PA6 GPIO-high for minimum buck voltage. TIM1 output remains disabled; TIM3 never takes ownership of PA6. Power/current limiting still calculates the requested logical PWM, which the runner tests separately from the clamped physical pin. Fault ABI 2 and the output-inhibit flag identify this behavior; the runner also verifies GPIO/timer registers. Earlier wording about active RF or applied PWM in the case contracts describes production behavior, not permission to energize the simulation fixture. GPIO reset-time levels depend on hardware bias; software establishes the clamp as soon as firmware initializes.

Separate three evidence levels in every result:

- **Target simulation:** the actual MCU runs production detection paths with synthetic inputs or explicitly identified failure injections.
- **Native/component:** actual firmware C runs with mocked peripherals, allowing exact boundaries and failure returns that cannot be represented by the current target interface.
- **Electrical qualification:** probes or a dedicated fixture establish physical input detection, pin voltages, shutdown latency, clock fallback, or hardware-break behavior. Software observations cannot substitute for this evidence.

Covering a fault means accounting for all of its cases at the appropriate level. It does not mean declaring unsupported hardware tests passed.

## Current power-control and feature expectations (2026-10-30)

Default `PWRLVL_POWER_LIMIT_ENABLED=1` requests continuous RF and enforces measured 100/75/50 W buck-power ceilings. Current limiting remains independent at 5.2 A, with the unchanged strictly >5.8 A / five-second terminal fault. All RF-start interlocks remain active.

Default-enabled rise attenuation compares new segment peaks with the previous completed 250 ms peak, using full-scale rises of 20/25/30 W in SPORT/MEDIUM/ECO. Threshold and rise requests share one demand by maximum, with one-count release per `PWRLVL_UPDATE_PERIOD_MS` only below both limits. CCR32 survives limit attacks; mode changes clear pending requests/peak tracking while retaining attenuation. See [implemented L03](../implemented-faults.md#l03---shared-current-power-rise-and-selected-power-attenuation).

Host power cases check continuous logical RF requests, below-limit rise attack/release in each mode when enabled, and sustained PWM above each ceiling below the current threshold. Restore baseline before slow captures: synthetic ADC power does not respond to attenuation, so it can keep heating the shared image's enabled virtual bucket. Default history accepts all recorded segments regardless of RF, including zeros, while excluding unwritten slots.

Optional `PWRLVL_POWER_LIMIT_ENABLED=0` builds retain RF duty cycling and RF-dependent inactivity guards, covered by explicit native fixtures. Host target cases here assume the default wattage build. Physical charger tolerance and output-power qualification remain separate.

`PWRMGT_BUCK_VOLTAGE_SPIKE_MV=0` disables F04 by default. A disabled-detector skip is expected; positive trip/boundary cases require an enabled fixture. `OLED_DIMMING_ENABLED=0` also removes automatic dimming, its setting and page by default; enabled fixtures retain dimming coverage, while inactivity tests use `fault_test_inactive_ms`. Use compiled feature observations, menu titles and item count rather than historical fixed settings/page positions.

## Conflicts, limitations, and expectation decisions

The findings below retain the review context and record subsequent decisions. The runner records its contract version, working-tree revision, effective RAM settings and observed diagnosis. Software injections establish their named containment paths; they do not qualify physical faults.

| ID | Finding and source | Effect on testing / resolution |
| --- | --- | --- |
| C01 | **Resolved:** normal current limiting and short-circuit protection must be enabled together. `conf.h` rejects unequal enable settings; both default enabled. Comments now match their shared guard. | The shared target tests use both protections. Native tests check independent thresholds/timers. There is no production expectation that a disabled limiter leaves short-circuit protection enabled. |
| C02 | Ordinary faults require user action, except qualified automatic recovery for F05/F07. Fatal low-battery and short-circuit screens forbid button reset; fatal MCU failures use watchdog reset as best effort. | Retain the per-fault reset matrix. Distinguish recoverable input faults from terminal latches and require normal supervision before an allowed-phase restart. |
| C03 | **Build gap resolved:** `stm32f042_test` enables `HOT_WAND_TEST_HARNESS`, which enables ADC FIFO, synthetic button, virtual tip, and completed-framebuffer capture together. | Use this one full-application ELF for menu, fault, and optional camera testing. The separate setup, fiducial, and ADC simulation environments have been removed; do not introduce a faults-only environment. |
| C04 | **Mandatory camera gate resolved:** the shared image includes framebuffer capture and the small fiducial asset, but `vision_test_fiducial_enabled` defaults to zero. | Write a nonzero value at the ADC boot gate to request calibration. With zero, boot proceeds without showing or dismissing that screen; framebuffer capture remains enabled. No separate framebuffer/camera firmware build is needed. |
| C05 | **Boot-hold selection resolved:** `btn_swd_test_boot_hold` defaults to zero (released). Set it before ADC start to request a held setup-entry button. State 0 still means boot-held, not idle, and also appears before button initialization. | Runtime cases leave the option zero; the menu runner sets it to one, requests the optional fiducial, then releases with command 0 after setup entry. Wait for an initialized stage/frame before interpreting the mailbox; do not infer runtime readiness from the button word alone. |
| C06 | **Resolved:** `GdbSession.press()` accepts 1..6000 ms; menu timings are unchanged. | Battery holds use margin beyond the 4992 ms progress sequence and target-side status. Exact completion-refresh voltage boundaries also run in native C. |
| C07 | The ordinary tip mailbox defers commands during qualification, acknowledges same-level requests without edges, and is not serviced by terminal screens. | H05 now adds boot absence and a target-paced interrupt burst. Native tests cover restored-before-expiry glitches and the 10/11-event boundary. Reinsertion never dismisses the terminal latch. |
| C08 | Eight-bit logarithmic samples cannot represent every raw count. In a 25 V-enabled F04 fixture, 705 is bracketed by representable 704 and 710. | Use actual decoded values for target assertions and exact native boundaries. Default F04 is compiled out; do not wait for its screen or treat the feature skip as a failure. |
| C09 | ADC frames are timed by conversions; history publication is every 128 rounds, with a declared approximate 10 ms interval. Thermal/current timers use SysTick. Debug halts disturb their relationship, and IWDG keeps running during a halt. | Keep round-count and millisecond assertions separate. Do not use a long breakpoint, Python sleep, or a fixed "rounds per second" assumption as an exact timing oracle. Use target snapshots and uninterrupted qualification intervals. |
| C10 | VREFINT simulation changes the voltage/current/MCU-temperature conversions as well as the reference fault. Earlier voltage/battery/thermal checks can win. | Solve a valid complete seven-channel scenario, including per-device calibration, rather than changing only VREFINT and expecting F09. Record any unavoidable competing diagnosis. |
| C11 | The ADC FIFO runs through OLED initialization, boot waits, setup, and terminal screens. Its empty behavior holds the last frame. | A waveform preloaded immediately after the baseline can expire before runtime supervision starts. Begin with one healthy frame, intentionally let it hold, wait for runtime-ready, then enqueue the case waveform. Explicitly account for intended baseline/final holds versus an accidental mid-sequence starvation. |
| C12 | `show_fault()` stops the fan, including for thermal lockout. Fatal MCU handlers attempt RF stop, then wait for watchdog reset. | The suite asserts these existing containment policies. Register reads establish configured outputs; RF pin voltage, watchdog-reset transitions and hardware-break behavior remain electrical qualification. |
| C13 | Terminal screens feed IWDG. Recovering voltage/temperature does not resume them. The battery override loop suspends ordinary foreground power supervision while interrupts continue. | Hold terminal screens past a watchdog interval to prove intentional latching. Distinguish an override loop from normal runtime and from an automatic-recovery requirement. |
| C14 | **Resolved:** override completion now rereads voltage after the final display transfer, rechecks one-shot/count-floor/14 V eligibility and the reduced-count cutoff before committing or resuming. | Native tests exercise the exact completion refresh and I2C-transfer boundary. Target cases exercise a late voltage crossing and invalid reduced-count voltage, and require unchanged count/zero RF restart attempts on rejection. |
| C15 | NTC failure warning is one check 10 s after the first task call, based on a converted temperature of exactly 0 C. A cold valid sensor can satisfy it, and a later disconnection is not warned again. | Confirm that this is a one-time diagnostic, not continuous open-sensor protection. Test current semantics; a shorted NTC is a hot-temperature case, not this warning. |
| C16 | Synthetic power does not fall when firmware attenuates buck output. Default ECO enforces a 50 W ceiling. | Declare open-loop tests explicitly. Check sustained PWM above a ceiling and restore baseline before slow captures; high synthetic input can keep heating the enabled test bucket. |
| C17 | **Instrumentation resolved:** in the shared test image the ADC arm gate now precedes clock/ADC/OLED initialization. Named failures are consumed before their operation. | Injected boot clock failures exercise the normal latched `CLOCK FAULT` path if subsequent OLED startup succeeds. Real early clock loss may prevent display startup; callback/configuration-error injection does not qualify real CSS/NMI/fallback hardware. |
| C18 | The shared image omits the splash page and, by default, auto-dim. It exports compiled menu count; runners discover pages by title. | Do not assume ten/eleven pages or use historical list slicing. Saved settings are decoded with the current format and compiled features. The older setup-menu plan is historical where it conflicts. |
| C19 | The capture mirror is updated only after successful OLED transmission, and current startup fails if the OLED cannot initialize. | Camera optional does not currently mean OLED hardware optional. Missing/failed OLED cases produce transport/reset evidence, not a fabricated captured fault PNG. Do not treat a stale mirror as the current screen. |
| C20 | Boot stability accepts 100 ms without a new maximum and continues after a two-second timeout; older design wording differs. Runtime shows blinking !HOT! while thermal derating is active. | Keep actual boot timing separate from historical requirements. Thermal display and graph marker indicate applied ECO; by default that means a 50 W ceiling with continuous RF. |
| C21 | **Resolved:** successful battery override consumes modal button events and suppresses further events through debounced release, including bounce/release-generated short presses. | Native tests cover physical/SWD gestures and the next independent press. Target cases require continued runtime after the remaining hold and release, rather than accepting a brief RF resume followed by sleep. |

The shared test image uses a virtual tip input; physical PA5 transitions are ignored in that image. Physical detection must still be qualified with the production image/fixture. The ADC producer header now describes the shared virtual inputs and its default boot behavior.

## Shared build and runner

Build the existing shared image from the firmware directory:

```powershell
pio run -e stm32f042_test
```

Its ELF is `.pio/build/stm32f042_test/firmware.elf`. The existing setup runner and ADC producer both default to this artifact. Menu tests can use `py tools/auto_testing/setup_menu_test.py --no-camera`; they still save synthetic framebuffer PNGs. The shared boot helper in `tools/auto_testing/harness.py` sets startup options and supplies a per-device-calibrated held-last baseline after each reset. It is not a fault waveform or a runtime-ready observation hook.

The entry point is `tools/auto_testing/faults_test.py`:

```powershell
py tools/auto_testing/faults_test.py --camera off
py tools/auto_testing/faults_test.py --camera off --case F05.fast --output-dir .\fault-run-01
py tools/auto_testing/faults_test.py --camera rx0 --calibration .\previous-run\fiducial_calibration.json
```

The runner consumes an already built ELF; it does not rebuild firmware during a run. Default selection runs supported target-simulation cases, reports the full coverage manifest including blocked cases, and keeps hardware qualification separate. `--case`, `--output-dir`, `--elf`, and `--gdb-port` select a case, a new artifact directory, a matching image, and the debugger port. Use `--native` for companion checks or `--native-only` for an offline run. Hardware-only IDs are reported BLOCKED; this runner does not silently execute or pass an unspecified fixture procedure.

The existing `stm32f042_test` environment includes normal main/setup/supervision, ADC FIFO, virtual tip, synthetic buttons, framebuffer capture, and SWD-preserving UART/fan settings. Ordinary splash assets are excluded; the optional calibration fiducial remains available. It bypasses the selected bring-up `test_run()` loop. The fault control/observation hooks compile only into this same environment. Existing standalone hardware bring-up environments are separate manual diagnostics, not alternative automated-test images. Watchdog and core protection thresholds follow production configuration; the shared image explicitly enables the optional bucket and sustained-loss detector. Feature-disabled positive checks skip or run in explicitly enabled native fixtures, rather than adding separate target images.

Reuse and extend existing components rather than copying the menu runner:

| Component | Responsibility |
| --- | --- |
| [build_metadata.py](../../tools/auto_testing/build_metadata.py) | Resolve ELF symbols/load ranges, record image hash, validate NVM bounds and required features; extend shared-build metadata for fault hooks; compiled menu length is already exported. |
| [target_gdb.py](../../tools/auto_testing/target_gdb.py) | One persistent debugger session, short batched transactions, button commands, flash/reset, coherent frames, expected reset/reconnect handling. Preserve `reset_on_connect=False` for reattachment. |
| [harness.py](../../tools/auto_testing/harness.py) | Configure optional boot hold/fiducial before ADC start and prepare a held-last menu baseline after each reset. Fault cases supply their own scenarios and runtime-stage validation. |
| [adc_sim.py](../../tools/auto_testing/adc_sim.py) | Encode seven-channel frames from the compiled lookup table, commit FIFO batches last, report requested/actual samples and execution progress. |
| [frmbuf2png.py](../../tools/auto_testing/frmbuf2png.py) | Decode every captured 512-byte buffer to the upright 32 x 128 view and an enlarged nearest-neighbor PNG. |
| Existing RX0/fiducial/image modules | Optional camera-only imports, calibration, corrected photos, and photo-to-buffer comparisons. Camera-off must not import OpenCV/gphoto2 or open a camera. |
| `faults_cases.py` | Versioned case definitions, independent conversion/window calculations, selected inputs, expected first fault, timing bounds, and recovery policy. |
| `faults_test.py` | Case isolation, preflight, artifact capture, reset handling, a machine-readable result and a reviewable image index. |

### Implemented instrumentation

| Hook | Minimum capability and boundary |
| --- | --- |
| H01: framebuffer capture (implemented) | The shared image exposes the completed even-generation mirror regardless of the optional camera gate. Existing symbol names/API are retained. Never call a work-in-progress drawing buffer a successfully sent frame. |
| H02: case setup and observation | Test-only aligned mailboxes/acknowledgments for boot/runtime-ready stage and RAM-only battery/cell count, calibration, power preference, sleep and optional idle-profile override. Preserve the 128-byte ABI and boot bit positions; idle index is bits 22-24 with enable bit 25. Dimming override bits are ignored when compiled out. Exported feature constants and inactive-duration telemetry select expectations; setup still loads NVM independently. |
| H03: detection snapshot | A compact sticky snapshot of first diagnosis/message, reset permission, target tick/ADC round at qualification/trip, relevant raw/filtered inputs/history, and RF/output register state. Capture at the real detection points without changing their decisions. No I2C or watchdog feed added inside ISR/fatal detection hooks. Include a sticky restart-attempt/eligibility record for C14 so a brief resume cannot disappear before host observation. Save a snapshot before reset when feasible and record hardware reset-cause flags at startup. |
| H04: controlled failures | A named one-shot test selector can force the relevant HAL/API error return before its side effects, or request a foreground hang, software clock-fault callback, or deliberate CPU HardFault. Run the normal containment path. Establish an early arm point before clock/ADC/OLED calls that occur ahead of today's ADC gate. Watchdog-init failure itself can be native-only; do not assume a failed watchdog will reset the device. |
| H05: tip variants | An initial virtual-absent option for F01 boot tests, plus a target-paced burst of 12 software tip EXTI events while the selected input remains present, one after each normal qualification completes, for F02. This exercises the real event counter/timer but is labelled synthetic interrupt activity, not measured electrical bounce. Precise short pulses remain native/fixture cases until a suitable target pulse hook exists. |
| H06: NVM errors | A named fail-before-write selector is available for default target runs. Actual unlock/erase/program/readback/torn-record branches run against the native RAM flash model. Real electrical flash failures need a separate fixture; the target runner never corrupts the user's settings page. |

All hooks compile out of production. A failure selector must be consumed only at its named point and acknowledged, so a case cannot pass because it accidentally set a generic terminal-screen flag. Test selection must not bypass raw detection, stop supervision, accelerate timers, clear fault latches, or silently disable current limiting.

RAM/flash remain acceptance gates. Record sizes from the exact built ELF instead of reusing historical headroom figures: application load must end before NVM at `0x08007C00`, and RAM must fit the 6144-byte device with its linker heap/stack reservations. Retain the 128-frame FIFO, framebuffer mirror and 128-byte fault ABI. No second trace ring is required. Static headroom does not establish measured maximum stack usage.

### F05 automatic recovery acceptance

Production defaults leave the sustained detector and virtual energy model disabled. `stm32f042_test` explicitly enables both to retain F05.trend and F08.bucket positive coverage; native tests separately cover all four switch combinations, disabled model state, retained physical thermal protection, and the independent fast detector. A production build with these switches off must not be judged against an enabled-feature trip expectation.

Keep both detector signatures and their boundary controls unchanged. After first-diagnosis capture, hold filtered input at or below 14.000 V for longer than one second and verify the power-loss screen, RF-off latch, forced-minimum buck, fan-off state, and watchdog servicing persist. Exactly 14.000 V must not start qualification. Restore filtered input strictly above 14.000 V: no restart at 999 ms, eligible at 1000 ms; a dip to 14.000 V or below resets the full interval. Cover delayed recovery, repeated interruptions, tick wraparound, repeated trip/recovery cycles, missing graphics, and the existing manual reset option.

On automatic return, verify no MCU reset or NVM/settings change, retained debug selection/button classification, consumed fault-screen gestures, selective power-loss latch clearing, gradual buck release, resumed fan control, and a normal RF start attempt only in a permitted phase after supervision; an OFF-phase recovery must wait for the next permitted phase. Independent tip/clock/buck-spike faults must remain terminal; battery, qualified temperature, reference and current supervision must authorize any restart. A still-qualified sustained decline must keep outputs stopped. Absolute undervoltage now has its own recoverable `LOW / VOLT / FAULT` screen with the same strict >14 V / 1000 ms qualification and supervised restart.

The harness uses the transient `RESUMING` stage (8) after selective rearming; `RUNTIME` (5) is published only after the full supervised foreground pass. The F05 host runner holds a below-threshold input during evidence capture, then checks delayed return to runtime without sending a reset. Exact engineering-unit/time boundaries are covered by native C tests because target ADC codes are quantized. Preserve first-trigger history before publishing the recovery stimulus. Verify the shared target build fits before executing target cases; historical overflow figures do not describe every subsequent firmware revision. Native tests and the normal production build remain separate validation results.

## Run lifecycle, timing, and scenario construction

### Preflight and isolation

1. Use the same unloaded, checked board arrangement as the setup test: ST-Link/SWD/NRST connected, power stage/handpiece absent as required by that fixture. Use the shared output-inhibited image; require GPIO-low RF and GPIO-high buck attenuation before arming cases. This run establishes software behavior; connected-load tests remain separate.
2. Record ELF SHA-256, source/config revision including local changes, build flags, ABI versions, lookup table, device identity/factory calibration words, effective settings, and expected fixture. Reject an incompatible ELF or flash range touching the reserved NVM page.
3. Back up the raw 1 KiB NVM page before flashing. Default cases leave it byte-for-byte unchanged. Flash application sections only, then verify NVM preservation. If the page has no valid record, use the real default-settings path and log it.
4. Use one persistent OpenOCD/GDB session. A routine status read or refill must not reconnect/reset. All addresses come from the flashed ELF or the documented device memory map; do not depend on optimized private-variable names or debugger function calls.
5. Begin each independent case with MCU reset, clear/verify the selected one-shot hook, configure that case's RAM setup, and preload **one healthy frame**. Request simulation start; its intentional empty FIFO then holds that baseline through boot.
6. For runtime cases, leave `btn_swd_test_boot_hold = 0` and `vision_test_fiducial_enabled = 0` before ADC start, then wait for button idle plus runtime-ready/healthy supervision. For F11, set the boot-hold option before starting ADC and enter setup deliberately. Validate at least one current successful framebuffer send and the absence of unexpected latches before injecting a case.
7. Establish baseline history/reference validity and initial thermal energy as required. Then enqueue the entire bounded waveform while the target is halted once for the batch. Consumption begins on a subsequent real round boundary. A case's time zero is its target-side activation/detection timestamp, not the host enqueue timestamp.
8. Observe the expected first diagnosis, preserve raw and image evidence, exercise permitted recovery (automatic for F05), and only then reset into the next case. Do not rearm power-loss latches using the CT diagnostic helper during an independent case.
9. On interruption, attempt a final control/status snapshot, framebuffer, and NVM read before disconnecting. If the device is in a deliberate hang/reset, preserve host logs and known reset cause instead of waiting indefinitely for a new image.

A reset empties the FIFO and returns to a gate before OLED initialization. Button-triggered reset, watchdog recovery, and host reset are recorded separately. After an expected reset the runner must re-establish setup/baseline; it must not keep interpreting old FIFO counters or a previous frame as current data.

### Healthy baseline and unit conversion

Use approximately 24 V DC input, buck below 25 V, low current/power, valid external NTC temperatures near room temperature, and a plausible MCU temperature/reference. Do not copy one universal MCU/VREF raw value from an example: read the device calibration words (currently temperature calibration at `0x1FFFF7B8`, VREFINT calibration at `0x1FFFF7BA`) and solve for suitable decoded counts using the actual conversion/rounding rules.

Record each requested engineering value, requested raw count, selected 8-bit code, decoded raw count, and independently predicted engineering value. Keep VREFINT and input calibration controlled for non-reference cases. Require the predicted healthy temperatures/voltages to remain comfortably outside unrelated fault limits. Confirm actual filtered values and reference validity after settling.

The initial baseline hold and the final hold of a complete waveform are intentional empty episodes. Predeclare their expected underrun counts. Any undeclared starvation in the middle of a timing/trend case invalidates the stimulus and yields an infrastructure error, not a DUT pass. Repeat counts are complete ADC rounds, not conversions or milliseconds. VREFINT's additional conversion does not consume another frame; preliminary warm-up conversions do not consume a full repeat.

### Time and history rules

- Read progress/ticks in short batched halts before and after qualification; avoid continuous SWD polling during a 300 us tip debounce or a short voltage trend. Use H03 for fast observations. Log host halt intervals and whether a halt landed inside an ISR.
- Assert continuous durations from the **first filtered excessive reading observed by supervision**, not from the raw waveform edge. A safe evaluated reading cancels thermal/current qualification. Native tests cover exact integer boundaries and tick wrap; target runs use margins accounting for superloop/display scheduling.
- Calibrate/record actual ADC rounds versus target milliseconds during a healthy interval, but do not rewrite conversion-count history as a fixed wall-clock sampler. After a clock fault, discard the earlier cadence estimate.
- For isolated battery/low-voltage tests, avoid fast drops of 85 counts and avoid a continuously declining sustained window. Use separated voltage plateaus, normally at least ten 128-round history publications per transition. Verify the actual window model rather than assuming a low final voltage identifies the intended fault.
- For sustained power loss, align or independently model the 128-round averaging phase. Capture the seven history entries used at detection. Changing raw values at arbitrary host intervals and inspecting only the final voltage is insufficient.
- For leaky-bucket tests, reset to empty for independent cases, then drive a declared power history. Include preloaded-energy and interrupted-power sequences separately. Missed 50 ms updates use the latest power for each replayed period; debugger pauses cannot reconstruct past power samples.

## Framebuffer and optional camera evidence

For every display-capable observation, save the following under a stable case/step prefix:

- `framebuffer.bin`: the exact 512 captured bytes, with send generation, target tick, boot/case generation, and transport-success status in adjacent JSON.
- `framebuffer_native.png`: upright 32 x 128 pixels; `framebuffer.png`: 8x nearest-neighbor enlargement (256 x 1024). Use `save_framebuffer_png()`; do not regenerate the display from expected message text and label it captured evidence.
- State/measurement JSON with requested/decoded stimulus, filtered readings, reference, history/model state where relevant, expected/observed diagnosis, reset policy, output register snapshot, and timing bounds.
- A report/index linking the before/qualification/first-fault/held-fault/recovery images. Preserve images for failures and unexpected screens too.

Accept the mirror only when its generation is nonzero, even, unchanged across the copy, belongs to the current boot, and advances after the action being observed. Retry an interrupted copy by briefly resuming. A fatal initialization path or failed display transport can legitimately have **no current frame**: save an explicit `NO_FRAME` result and causal evidence. An old frame may be retained as labelled pre-fault evidence, never as the current fault screen. Recovery cases may produce their next current frame only after the runner releases the new boot gate.

Fault screens move every five seconds, voltage can change, short messages expire, and some warnings choose random phrases. For deterministic terminal text checks capture the initial unshifted frame, then use approved glyph/layout fixtures or normalized text-region comparisons for later frames. Independent expected messages come from the reviewed case definitions, not the observed `show_fault()` argument. Keep variable voltage/graph/progress regions explicitly masked or modelled; do not mask away the fault text. Initial fixtures need human image review; mark visual approval pending rather than declaring a self-generated golden image correct.

Default `--camera off` runs must never initialize camera tools, require a calibration JSON, or count missing photos as failures. Expected optional camera capture failure records a camera warning and retains framebuffer/state results; `--require-camera` may make it an evidence failure when explicitly selected. With `--camera rx0`, reuse the existing corrected-photo comparison and keep full/rectified/difference images. Pause stimulus at a declared stable hold for photography when appropriate; do not stall CPU time to take a photo. Fast transients require stored framebuffer/target evidence. Camera comparison establishes lit-screen agreement with a framebuffer, not fault-detection correctness or shutdown latency.

## Case matrix

Every row includes a healthy control and preserves first-fault evidence. "Native" or "needs hook" is a planned coverage boundary, not an implementation result. `/` separates OLED lines; the voltage header is additional.

### Fault diagnoses

| Case | Stimulus and negative/boundary controls | Expected behavior / evidence |
| --- | --- | --- |
| F01.runtime | From healthy runtime, write `tip_swd_test_command=0`; record command acceptance, EXTI, timer qualification, completion. | After qualified absence, RF stops in the timer path and `TIP / FAULT` becomes terminal; button reset allowed. Selected virtual input must also inhibit RF restart. Same-level present command must not create an edge. |
| F01.boot/init/glitch | H05 initial absence; H04 timer-base and one-pulse initialization errors. Native physical-input tests: low at boot; low/high pulse shorter than debounce; absence still present at expiry; high racing expiry; reset while qualifying. | Boot absence and init failure fail closed. A short pulse with high at the sample must not latch absence. Return to high after a confirmed disconnect must not auto-clear it. Existing SWD level mailbox alone cannot prove glitch rejection. |
| F02.activity | H05 generates enough serviced software EXTI events with selected input present to make the decaying counter exceed 10; timer qualifications complete normally. Native controls at snapshots 10/11, 100 ms decay, long task gap, and edges masked during qualification. | `TIP / DETEC / MALFU`, terminal/button-reset. Use the pre-decay snapshot rule. Do not let a sampled absence turn this into F01; do not simulate it by writing the terminal diagnosis directly. |
| F03.runtime | H04 requests the software CSS callback during runtime with physical RF inhibited and other inputs healthy. | Clock/emergency latch, RF stopped, restart inhibited, `CLOCK / FAULT`, button reset. This proves callback containment; it does not prove crystal failure detection, real NMI entry, or HSE-to-HSI switching. |
| F03.boot/hardware | One-shot oscillator configuration error, clock-switch error, and failed ready/source verification before the relevant boot step. Separate real CSS/crystal fixture case. | Record the stage reached and agreed screen/reset outcome (C17). Native tests cover each return branch. Physical clock-loss/fallback qualification remains separate; callback injection is not a substitute. |
| F04.spike | Positive enabled-fixture test: healthy raw 704, a full round at representable 710, then healthy input; repeat with logical RF off. Exact 704/705 boundary is native-only with this table. | With the detector enabled at 25 V, one raw sample >=705 latches RF off and reports terminal VOLT / SPIKE / FAULT; button reset allowed. Healthy input cannot clear it. Default PWRMGT_BUCK_VOLTAGE_SPIKE_MV=0 means detection/stop are compiled out and this positive target case is skipped, not timed out waiting for a screen. |
| F05.fast | After stable history, use exact representable positive pair 676 -> 591 (85 counts); negative pair 665 -> 581 (84 counts) in an independent case. Include no-history and upward-change native controls. | Positive pair latches RF off in ADC ISR and reports the recoverable `POWER / LOSS / FAULT` screen; negative pair alone must not trip the fast detector. Capture newest completed average and current raw sample, not just two filtered readings. |
| F05.trend | Seven consecutive 128-round plateaus at representable counts `698, 693, 687, 682, 676, 670, 665`, after phase alignment/modelled publication. Keep all individual drops below fast threshold. Controls: one step then plateau, rising/flat comparison segment, insufficient history, insufficient total/segment drop (native exact boundaries where needed). | Foreground detects a total fall >=8 with every 20 ms-declared comparison segment falling >=2. `POWER / LOSS / FAULT`, automatic recovery after input qualification, with manual button reset still allowed; fast latch must remain false at the sustained-only first diagnosis. Save actual history ages 6/4/2/0 and confirm averaging-phase effects. |
| F06.cutoff | Each of six battery modes with known cell count: slowly move below `N * minimum_per_cell`; test disabled battery checks and supported count floors. Native exact equality and chemistry/cell-guess ambiguity tests. | Low-battery screen occurs before absolute undervoltage when its cutoff wins. Equality passes. Record mode, selected/guessed count, actual millivolts, and whether override is offered. Do not infer individual cell voltages were measured. |
| F06.override | Choose a valid count above the minimum (for example a genuine 5S LiPo guess), then a stable low-battery voltage >=14 V. Hold 6000 ms; also release early. Repeat cutoff after the one allowed correction; test count at minimum, already-used override, and input below 14 V. | Incomplete hold resets progress. Completion reduces count exactly one and resumes only if eligible; override cannot be used twice. Ineligible/fatal screen is `LOW / BATT / !!!!!` with no button reset. Save empty/partial/full bar and resumed/fatal frames. Include C14's controlled completion-boundary voltage change and C21's consumed-gesture sleep/mode-change regression; require continued normal supervision after the gesture. |
| F07.undervoltage | Battery disabled; separated plateaus approaching 14 V without competing F05. Native 13999/14000 mV and interrupted recovery. | LOW / VOLT / FAULT only below 14000 mV. Outputs stop; recovery requires filtered input strictly >14000 mV continuously for 1000 ms. Equality or a dip restarts qualification. Repeat cycles without reset, retain preference/debug/history and consume fault-screen buttons. Recheck interlocks/low-input ECO derating before fan/buck release and RF restart in an allowed phase. Manual reset remains permitted. |
| F08.sensor | Independently drive NTC1, NTC2, and the calibrated MCU channel above 110 C for >1000 target ms after filtered crossing; use a 1500 ms qualified hold for the target smoke case. Controls at/below 110 C, short excessive interval, a safe reading cancelling timing, and hottest-sensor handoff. | Derating above 80 C precedes `CIRKT / TOO / HOT / FAULT`; terminal/button-reset. Native tests assert exact 110/111 C and 999/1000 ms. Confirm the maximum across sensors/bucket, not each sensor's separate timer. |
| F08.bucket | Enabled shared/native fixtures only: cool real sensors and declared 60/80/100 W histories with modeled initial energy; responsive-plant variant accounts for RF OFF and attenuation. | Virtual warning/derating and >110 C qualification only when injected history supports them. ECO requests 500/500 RF by default; fixed ADC watts do not fall in response. An 81 C model-cap fixture cannot cause bucket-only shutdown. Normal defaults compile this model out; do not require its trip there. |
| F09.reference | Solve lower/upper implausible reference estimates while adjusting the other simulated channels so earlier checks stay healthy. Include valid range, no estimate yet, zero VREF sample, and saturation native branches. | `VOLT / REF / FAULT` outside 2475..4125 mV inclusive, button-reset. Log sparse VREF sampling/filtering and the actual first fault. Exact engineering boundaries can require native tests. Zero VREF result does not necessarily change the previously cached conversion reference to zero. |
| F10.short | Stable simulated current >5800 mA for >5000 ms after filtered crossing (target hold with margin), then safe control/current interrupted before full qualification. Keep input/buck/temperatures safe and measured power low enough to avoid bucket interference. | `SHORT / CIRKT / FAULT`, no button reset. Normal 5200 mA limiting should already be active; the injected measured current deliberately ignores its plant response. Native 5800/5801 mA and 4999/5000 ms, timer-cancel, forced-minimum, and the coupled-enable configuration contract cover C01. |
| F11.save | Enter setup, make a real change, arm H06's fail-before-write selector, request Save. Controls: Exit No Save, unchanged valid save, successful changed save on a separate approved fixture. Native erase/unlock/program/readback/final-validation failures. | `SAVE / FAULT`, no successful-save reboot, button reset allowed. Default injected failure must leave NVM byte-for-byte unchanged. A full journal normally erases/reuses rather than faulting by itself; invalid old records can fall back to a valid record/defaults without this screen. |

Battery cutoff parameters to cover explicitly (millivolts per cell):

| Mode | Minimum | Maximum for guessing | Supported range |
| --- | --- | --- | --- |
| LiPo / LiPo Safe | 3100 / 3300 | 4200 | 4S-8S |
| LiHV / LiHV Safe | 3100 / 3300 | 4350 | 4S-8S |
| LiFe / LiFe Safe | 2500 / 2750 | 3650 | 6S-9S |

### Warnings and limits

| Case | Stimulus / controls | Expected result |
| --- | --- | --- |
| W01.NTC | One sensor, then both, converting to 0 C at the one-time check >=10 s after first `ntc_task()`. Healthy controls; late disconnection; warning-disabled variant. | `NTC / SENS / FAULT` owns the display for 3000 ms, then normal display returns. No warning-induced RF/buck shutdown. A short/low-count sensor instead exercises hot-temperature protection. Capture inside the warning interval without depending on a camera exposure. |
| W02.high-input | Stable input on either side of 32000 mV with battery disabled and buck voltage independently held safe. | Warning overlay at/above 32 V, 500 ms visible/hidden phases, clears below 32 V. Accept only the defined random message set. No associated terminal shutdown. Control display ownership so a transient message is not mistaken for a missing warning. |
| L01.thermal | Physical sensors and enabled bucket around >80 C entry / <=75 C release; hysteresis and blocked requests. | Applied ECO enforces 50 W, selects its rise scale and retains attenuation for gradual release. Preserve preference, current limiting, feedback and blinking !HOT!. |
| L02.low-input | Stable input below 19 V, hysteresis band and >=20 V; avoid competing supply faults. | Applied ECO enforces 50 W and selects its rise scale. Preserve attenuation, preference, hysteresis and feedback; thermal messages have priority. RF resumes through normal supervision/interlocks. |
| L03.current/power | 5200 mA boundaries and previous samples; each wattage ceiling; rise peaks in every mode; release, derating and recovery. | Configured CCR24 jump / +4 per eligible 2 ms attack up to 31; rise may request 32. Take maximum requests, retain 32 while over a limit, and release one count per eligible 50 ms only below both limits. Rise scales are 20/25/30 W. Preserve short-circuit timing and forced minimum. Observe continuous logical RF with physical outputs inhibited; optional cycling has native coverage. |

### MCU/fatal recovery paths

| Case | Injection and controls | Expected result and evidence limit |
| --- | --- | --- |
| R01.foreground | Target-side commanded foreground hang after healthy operation, with interrupts still running; separately debugger halt; native suppressed boot seed. | No foreground feed, eventual IWDG reset, reset-cause evidence and return to the appropriate boot gate. Nominal 4 s is not an exact assertion: existing comments give roughly 3.2-5.33 s on F042. Allow measured scheduling/host tolerance. Watchdog alone need not stop RF before reset. An ordinary terminal screen must remain stable beyond this window because it feeds IWDG. |
| R02.ADC-init | Named failures for ADC HAL init, calibration, both channel-config calls, and interrupt start; native equivalent branches. | RF emergency latch/stop, ADC IRQ disabled, no feeds, watchdog reset if already running. No `ADC FAULT` OLED message is implemented. Do not wait for a framebuffer that cannot exist at that boot stage. |
| R03.application-init | OLED initialization failure, controlled missing graphics for setup helper, native watchdog-init failure. | `Error_Handler()` stops RF, disables interrupts, does not feed. Existing watchdog gives reset; failed watchdog initialization gives no assured eventual reset. Current runtime OLED disconnection is not an implemented terminal diagnosis. |
| R04.HardFault | Target action deliberately executes a CPU fault instruction, rather than merely setting an RF latch. | Real exception handler attempts RF stop, disables IRQs, waits for IWDG. No dedicated screen. Preserve pre-fault frame and reset cause; snapshot evidence may be unavailable if the stack is unusable. |
| R04.break | Separate CPU-lockup and SRAM-parity-to-TIM1-break qualification with an agreed fixture/injection mechanism. | Confirm physical RF output suppression independently of `rfgen_active`. Route/register inspection or a HardFault-handler test does not prove hardware break behavior. Mark BLOCKED without the required physical evidence. |

No new diagnoses are invented for fan stall, reflected RF power/load mismatch, tip temperature, current-sensor failure, runtime ADC staleness, or ADC overrun. Indirect listed faults are not proof of separately detecting those conditions. Sleep (`ZZZZZ`) and the boot `.....` display are controls, not fault coverage IDs.

## Shared shutdown, priority, and recovery assertions

For every normal terminal `show_fault()` case, verify after entry:

- RF software state inactive; PB1 configured low and TIM1 output/channel/counter disabled as applicable.
- Buck forced-minimum state and PA6 high/full attenuation.
- Fan commanded off; because this build deliberately preserves PA13/SWD, observing zero command is not a live fan-pin/airflow test. Never run the normal fan-takeover assertion on the debugger pin while requiring SWD.
- Expected message and reset permission agree with the independent case definition; the screen remains terminal when the stimulus returns healthy.
- With reset permitted, a new short press resets. A button already down on entry must first release for at least the 50 ms debounce. With reset forbidden, short and long presses do not reset; host reset is cleanup and cannot count as successful button recovery.

Register readback establishes commanded/configured safe states, not analog pin voltage or nanosecond shutdown latency. Fatal handlers in R cases have their narrower expected containment, per C12.

| Recovery class | Cases |
| --- | --- |
| Terminal, new button press resets | F01-F04 (when enabled), F08-F09, F11 |
| Terminal, no button reset | F10 and fatal/non-overridable F06 |
| One permitted hold may resume | Eligible F06 override, with a one-cell correction |
| Recoverable input faults, button reset also permitted | F05/F07: filtered input >14 V continuously for 1000 ms, then supervised restart in an allowed RF phase |
| Automatic/nonterminal release | W01/W02 and L01-L03 under their own duration/hysteresis/phase rules |
| Watchdog-driven reset where running | R01-R04 software fatal/hang paths, with the stated watchdog-init limitation |

Add combined-condition cases with assertions about the **first evaluated qualifying condition**, not eventual messages after cleanup. Current runtime order is tip activity -> tip latch -> clock latch -> buck spike -> fast/sustained power loss -> battery -> absolute undervoltage -> qualified thermal shutdown -> reference -> low-voltage derating/current/short-circuit supervision. Pending current/temperature timers do not win until qualified. Cases include:

- Latched tip plus buck spike: tip message, both interrupt/output evidence retained.
- Buck spike plus fast input loss: buck-spike message.
- Battery cutoff plus absolute undervoltage on the same pass: battery path.
- Raw power loss plus battery cutoff: power loss if its latch/window already qualifies; battery may win an earlier pass before the trend matures.
- Thermal qualification plus implausible reference on the same pass: qualified thermal branch first; otherwise reference can win.
- High current alone: limiting first, terminal short only after five continuous seconds; avoid incidental high simulated power causing F08 first.

Use native tests or a controlled target barrier when an exact same-pass combination is required. Sequential SWD writes while the target runs do not create simultaneous conditions. Do not infer a precedence change from a case that reached the first terminal screen before the second condition was injected.

## Implemented coverage and validation

The case catalog has 63 target cases, eight native boundary/failure IDs and five explicitly blocked electrical IDs. `--list` is the authoritative selection list. The runner supplies reports, raw frame dumps, upright native/enlarged PNGs, coherent generation checks, per-case isolation, held-last waveforms, NVM preservation, named failure acknowledgments and optional camera comparisons.

The native companions run actual ADC/FIFO, tip, button/battery, supervision, buck control, bucket, NVM journal and watchdog code with mocked peripherals. They also check the host ABI, conversion/window model, camera-off imports and report/image artifacts. Passing native checks does not certify the 63 target scenarios, GPIO electrical behavior, or visual layout approval.

The case tables remain the behavioral contract, including fixture-dependent and additional controls. Per-case detailed assertions are in `faults_cases.py` / `faults_test.py`, with exact integer/timing cases in `test`. Runtime ADC/clock/OLED initialization failures use named target injections; watchdog-start return handling and flash failures use native models. Hardware-only procedures remain BLOCKED until their fixture exists. The initial image review remains PENDING; no self-generated golden image is approved automatically.

## Reporting and acceptance

Each case has a stable ID, evidence level, prerequisites/hooks, input sequence, expected contract version, maximum wait, cleanup, and a result of PASS, FAIL, BLOCKED, NOT_RUN, or ERROR (infrastructure/stimulus invalid). Keep visual-review status and optional camera status separate. A known discrepancy is reported as FAIL against the agreed intent or BLOCKED pending a decision, not silently skipped or converted to PASS.

Save `report.txt`, machine-readable results/manifest, GDB/OpenOCD logs, original/final/interrupted NVM dumps, per-case requested/actual stimulus, target snapshots, framebuffer binaries/PNGs, and an HTML or Markdown image index. Use a new timestamped directory, never overwrite prior evidence. Record firmware resets/reconnects, timing uncertainty, intentional FIFO holds, and any unavailable current frame.

Default runs must return success only when every selected supported case passes and no mandatory evidence is missing; failures return a nonzero status. A partial selection or unavailable hook must be plainly reported as incomplete coverage. Default NVM contents must remain unchanged. Separate approved write-fixture runs restore/verify settings according to their own contract and report raw-page changes rather than promising byte-for-byte restoration through ordinary journal saves.

Resolved expectations are C01/C02/C06/C14/C17/C21 and the shared-build gaps C03-C05/C18. The suite keeps current fan-off containment, one-time NTC warning semantics, and the actual graph/limit UI (C12/C15/C20). C20 still records older design wording that differs from implementation; this suite does not silently change that behavior. Physical clock loss, RF transitions through reset, parity/lockup break and real closed-loop regulation require fixture evidence.

Offline checks and build verification accompany this implementation. Target and optional-camera runs have not yet been performed for the new suite; use the README smoke command first. Normal application behavior outside this fault scope remains for the future `rest of it` suite.
