This is the firmware for the Hot Wand, running on a STM32F042 microcontroller.

It is meant to be built under PlatformIO and flashed via the SWD pins using a ST-Link V2.

# Firmware Architecture

The overall firmware is split into the main application that operates the soldering iron, and a setup menu (which keeps the soldering iron unpowered while within the menu). The main application owns initialization and the continuously running control loop. The setup menu is a separate modal loop entered during boot; it stops RF generation and forces the buck-converter control to minimum output before accepting input or drawing menu pages. It uses only the button, display, timeout, and nonvolatile-settings paths needed to edit configuration, then resets the controller when exiting. This keeps setup logic out of the active power-control path and makes the safe output state explicit at the boundary between the two modes.

## Platform and hardware layer

The project uses PlatformIO's STM32Cube framework and the STM32F0 hardware abstraction layer supplied by ST. The vendor HAL and CMSIS definitions provide the supported interface to clock control, GPIO, interrupt configuration, ADC setup and calibration, flash programming, and the other MCU peripherals. This keeps device-specific register names, bit definitions, and startup behavior tied to the selected STM32 target rather than duplicating them inside the application.

Some paths use the STM32 peripheral registers directly after the HAL has established the surrounding clocks and pin configuration. This is intentional where the firmware needs an exact timer waveform, a carefully ordered GPIO/peripheral handoff, or a small deterministic interrupt path. The RF carrier, power-attenuation PWM, tip-detect timer, ADC channel rotation, and OLED I2C transport are examples. These accesses still use ST's CMSIS device definitions; they are not a separate home-grown hardware abstraction layer.

## Imported U8g2 display library

A copy of U8g2, including its U8x8 support layer, is imported into `lib/u8g2` and compiled as part of the firmware instead of being fetched as a build-time dependency. The imported library provides the SSD1306 display driver, framebuffer, fonts, and drawing primitives. Its upstream license is retained alongside the source.

Application code reaches U8g2 through `oled.c`. That module supplies the display setup and the U8x8 byte, GPIO, and delay callbacks that connect the library to this board's I2C transport and STM32 timing functions. Keeping the adapter at this boundary avoids spreading display-controller and library-callback details through the rest of the firmware.

The STM32F042 is the primary build target. Its custom PlatformIO board definition and linker script describe the actual device and reserve the required flash area for nonvolatile settings. Link-time optimization is enabled, and the configured maximum image size ensures the application fits in the STM32F042 flash allocation.

## Execution model

There is no RTOS, scheduler, thread, or dynamically registered task list. After a fixed initialization sequence, `main()` enters one cooperative `for (;;)` superloop. Each subsystem exposes a small task function that is called in a deliberate order: inputs and tip state are serviced first, safety faults are checked, power is supervised, RF restart eligibility is evaluated, and then the lower-priority temperature, fan, debug, button-action, and display work runs.

Most tasks are non-blocking state machines. They remember their own state and compare timestamps against the shared millisecond tick, then return quickly when no work is due. This makes execution easy to inspect: there is one foreground control flow, and timing behavior does not depend on scheduler priorities or task synchronization primitives.

The boot/setup screens and terminal fault screen use their own explicit loops. Those are intentionally modal states, not hidden schedulers. They use short delays while continuing the small amount of input and display work appropriate to that state. The setup menu and all terminal fault paths first disable power-producing outputs.

## Interrupts and timing

Interrupts are reserved for events that cannot depend on foreground loop latency:

- SysTick maintains the HAL millisecond counter used by the cooperative state machines.
- The ADC completion interrupt advances continuous round-robin sampling and updates the filtered readings.
- EXTI and TIM17 qualify tip-presence edges with a hardware-timed debounce interval.
- The clock security system uses the NMI path to shut down RF if the external crystal fails.

The startup vector table supplied by STM32Cube uses weak handlers. `interrupt_vectors.S` provides strong, non-LTO shims that retain and dispatch to the firmware's C implementations even with the GCC 7 link-time optimizer enabled.

## Tip-detection safety

Tip detection starts fail-closed: before the detector has initialized and confirmed the input, its fault latch is set and RF is not allowed to start. The electrical input has an external pull-up, so a high level represents a connected tip and a low level represents a missing tip.

Both input edges trigger EXTI. The handler masks the tip interrupt and starts TIM17 as a one-shot timer for the 300 microsecond debounce interval. When that interval expires, the timer interrupt samples the settled pin. A confirmed missing tip latches the fault and calls `rfgen_stop()` directly from the interrupt path, without waiting for another pass through the main loop. Edges arriving during qualification remain pending and receive another complete qualification interval after EXTI is unmasked.

The latch does not clear merely because the signal returns high. Reset is allowed only when the last debounced state says the tip is present, the physical pin is currently high, and no debounce interval is in progress. Normal operation treats the resulting tip fault as terminal until the user resets the controller.

RF startup independently repeats these checks. It refuses to start while the tip latch is set, while TIM17 is qualifying an edge, or while the physical tip input is low. The final check and timer enable are performed with maskable interrupts disabled, closing the race in which a disconnect could otherwise occur partway through startup.

## RF generation and shutdown safety

The RF carrier depends on the 27.12 MHz external crystal. TIM1 divides that clock by two to produce the 13.56 MHz, 50 percent duty-cycle waveform. Startup verifies that HSE is ready and is actually the system clock before enabling the RF output. The clock security system remains enabled afterward; an HSE failure enters the NMI handler, latches a clock fault, and stops the generator. Internal lockup and SRAM-parity fault signals are also routed to TIM1's hardware break input, whose configured run and idle off-states hold the RF output inactive.

RF shutdown does not rely on simply stopping a timer and hoping the pin assumes a safe state. `rfgen_stop()` first marks the generator inactive, preloads the GPIO output latch low, and changes the RF pin from TIM1 alternate-function control to a push-pull GPIO output. The pin is therefore actively held low rather than left floating or dependent on a disabled timer. TIM1's main-output enable, complementary channel, and counter are then disabled.

Startup uses the reverse discipline. The timer is completely configured while its main output remains disabled, the inactive timer state owns the pin first, and only the final guarded sequence enables the timer and RF output. If a tip or clock fault is observed during that sequence, shutdown runs again immediately.

## Fault containment

Fault handling is deliberately terminal. Entering `show_fault()` immediately stops RF generation, forces the buck-converter attenuation output to its minimum-power state, and stops the fan before attempting to draw or animate an error message. If the display is unavailable, the firmware remains in the safe terminal loop anyway. Faults are never cleared automatically; recovery requires the explicit reset path allowed for that fault.

This gives the firmware layered protection. Foreground supervision handles normal voltage, temperature, current, and power decisions; time-critical tip and clock failures have interrupt-level shutdown paths; TIM1 has hardware break inputs for internal MCU faults; and terminal fault presentation independently reapplies safe output states before doing any UI work.
