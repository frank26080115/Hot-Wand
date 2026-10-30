# Automated firmware testing

Run commands from the `firmware` directory. All SWD suites use the same
full-application image, `stm32f042_test`: ADC FIFO, buttons, virtual tip,
sent-framebuffer capture, and fault hooks. Camera calibration is a runtime option.

## Fault suite

```powershell
pio run -e stm32f042_test
py tools/auto_testing/faults_test.py --list
py tools/auto_testing/faults_test.py --camera off
```

Default: flash the matching ELF, reset between cases, and run 63 target cases
covering F01-F11, warnings W01-W02, limits L01-L03, and software fatal/watchdog
recovery. Use an unloaded board with a working OLED, ST-Link and NRST connected.
The shared test image physically inhibits RF and holds the buck attenuation
pin GPIO-high (minimum output) throughout startup, runtime, setup and faults.
Neither RF start entry point can enable TIM1/PB1, and PA6 is never connected to
TIM3 PWM. Logical power/current limiting and requested PWM still run for tests.
The runner checks both GPIO modes/levels and disabled RF timer outputs.
Fault ABI 2 and the compiled buck-spike threshold symbol are required; rebuild/flash the shared image to replace older images
that permitted real outputs. The clamp is established at firmware startup;
GPIO levels during MCU reset itself depend on the board's hardware bias.
Physical PA5 is replaced by the virtual tip; physical detection and shutdown latency require
separate electrical qualification.

Start with a smaller selection when bringing up the fixture:

```powershell
py tools/auto_testing/faults_test.py --case control.healthy
py tools/auto_testing/faults_test.py --case F05 --case F08 --output-dir .cache/fault-review-01
py tools/auto_testing/faults_test.py --native --case F10
```

`--case` accepts an exact ID, family prefix or glob, and can be repeated.
`--elf` selects a matching shared-build ELF; `--gdb-port` chooses the OpenOCD
port. `--no-flash` verifies the existing image against that ELF. `--fixture`
records the fixture name. Default output is a new `faults-TIMESTAMP` directory
in the current directory. An explicit output directory must not exist.

The runner backs up the entire NVM page before flashing, checks preservation
after flashing and each case, and saves the page again at shutdown. Ordinary
cases use RAM settings. The save-error case fails before flash unlock/erase/
program; native tests exercise actual journal failure branches against RAM.
Successful changed-setting saves retain the setup suite's own NVM contract.

Each waveform is one atomic FIFO batch, including across wrap. Baseline/final
held frames are intentional. Durations are complete ADC rounds; thermal/current
qualification uses the MCU clock. The report records actual LUT encoding and
predicted settled engineering values. Slow voltage ramps avoid accidental
power-loss faults; the sustained-loss case records the actual evaluated window.

## Reports and images

Open `index.html` in the generated directory to review images.

| Artifact | Contents |
| --- | --- |
| `report.txt` | Progress, assertions, failures and artifact location. |
| `results.json` | Full coverage manifest, selection, ELF hash/revision, calibration, stimuli, snapshots, reset causes, timing and visual-review status. |
| `index.html` | Image gallery and links to text/JSON reports. |
| `CASE/adc_waveform.png` | Seven-channel ADC waveform overview, including held-last periods, with an estimated time axis. |
| `CASE/adc_sequence_NN.png` | Zoom of each FIFO batch so short spikes and voltage trends remain readable. |
| `CASE/adc_waveform.csv`, `CASE/adc_waveform.json` | Decoded values, round offsets/repeat counts, estimated seconds and timing/activation metadata. |
| `CASE/NN_LABEL_framebuffer.bin` | Exact 512 bytes from a coherent, newly transmitted framebuffer. |
| `CASE/NN_LABEL_framebuffer_native.png` | Upright 32 x 128 synthetic OLED image. |
| `CASE/NN_LABEL_framebuffer.png` | The same pixels enlarged to 256 x 1024 with nearest-neighbor scaling. |
| `CASE/NN_LABEL.json` | Diagnosis, ticks/rounds, raw trigger, filtered/history values, output registers, ADC progress and frame generation. |
| `gdb.log`, `openocd.log` | Debugger commands/replies and server output; halt durations are in the manifest. |
| `nvm_before.bin`, `nvm_at_stop.bin` | Original/final 1024-byte settings page. |
| `native-SUITE.log` | Native companion output when requested. |

