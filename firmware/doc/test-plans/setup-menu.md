# Setup Menu hardware test plan (13.56 MHz Hot-Wand)

## Purpose and boundaries

Exercise the Setup Menu, OLED, button event state machine, reset paths, and nonvolatile settings on the STM32F042 board. The AI operator will request timed **virtual** button presses through SWD; the MCU will generate the down and release transitions using its own millisecond clock. The AI will capture and inspect RX0 II still images of the OLED with `eyes_rx0.py --crop-to=800` at each observation point. This is a future procedure, not a record of tests already run.

The board has no L3, L4, L5, or L6 and no handpiece. Keep that configuration. Do not infer RF heating, fan electrical performance, tip detection, or loaded power-stage behavior from this test. The physical switch, PA7 electrical path, and EXTI edge handling are **outside this AI-run plan**; the human will verify them separately with the OLED button bring-up test. A passing virtual-button run does not constitute a pass for the physical button.

## Image and equipment

- Build and use `stm32f042_setup_swd` from `firmware/platformio.ini`. It includes `main.c` and `setup_menu.c`, sets `BTN_SWD_TEST_HARNESS=1`, keeps SWD available on PA14, and defers fan ownership of PA13. It skips the separately selected `test_bringup_oled_button()` in `test_run()`. The ordinary `stm32f042` image currently runs that bring-up test and will not reach the menu. The `stm32f042_cubeide` image excludes the menu entirely.
- Use the ST-Link V3MINIE with the board's normal SWD, NRST, target-voltage reference, and ground connections. No UART solder pad, Bridge GPIO, PA7 wire, or external button driver is needed.
- Use the board's already verified regulated supply and current limit. Fix the Sony RX0 II in position with PC Remote and still-photo mode, JPEG capture enabled, manual focus, and exposure set so individual OLED pixels are clear. Keep the camera connected for PTP capture and ensure its card and the host have room for the stills. Close software that might claim the camera. Optional high-impedance meter or scope probes may inspect PB1 RF drive and PA6 buck attenuation.
- **User preflight before the AI starts:** From the repository root, run `py firmware\doc\test-plans\eyes_rx0.py --crop-to=800 --view`. Confirm the saved image shows the entire OLED within the centered 800-pixel square, with readable text and no blooming that obscures glyphs. Report that the view passes, then keep camera position, focus, and exposure fixed. If it fails, adjust the camera and repeat this check before starting the test.
- Record firmware image hash, build flags, board revision, V3MINIE model/firmware, supply voltage, and date. Before flashing, preserve the current firmware and the 1 KiB NVM page at `0x08007C00` if the connected tools permit readback. Use a flash operation that preserves that reserved NVM page; do not mass erase it. Do not write directly to NVM during this test.
- Before any save, inventory every existing setting from the OLED and retain that inventory for restoration. A valid saved record takes precedence over defaults. A raw NVM backup is additional evidence; restoring settings through the menu will append a record rather than reproduce the original page byte for byte.

## SWD button protocol

The harness in `firmware/src/button.c` exports three volatile 32-bit symbols in the test ELF:

| Symbol | Meaning |
| --- | --- |
| `btn_swd_test_command_ms` | Host writes one duration in milliseconds. `0` releases the automatic boot hold; a positive value requests one press. `UINT32_MAX` means no pending command and is restored by the firmware when a command is accepted. |
| `btn_swd_test_state` | `0` boot held, `1` idle, `2` virtual button down, `3` release/debounce in progress. |
| `btn_swd_test_completed` | Number of debounced releases completed since reset, including release of the boot hold. It resets on every device reset. |

The image starts every reset with its virtual button held. Wait until `SETUP` appears, then write `0` to release it and wait for state `1`. For each further press, write a positive duration **only while state is `1`**, wait for state `2` or `3`, then wait for state `1` and a completed-count increment before issuing the next command. A save/exit long press can reset the MCU before that press reaches state `1`; treat the new state `0` and renewed boot progress as its completion instead. The MCU supplies the actual hold duration and 50 ms release debounce. Do not write to `btn_swd_test_state` or `btn_swd_test_completed`.

Example GDB commands after the test ELF is loaded and the target is running (resume promptly if a GDB write halts it):

```gdb
p btn_swd_test_state
set var btn_swd_test_command_ms = 0
p btn_swd_test_state
set var btn_swd_test_command_ms = 200
p btn_swd_test_completed
set var btn_swd_test_command_ms = 1250
```

