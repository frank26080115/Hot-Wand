# Fan Modes

| Code | Desc | Ramp? | Display  |
|------|------|-------|----------|
| 0    | OFF  | No    | `OFF`    |
| 1    | 100% Always | Yes | `ON\n100%` |
| 2    | 25% Always  | No  | `ON\n25%`  |
| 3    | 50% Always  | No  | `ON\n50%`  |
| 4    | 75% Always  | No  | `ON\n75%`  |
| 5    | Auto, Binary, Cool        | Yes | `AUTO\n100/0\nCOOL`  |
| 6    | Auto, Binary, Quiet       | Yes | `AUTO\n100/0\nQUIET` |
| 7    | Auto, Binary (50%), Cool  | No  | `AUTO\n50%/0\nCOOL`  |
| 8    | Auto, Binary (50%), Quiet | No  | `AUTO\n50%/0\nQUIET` |
| 9    | Auto, Binary (25%), Cool  | No  | `AUTO\n25%/0\nCOOL`  |
| 10   | Auto, Binary (25%), Quiet | No  | `AUTO\n25%/0\nQUIET` |
| 11   | Auto, Adaptive, 25%       | No  | `ADAPT\n25%`  |
| 12   | Auto, Adaptive, 50%       | No  | `ADAPT\n50%`  |
| 13   | Auto, Adaptive, 75%       | No  | `ADAPT\n75%`  |
| 14   | Auto, Adaptive, 100%      | No  | `ADAPT\n100%` |
| 15   | Auto, Adaptive, 150%      | No  | `ADAPT\n150%` |

The minimum steady fan PWM duty cycle is 25%.

For all adaptive modes, 40C maps to 25% duty. The gain scales the slope between temperature and duty. At 80C, gains of 25%, 50%, 75%, 100%, and 150% produce approximately 44%, 63%, 81%, 100%, and 100% duty. The result is clamped to 25-100%. Adaptive control turns on at 40C and turns off below 38C. If the fan is already active, it stays at 25% between 38C and 40C. Its target is updated once per second.

COOL and QUIET select activation thresholds: COOL turns on at 50C and off below 48C; QUIET turns on at 70C and off below 68C. All automatic profiles use the maximum filtered temperature from THERM1, THERM2, and the MCU sensor. They retain the 10-second minimum-on and 5-second minimum-off dwell. The minimum-off timer starts when commanded duty actually reaches zero.

For every mode whose "Ramp?" column says "No", a zero-to-nonzero transition starts with 100% duty for 0.5 seconds before applying the selected duty. Later adaptive target changes do not retrigger the boost.

For every mode whose "Ramp?" column says "Yes", actual fan duty can change only at 25 percentage points per second in either direction. There is no minimum duty floor during a ramp.

To account for direct push-pull and external-MOSFET wiring, the `FAN SIGNL POLAR` option offers `DIRCT` and `INVRT`. The regular PWM build presents all sixteen profiles and this polarity option.

The `stm32f042_no_fan_pwm` build compiles out the TIM16/TIM17 backend. Its menu presents only codes 0, 1, 5, and 6, hides polarity, and drives PA13 as an active-high GPIO. Modes 5 and 6 switch immediately in this build because ramping is unavailable. If this build reads any other valid saved fan code, it uses mode 5 in RAM.

At boot, every mode defers fan control and leaves PA13 available to SWD for `FAN_STARTUP_OFF_TIME_MS`, which defaults to five seconds. The fan input's passive behavior during that interval depends on the hardware. When the interval ends, the firmware claims PA13 at 0% duty before applying the selected profile. Mode 0 therefore actively drives the fan off after the debugger window. Defining `FAN_STARTUP_OFF_TIME_MS=0` removes the window and claims PA13 during fan initialization. This startup setting does not change the five-second minimum-off dwell used after runtime transitions.

A fault or sleep stop bypasses ramping and dwell and commands zero immediately; a recoverable resume reapplies the profile's normal startup behavior without repeating the boot-only debugger window.