ADC plots use the actual LUT-decoded counts fed into the acquisition pipeline,
before filtering. Time is relative to the first waveform enqueue, estimated from
that case's measured ADC rounds versus target milliseconds. For setup/fault
screens without a live foreground tick, the fallback measures ADC progress
against host time with debugger halts removed. Ordinary batch activation can
differ by one ADC round; the sustained-trend batch uses its firmware marker.
The graph metadata states the time basis. No fixed conversion rate is assumed.

Plots preserve frame repeat durations and final holds through the last observed
progress; queued but unobserved future is shaded gray. They describe programmed
injection, not a measured trace of filtered values or exact VREFINT conversion
times. Cases with FIFO batches save their plots after cleanup, including failed
cases when data is available. Graphs and sequence zooms are linked from the HTML
report. Rendering runs after the target returns to the reset gate, so it cannot
delay a trigger sequence or qualification interval.

Functional PASS does not imply human visual approval: `visual_review` begins
`PENDING`. Unreviewed images are not accepted as goldens. Early ADC/OLED/fatal
failures can legitimately produce `NO_FRAME`; old pixels are never presented
as a new fault screen.

Results distinguish `PASS`, `FAIL`, `ERROR`, `NOT_RUN` and `BLOCKED`. The manifest
also contains eight native cases and five electrical cases. Electrical cases
remain `BLOCKED` without physical fixture evidence. A successful default run
means its selected target cases passed, not complete electrical coverage.

`SKIPPED` means a selected check does not apply to the compiled image. In
particular, F04 spike and below-threshold checks are skipped when buck-voltage
spike detection is compiled out. The manifest records the actual ADC trip
threshold (zero means disabled); expectations never come from the current
source configuration alone. Rebuild the shared image after this metadata change.

Exit codes: 0 selected checks passed; 1 failed/blocked selected cases; 2
infrastructure failure. Unselected target cases remain `NOT_RUN`.

## Camera is optional

`--camera off` is the default. It saves all synthetic images without importing
OpenCV, invoking gphoto2 or accessing a camera. The OLED is still required.

```powershell
py tools/auto_testing/faults_test.py --camera rx0 --case F01.runtime
py tools/auto_testing/faults_test.py --camera rx0 --calibration ./previous-run/fiducial_calibration.json
```

RX0 mode reuses fiducial calibration and saves full, rectified, classified and
difference images. Moving text can differ during exposure, so comparison
metrics are diagnostic and separate from functional results. Missing optional
photos record warnings; `--require-camera` makes camera evidence mandatory.
Slow photography is deferred during the battery hold timing window.

## Offline companions

These commands never connect to or flash hardware:

```powershell
py tools/auto_testing/faults_test.py --native-only
py -m unittest discover -s test/faults -v
```

`--native-only` runs `test/adc_sim`, `test/tipdetect_swd`, `test/button_modal`,
`test/harness` and `test/faults`, retaining logs in the normal report directory.
They compile actual firmware C into native DLLs, checking FIFO/ISR behavior,
unrepresentable ADC count boundaries, tip debounce/activity, battery gesture/
eligibility, thermal/current timers and tick wrap, precedence, bucket energy,
journal errors/torn records, watchdog startup returns, ABI and image/report code.

Use Windows Python with `numpy`, `Pillow` and `matplotlib` (ADC waveform plots). Native C tests require LLVM `clang`
and `lld-link` on PATH. PlatformIO supplies ARM tools, GDB and OpenOCD;
installation paths are in `build_metadata.py`. Camera mode additionally needs
OpenCV and the existing gphoto2 installation.

## Existing menu suite and low-level producer

```powershell
py tools/auto_testing/setup_menu_test.py --no-camera
py tools/auto_testing/adc_sim.py --help
py tools/auto_testing/frmbuf2png.py framebuffer-hex.txt oled.png
```

The menu runner retains its report/image workflow and settings behavior.
Debugger halts retry one interrupt if a firmware reset consumes the first request.
A confirmed stop is still required before inspecting memory; a persistent
transport failure remains an ERROR.

The ADC producer can attach without resetting a streaming sequence, enqueue
JSON frames, request start and read progress. Its module comments and `--help`
describe the seven-channel format and SPSC publication protocol.

## What each file does