The example is a protocol sketch, not a command batch: wait for idle between writes. If the debugger disconnects or a command fails, stop the sequence and inspect state before retrying. A queued or repeated write could cause an extra menu action. Use brief SWD access; do not leave the target halted while measuring a press or the five-minute timeout. The harness ignores physical PA7/EXTI button events in this image, so pressing the real switch will not rescue a stalled test. A reset restores the automatic boot hold.

### Interface checkout before the full sequence

1. Confirm SWD connection, read the three symbols, and confirm state `0` after reset. Confirm the boot progress bar advances and reaches the first `SETUP` page. If the image does not reach Setup Menu, stop.
2. Write `0`, confirm the command returns to `UINT32_MAX`, state reaches `1`, and the completed counter increments. The first subject must remain `START/POWER/LEVEL`; boot-hold release must not count as a menu action.
3. Request one 200 ms press, then one 1250 ms press on a non-action page. Confirm the state transitions, completed-count increments, and OLED response. This checks the SWD transport and virtual event path before NVM-changing work. Reset and reenter to establish a known starting page.
4. Set up a machine-readable event log containing test ID, command value, host timestamp, target state/completed observations, expected OLED page/value, observed OLED page/value, and still path/capture timestamp. Use one host controller for all mailbox writes. Save each still under a unique test ID in a run-specific artifact directory, using `eyes_rx0.py --crop-to=800 --output <path>`. Inspect the saved image before the next menu command. If capture fails or text is unreadable, stop menu actions, record an evidence failure, and fix the camera setup before continuing.

## Timing and still-capture pace

The current configuration uses a 50 ms release debounce (`button.h`), 1000 ms long-press threshold (`conf.h`), 3000 ms boot setup hold (`conf.h`), and 300000 ms menu timeout (`conf.h`). Do not change these constants for the timeout cases. The menu emits one long event while held; a short event occurs after release. These tests exercise the button state machine's timing, but not physical EXTI latency or switch bounce.

| Action | SWD command / target behavior | OLED observation |
| --- | --- | --- |
| Short press | Write `200`; wait for idle/completed | Wait at least 250 ms for redraw, then capture and inspect a still. |
| Long press | Write `1250`; wait for idle/completed or reset | Wait at least 250 ms for redraw, then capture and inspect a still. |
| Between presses | Require state `1` before the next write | Finish image inspection before the next command. |
| Boot entry | Reset; virtual button is already held until the host writes `0` after `SETUP` | Capture `SETUP` after the progress bar completes; release the boot hold afterward. |
| Boundary probes | Write `850`, then `1150` on separate idle cycles | Capture and inspect after each completed action. |

Use an explicit output path for each still, for example `firmware/.pio/test-artifacts/setup-menu/<run-id>/<test-id>.jpg`; the utility prints its full path. The AI opens the saved JPEG to read the OLED and records that path and its capture time. Camera capture and transfer may take several seconds, so do not use them to time button holds, boot-release probes, or the near-timeout press. The host logs command acceptance and target state, not an electrical PA7 waveform. Still images establish settled screens; mailbox observations establish transitions during a hold. Avoid frequent debugger polling during the 5-minute cases; debugger pauses can disturb wall-clock comparisons or trigger watchdog recovery.

## Current menu oracle

Use **the compiled code** for subject order and options, as requested. Before execution, compare this table with the exact source/image under test. A long press advances one value and wraps at the end. Short presses advance one subject and wrap after the last action page. Initial values come from valid NVM or the defaults below.

