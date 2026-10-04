# Automated Setup Menu test (13.56 MHz Hot-Wand)

## Run it

Build PlatformIO environment **stm32f042_setup_swd** in firmware/platformio.ini first. The Python runner flashes that already built ELF; it does not build firmware. This image provides the SWD synthetic button, preserves SWD pins, and displays the vision fiducial as its first OLED frame. The ordinary stm32f042 and stm32f042_cubeide environments are not suitable.

Before launching, power the safe, checked board and connect the ST-Link V3MINIE through SWD, NRST, target voltage, and ground. Keep L3–L6 and the handpiece absent. Put the Sony RX0 II in PC Remote still-photo mode, with JPEG size L, PC+Camera storage, manual focus available, and an exposure that resolves individual OLED pixels. Close other debugger or camera applications. Check framing with:

    py firmware\tools\auto_testing\eyes_rx0.py --view

The whole OLED must be visible. From the repository root, start the unattended test with:

    py firmware\tools\auto_testing\setup_menu_test.py

Optional arguments:

    py firmware\tools\auto_testing\setup_menu_test.py --output-dir .\menu-run-01 --crop-to 800
    py firmware\tools\auto_testing\setup_menu_test.py --calibration .\previous-run\fiducial_calibration.json

The runner needs numpy, opencv-python, Pillow, gphoto2, PlatformIO ARM GDB, and PlatformIO OpenOCD. Full executable paths are near the top of tools/auto_testing/build_metadata.py; update them if installations move. The default output directory is date-and-time named under the current directory. An explicit --output-dir must name a new directory. The run includes the five-minute inactivity tests, so allow more than 15 minutes plus camera focus and transfer time.

## Modules and evidence

| Module | Responsibility |
| --- | --- |
| tools/auto_testing/eyes_rx0.py | Importable Rx0Camera handles still capture, automatic fiducial focus/exposure search, and corrected capture. |
| tools/auto_testing/fiducial.py | Finds the fiducial and applies its saved perspective transform. |
| tools/auto_testing/image_processing.py | Compares corrected photos with the framebuffer using only the **central 2×2 camera pixels** of each 8×8 OLED pixel region. |
| tools/auto_testing/build_metadata.py | Reads symbols and addresses from the ELF and rejects flash load ranges that reach NVM. Reads the menu table in compiled target memory. |
| tools/auto_testing/target_gdb.py | Starts OpenOCD, connects persistent GDB over local TCP, flashes, reads the sent-frame mirror and NVM, sends synthetic presses, and resets. |
| tools/auto_testing/frmbuf2png.py | Converts framebuffer blobs to upright black-and-white PNGs. |
| tools/auto_testing/setup_menu_test.py | Runs the phases below and writes the report and image artifacts. |

The runner saves the initial 1 KiB NVM page to nvm_before.bin, flashes only the ELF application load ranges, then checks that the reserved NVM page remained byte-for-byte unchanged. It does not directly write settings or erase NVM. Firmware Save actions may append journal records. At the end, the runner restores the initial *setting values* through the menu and verifies them; the final raw page may differ from the backup.

Calibration happens while the fiducial gate remains visible. Unless --calibration is supplied, focus and exposure search are automatic; the corrected fiducial and JSON recipe are saved. The runner dismisses the gate by writing vision_test_fiducial_press, leaving the synthetic boot hold intact. The fiducial reappears after every reset. An existing JSON positions the camera once; subsequent stills reuse focus, exposure, and perspective transform.
Every downloaded calibration JPEG is retained under calibration_stills in the run directory.

After each successful OLED I2C send, the vision-test firmware copies the 512-byte framebuffer into vision_test_oled_sent_framebuffer and finishes an even vision_test_oled_sent_generation. GDB reads this completed-frame mirror, briefly halting and resuming the MCU. An odd generation means the copy was interrupted and must be retried. GDB-MI values are parsed only from their command's result record, excluding asynchronous stop notifications. The MCU measures button durations. The mailbox symbols are btn_swd_test_command_ms, btn_swd_test_state, and btn_swd_test_completed. Commands are issued only from idle: 0 releases the boot hold, 200 ms is short, and 1250 ms is long. The runner also tests 850, 1150, and 3000 ms. Expected page order and option strings come from the setup_menu_items table in the *flashed ELF*.

The photo comparison proves that the lit screen agrees with the framebuffer sent by firmware. On the voltage page, the bottom 18 OLED rows contain a live measurement that may change between the SWD read and the PTP still. The report gives both the whole-frame mismatch and the checked upper-region mismatch; only the latter determines pass/fail there. It does **not** independently OCR displayed words. Navigation, distinct option images, option wrap, and NVM persistence are checked separately. Retained photos permit review of exact text, clipping, and layout. A camera failure is logged and the run continues with framebuffer evidence if possible. Missing SWD connection, persistently invalid framebuffer pointer, unsafe ELF load range, or a button transport that cannot advance is a hard stop.

## Automated cases

| Phase | Operations and expected evidence |
| --- | --- |
| A1 | Release the boot hold before the three-second entry threshold; capture the non-menu screen. |
| A2–A3 | Reset, enter Setup Menu, release the hold without a menu action, then short-press to fan and polarity pages. |
| A4–A5 | Check value changes from 1250 and 3000 ms holds; probe 850 ms as short and 1150 ms as long. Reset to a known state. |
| B1 | Short-press through all eleven compiled pages and wrap. Distinct page images and return to the initial frame are checked. |
| B2 | On each value page, long-press through its compiled option count. Option images must be distinct and wrap to the starting image. The live voltage footer is excluded from image signatures. |
| B3 | Edit two settings in RAM, navigate away and back, discard, reenter, and verify the saved baseline remains. |
| B4 | Capture the voltage page at two times, step through positive and negative calibration selections, and retain images. Voltage accuracy requires an external reference. |
| C1–C2 | Save one field, then several fields; verify NVM journal and reentry values. |
| C3 | Edit saved settings without saving, discard, and verify the previous NVM values. |
| C4 | Issue **GDB reset**, reenter, and verify persistence. This emulates a reboot; it does not remove board power or prove power-loss behavior. |
| C5 | Restore initial setting values through the menu, Save, reset, and verify. |
| D1 | Make an unsaved edit, wait at least 305 seconds without a button command, capture the post-timeout screen, and confirm no save. |
| D2 | Wait about 270 seconds, send activity, capture about 45 seconds later, then wait for the new timeout and capture again. |
| D3 | Start a long press near 299.3 seconds after menu entry and record whether it beats timeout. A firmware discrepancy is possible because timeout is checked before events. |
| D4 | RF and buck safe-state verification remains a separate electrical probe check; the script reports it as blocked. |

report.txt contains timestamps, mailbox observations, expected page/value, mismatch fractions, NVM checks, failures, and interruptions. Each observation has numbered full and corrected camera images, a raw framebuffer binary, a synthetic framebuffer PNG, and classified/difference images when comparison succeeds. nvm_at_stop.bin is attempted on an interrupted run. Individual discrepancies are logged and the sequence continues when possible; a hard stop retains available artifacts.

## Limits and separate checks

The physical PA7 switch, EXTI path, contact bounce, and RF/buck electrical behavior are outside this SWD script. The human button bring-up and high-impedance electrical checks remain separate. With L3–L6 and the handpiece absent, this run says nothing about loaded RF heating, fan electrical performance, or tip detection. Blank-NVM defaults and forced flash-write failures require separately prepared fixtures; this script does not erase or corrupt NVM to create those conditions.