| File | Responsibility |
| --- | --- |
| `faults_test.py` | Fault scenarios, isolation, assertions, evidence, cleanup, reporting and CLI. |
| `adc_waveform.py` | ADC FIFO timeline reconstruction, measured time conversion, seven-channel plots, sequence zooms and CSV/JSON export. |
| `faults_cases.py` | Case IDs/contracts, calibrated stimuli and independent history model. |
| `fault_target.py` | Versioned fault mailbox/snapshot ABI, RAM boot settings, reset and coherent frames. |
| `setup_menu_test.py` | Existing menu navigation/editing, save/discard and report suite. |
| `adc_sim.py` | ADC FIFO producer, firmware LUT encoding, atomic batches and progress CLI. |
| `harness.py` | Shared boot gate, optional setup hold/fiducial and calibrated menu baseline. |
| `target_gdb.py` | Persistent GDB/OpenOCD, batched halts, memory access, flash/reset and buttons. |
| `build_metadata.py` | Tool locations, ELF hash/symbols/load ranges, NVM checks and menu discovery. |
| `frmbuf2png.py` | Framebuffer decoding and synthetic PNGs; also a hex-dump CLI. |
| `eyes_rx0.py` | Optional RX0/gphoto2 capture, focus/exposure and calibration. |
| `fiducial.py` | Optional camera fiducial detection, geometry and alignment. |
| `image_processing.py` | Optional photo-to-framebuffer comparison and difference images. |

Firmware ABI comments are in `inc/adc_sim.h` and `inc/fault_test.h`. The fault
contract and remaining specification differences are in
[the fault plan](../../doc/test-plans/faults-test.md).
Normal application behavior is covered by the functional suite described below.


## Functional tests (normal application behavior)

Use the same `stm32f042_test` image. Rebuild it to include the new `!HOT!`
indicator before running these tests; physical RF remains off and the buck
pin stays GPIO-high/minimum. No new test firmware environment is needed.

```powershell
pio run -e stm32f042_test
python tools/auto_testing/functional_test.py --list
python tools/auto_testing/functional_test.py --case FUN.boot.healthy.0 --camera off
python tools/auto_testing/functional_test.py --native --camera off
python tools/auto_testing/functional_test.py --native-only
python tools/auto_testing/functional_test.py --case FUN.display.hot --no-flash
```

The default target run includes a real five-minute sleep and dimming delays.
`--long` adds 15/30-minute sleep, sleep-off, inactivity-reset and ten-minute
endurance cases; allow over an hour for the extended suite. Explicit `--case`
selection can run a long case by itself. Default runs do not write settings,
and verify the 1 KiB NVM backup after flashing, after each case and at cleanup.
The runner flashes by default; `--no-flash` verifies a matching attached ELF.
Close other debugger sessions before running. Camera is off by default and
optional RX0 arguments match the fault runner.

Open `functional-TIMESTAMP/index.html` for the framebuffer/ADC graph gallery.
The run also contains `report.txt`, `results.json`, raw framebuffer binaries,
native/enlarged PNGs, ADC overview/sequence PNGs and CSV/JSON, debugger logs,
NVM backups, and `native-functional.log`/`native-results.json` when selected.
Waveforms are split into `boot_N` subdirectories for cases spanning resets.
Contrast observations have separate JSON sidecars: framebuffer pixels cannot
show OLED brightness. Optional photos are diagnostic; visual review remains
PENDING until reviewed. Native groups list the exact component tests supporting
them. Electrical exclusions, deferred plant modeling and the unresolved
up-to-300-ms versus two-second boot-wait wording remain visible as BLOCKED.

A selected check must pass for exit 0; ordinary case failures return 1,
infrastructure/safety aborts return 2, and user interruption returns 130.
Output-clamp/SWD-ownership or NVM-preservation failure aborts after cleanup.
Unselected cases remain NOT_RUN, including long cases; a default passing run
is not full electrical or extended-duration qualification.

| File | Responsibility |
| --- | --- |
| `functional_test.py` | Functional target exercises, selection/long groups, contrast observation, safety aborts, and per-boot waveform partitioning; inherits the shared fault-runner lifecycle/report helpers. |
| `functional_cases.py` | Independent functional contracts, accepted power sequence, calibration/healthy-pack oracles, target/native/electrical coverage manifest. |
| `functional_native.py` | Offline component runner and exact per-test family evidence; skipped/failed/unexecuted checks cannot become a PASS. |
| `../../test/functional/test_native.py` | Actual-C startup/main-loop/display/button/power/fan/UART/ADC/battery/storage/utility tests under peripheral models. |
| `../../test/functional/test_host.py` | CLI, report images, camera-off, coherent frames, safety aborts, and waveform reset isolation regressions. |
| `../../test/functional/*_bridge.c` | Native wrappers around production C; RAM output models and bounded host observations, without target hardware access. |

The detailed contract and reviewed differences are in
[functional-test.md](../../doc/test-plans/functional-test.md). Existing fault
and menu suites retain their own coverage responsibilities.