| Index | Subject shown on OLED (line breaks shown with `/`) | Values in long-press order | Default |
| --- | --- | --- | --- |
| 0 | `START/POWER/LEVEL` | `SPORT`, `NORMAL`, `ECO` | `SPORT` |
| 1 | `FAN/MODE` | `OFF`; `ON/100%`, `ON/25%`, `ON/50%`, `ON/75%`; `AUTO/100/0/COOL`, `AUTO/100/0/QUIET`, `AUTO/50%/0/COOL`, `AUTO/50%/0/QUIET`, `AUTO/25%/0/COOL`, `AUTO/25%/0/QUIET`; `ADAPT/25%`, `ADAPT/50%`, `ADAPT/75%`, `ADAPT/100%`, `ADAPT/150%` | `AUTO/100/0/COOL` |
| 2 | `FAN/SIGNL/POLAR` | `DIRCT`, `INVRT` | `DIRCT` |
| 3 | `AUTO/SLEEP` | `OFF`, `5 MIN`, `15 MIN`, `30 MIN` | `OFF` |
| 4 | `AUTO/DIM` | `OFF`, `15 SEC`, `30 SEC`, `60 SEC` | `OFF` |
| 5 | `ACTIVE/MIN W` | `1 W`, `2 W`, `5 W`, `10 W`, `20 W`, `30 W`, `40 W` | `10 W` |
| 6 | `BATT/MODE` | `NONE`, `LIPO`, `LIPO/SAFER`, `LIHV`, `LIHV/SAFER`, `LIFE`, `LIFE/SAFER` | `NONE` |
| 7 | `INPUT/VOLT/CALIB` | `0`, `+1` through `+5`, `-1` through `-5` | `0` |
| 8 | `SHOW/SPLASH` | `NO`, `YES` | `YES` |
| 9 | `SAVE/AND/EXIT` | Action; no `=` or value | - |
| 10 | `EXIT/NO/SAVE` | Action; no `=` or value | - |

For the GPIO-only build, omit index 2 and use `OFF`, `ON/100%`, `AUTO/100/0/COOL`, `AUTO/100/0/QUIET` for fan mode. That variant is not part of this primary run and would need its own harness build. The voltage-calibration page also displays a live calibrated voltage near the bottom; require a measured reference before asserting a numeric voltage tolerance.

## Test sequence and pass criteria

Run phases in order. Preserve the baseline inventory, stills, and event log. On an unexpected page, value, reset, or mailbox state, stop issuing commands, retain the evidence, and inspect before deciding whether to reset. Do not silently retry a failed step. After a reset, wait for the automatic boot hold and release it only after `SETUP` appears.

### A. Entry and button classification

1. **A1 - Early boot release:** Reset and write `0` while the progress bar is still incomplete, about 1.5 s after the hold prompt appears. Time this from the host, without waiting for a camera capture. Capture the settled screen afterward. Expected: no Setup Menu; normal boot resumes. A later normal-mode fault on this unpopulated board is not a menu failure. Reset again; the harness restores the boot hold.
2. **A2 - Enter setup:** Reset and leave state `0` untouched through the full progress bar. Expected: `SETUP` and `START/POWER/LEVEL`. Write `0` and wait for idle. Entry hold and release must not advance the subject or value.
3. **A3 - Short navigation:** From index 0, request `200`. Expected: after the target's release/debounce, exactly one move to `FAN/MODE`; no advance while state is `2`. Check that interval with mailbox observations, then capture the settled page. Repeat once to confirm index 2. Values do not change.
4. **A4 - Long adjustment:** On a value page, request `1250`. Expected: exactly one next value, same subject, no extra subject change on release. Request `3000` on a later value page; expected exactly one value change, with no repeat while held.
5. **A5 - Timing margins:** On known value pages, request `850`, then `1150`, waiting for idle between them. Expected: 850 ms navigates once; 1150 ms changes one value without navigating. Do not use a boundary probe on an action page.

### B. Visual layout, subjects, and all option cycles

6. **B1 - Subject walk:** From a known page, request short presses through every oracle index, including the two action pages, then one more to wrap to index 0. Save and inspect a `--crop-to=800` still after each move. Verify order, no skips/double advances, `SETUP` on the top line, a blank second line, uppercase text, and readable titles. Value pages show `=` followed by the current value; action pages show titles without values.
7. **B2 - Full value sweep:** For each value page, request exactly its option count of long presses and capture/inspect every displayed value, including the wrap. This traverses all choices and returns to the starting value. Check line breaks, clipping, and one change per hold. Counts are 3, 16, 2, 4, 4, 7, 7, 11, and 2 for this image. Short-press to the next page after the final wrap.
8. **B3 - Unsaved cross-page memory:** Change two value pages to nonbaseline values, navigate away and back, and verify both remain edited. Select `EXIT/NO/SAVE` and long-press. Expected: reset to state `0`; after reentry and release, the original baseline values appear.
9. **B4 - Voltage preview:** On `INPUT/VOLT/CALIB`, capture at least two stills at least 2 s apart, then cycle from `0` through a positive and then a negative selection. Capture each selection and verify the displayed voltage and selection remain readable. Record numbers; assert direction or magnitude only with an external voltage reference. Stills do not establish continuous refresh between captures.

