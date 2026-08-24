| Code | Desc | Ramp? | Chase? | Display  |
|------|------|-------|--------|----------|
| 0    | OFF  | No    | No     | OFF      |
| 1    | 100% Always | No | No | ON\n100% |
| 2    | 25% Always  | No | No | ON\n25%  |
| 3    | 50% Always  | No | No | ON\n50%  |
| 4    | 75% Always  | No | No | ON\n75%  |
| 5    | Auto, Binary, Cool        | Yes | No | AUTO\n100/0\nCOOL  |
| 6    | Auto, Binary, Quiet       | Yes | No | AUTO\n100/0\nQUIET |
| 7    | Auto, Binary (50%), Cool  | Yes | No | AUTO\n50%/0\nCOOL  |
| 8    | Auto, Binary (50%), Quiet | Yes | No | AUTO\n50%/0\nQUIET |
| 9    | Auto, Binary (25%), Cool  | Yes | No | AUTO\n25%/0\nCOOL  |
| 10   | Auto, Binary (25%), Quiet | Yes | No | AUTO\n25%/0\nQUIET |
| 11   | Auto, Adaptive, 25%  | Yes | Yes | ADAPT\n25%  |
| 12   | Auto, Adaptive, 50%  | Yes | Yes | ADAPT\n50%  |
| 13   | Auto, Adaptive, 75%  | Yes | Yes | ADAPT\n75%  |
| 14   | Auto, Adaptive, 100% | Yes | Yes | ADAPT\n100% |
| 15   | Auto, Adaptive, 150% | Yes | Yes | ADAPT\n150% |

For all adaptive modes, the minimum temperature is 40C, mapping to fan PWM duty of 0%, 100% means at 80C, fan is at 100% PWM.

The difference between COOL and QUIET are the activation thresholds, COOL means 50C, and QUIET means 70C. There shall be a 5C hysteresis for the fan to turn off.

For all of the binary modes, the startup of the fan must happen with at least 25% PWM for 2 seconds. Ramping down while turning off will ignore the 25% floor.

To account for open-drain mode and push-pull mode, the option "FAN SIGNL POLAR" (signal polarity) can have the option of "DIRCT" or "INVRT" (direct or inverted).
