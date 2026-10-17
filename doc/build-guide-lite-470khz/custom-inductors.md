# Custom Inductors

Hot Wand Lite uses four Magnetics Kool Mu 0077932A7 cores, one each for L2, L3, L4, and L5. All built Lite units use one core per inductor. The nominal Aₗ is 32 nH/turn², +/- 8%.

The coated core dimensions are 27.69 mm maximum outside diameter, 14.10 mm minimum inside diameter, and 11.94 mm maximum height. For 22 AWG wire, using a diameter of 0.644 mm, two 10 mm leads, and 5% extra wire for winding tolerance, the approximate cut length in millimeters is:

`(2 * 10) + T * (2 * ((27.69 - 14.10) / 2 + 11.94) + pi * 0.644) * 1.05`

Here, `T` is the number of turns. One turn means passing the wire through the center of the toroid once. These lengths are deliberately approximate; cut a little more if you want longer leads.

| Inductor | Target inductance | Turns on one core | Approximate wire cut length |
| --- | --- | --- | --- |
| L2 | 6.2 uH | 14 | 601 mm |
| L3 | 16.9 uH | 22 to 24; start with 23 | 932 to 1015 mm; 974 mm for 23 turns |
| L4 | 16.9 uH | 22 to 24; start with 23 | 932 to 1015 mm; 974 mm for 23 turns |
| L5 | 11.5 uH | 18 to 20; start with 19 | 766 to 849 mm; 808 mm for 19 turns |

Radio Thermal's developers ask that you use 22 AWG wire or thicker, and use a VNA or LCR meter to verify the results. The suggested turn counts are starting points; measure and adjust each inductor before installation. It is nearly impossible to hit the target inductances reliably from turn counts alone.

#### Total Wire Lengths

The totals below assume 22 AWG wire and use the rounded cut lengths given above. Include both 16.9 uH inductors, L3 and L4.

Using the suggested turn counts:

`601 + 974 + 974 + 808 = 3357 mm = 3.357 m = 11.01 ft`

Depending on whether 22 to 24 turns are used for each of L3 and L4, and 18 to 20 turns for L5, the total ranges from approximately 3231 mm to 3480 mm, or 3.231 m to 3.480 m (10.60 ft to 11.42 ft).

For one complete unit, buy at least 5 m (16.4 ft) of 22 AWG magnet wire to leave margin for longer leads, trimming mistakes, or rewinding an inductor.

## Measuring the Inductors

Use an LCR meter with **at least a 100 kHz test mode**, such as the [FNIRSI LC1020E listed in the shopping guide](shopping.md#lcr-meter).

1. Select inductance (L), series mode (Ls), and **100 kHz**. Perform open and short compensation with your test leads according to the meter's instructions.
2. Measure each inductor before installing it, with short connections to the stripped wire ends. If already installed, disconnect power, discharge the board, and lift one lead so the surrounding circuit does not affect the reading. Never connect the meter to a powered circuit.
3. Adjust turns and winding spacing toward the targets above, then remeasure. Record the final turn count and inductance for L2 through L5.

**Do not skip both inductance measurements and the tip temperature check. Building this without either check is asking for an overheated tip, even if it appears to solder normally.**