### C. NVM save, discard, reset, and restoration

10. **C1 - One-field save:** Choose a distinctive nonbaseline startup level, navigate to `SAVE/AND/EXIT`, and request `1250`. Expected: full reset to boot-held state `0`. Reenter and capture the saved startup value. On a write failure, expect `SAVE/FAULT`, stop, and avoid further flash writes. A transient fault screen may be missed by still capture; log the reset/mailbox behavior and any visible fault, and do not infer success from a missing fault image alone.
11. **C2 - Multi-field save:** Change at least startup level, one fan mode, auto sleep, battery mode, and voltage calibration. Save, reenter, and verify each exact value. A raw NVM read can support the required UI persistence check.
12. **C3 - Discard:** Change at least two saved values in RAM, choose `EXIT/NO/SAVE`, and long-press. Reenter; the last saved values must appear, not the discarded edits.
13. **C4 - Power-cycle persistence:** After a successful save, power-cycle by the established bench procedure, reenter, and verify the same saved values. The harness begins boot-held again after power restoration.
14. **C5 - Restore starting configuration:** Cycle each changed setting back to its recorded pretest value, save, reset, and reenter to verify all values. Record any value that cannot be restored and stop further writes.

### D. Inactivity and safe-state checks

15. **D1 - Five-minute inactivity:** Enter setup with a known unsaved edit, capture the last menu page, wait until the mailbox is idle, then issue no commands for at least 305 s. Capture the post-timeout `ZZZZZ` screen. Expected: `ZZZZZ` replaces the menu around 300 s after the last accepted button action, without saving. Reset/reenter and verify the edit was discarded. Record host timestamps and still capture times; the images bracket the transition rather than measure its exact instant.
16. **D2 - Activity restarts timeout:** Enter setup, wait about 270 s, request a short press, then capture a still after the action and another about 45 s later. Expected: still in menu. Continue untouched until about 300 s after that action, then capture the expected sleep screen. Use host timing for the wait; account for capture time when scheduling checks. A separate run may use a long press. Do not accelerate firmware time.
17. **D3 - Press near timeout:** On a value page, request `1250` shortly before the five-minute deadline (about 299.3 s after the last accepted action). Schedule the SWD write from the host clock; do not start a camera capture across the deadline. The design says any button press ends inactivity. Expected: no timeout during the hold and one value change. Capture the settled screen after the press. The current menu checks timeout before consuming events, so a mismatch is a possible firmware finding. Log command acceptance and timing; do not classify a late or stalled SWD write as a firmware failure.
18. **D4 - Menu output state:** During entry, value changes, and an idle interval, verify PB1 RF drive stays inactive and PA6 buck attenuation stays at its minimum-output command state. Use a high-impedance electrical probe if available; read-only SWD register inspection gives register-level evidence only. Any RF switching or lost minimum-output command is a stop condition. Do not infer buck voltage from the unloaded board without measuring it.

### E. Conditional and deferred cases

- **Physical button path:** Human-run OLED button bring-up verifies PA7, real short/long durations, switch bounce, and EXTI. Record its result separately; do not treat the SWD harness as a substitute.
- **GPIO-only menu:** If deployed, create a dedicated harness image with `FAN_PWM_ENABLED=0`, then repeat entry, page walk, four fan options, save/discard, and restoration. Do not switch images midway through this run.
- **Fresh/invalid NVM defaults:** Check only on a sacrificial or explicitly approved blank-NVM board. Do not erase current NVM to test defaults.
- **Flash write failure:** Controlled failure needs an agreed fault-injection method or separate test image. Do not corrupt flash or interrupt a save merely to force `SAVE/FAULT`.

## Reporting and completion

For every case record test ID, firmware image hash/flags, requested duration, mailbox transitions/completed count, OLED observation, still path and capture timestamp, expected result, pass/fail/blocked, and any electrical or SWD measurement. Include the pretest and post-restoration setting inventories. Retain the stills and event log. Distinguish a test-harness fault, camera/evidence failure, firmware discrepancy, and missing observation.

The AI-run menu test is complete when A-D pass (or each exception is recorded), the pretest settings are restored and verified, the mailbox is idle or the board is reset to its boot-held state, and the board is left in the agreed powered or unpowered state. This plan does not authorize code changes, NVM erasure, or RF/heating tests during execution.
