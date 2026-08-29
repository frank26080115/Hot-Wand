# Custom Inductors

In Radio Thermal's design, they used the Kool Mu 0077932A7 core, with Aₗ = 32 nH/turn², +/- 8%. The first core is actually two of these cores epoxied together in a stack, making it roughly Aₗ = 64 nH/turn².

The coated core dimensions are 27.69 mm maximum outside diameter, 14.10 mm minimum inside diameter, and 11.94 mm maximum height. For 22 AWG wire, using a diameter of 0.644 mm, two 10 mm leads, and 5% extra wire for winding tolerance, the approximate cut length in millimeters is:

Single core, where `T` is the number of turns:

`(2 * 10) + T * (2 * ((27.69 - 14.10) / 2 + 11.94) + pi * 0.644) * 1.05`

Two cores stacked together:

`(2 * 10) + T * (2 * ((27.69 - 14.10) / 2 + (2 * 11.94)) + pi * 0.644) * 1.05`

One turn means passing the wire through the center of the toroid once. These lengths are deliberately approximate; cut a little more if the two-core stack has a thick epoxy joint or if you want longer leads.

For 6.2 uH: 10 turns using 2 cores, requiring about 685 mm of wire; or 14 turns using 1 core, requiring about 601 mm

For 16.9 uH: 22 to 24 turns, requiring about 932 mm to 1015 mm of wire (choose 23 turns and about 974 mm if no measurement)

For 11.5 uH: 18 to 20 turns, requiring about 766 mm to 849 mm of wire (choose 19 turns and about 808 mm if no measurement)

Radio Thermal's developers ask that you use 22 AWG wire or thicker, and use a VNA or LCR meter to verify the results.

If you really do not have the measurement equipment, I have indicated the best choice.

#### Total Wire Lengths

The totals below assume 22 AWG wire and use the rounded cut lengths given above.

Using the recommended turn counts and two stacked cores for the 6.2 uH inductor:

`685 + 974 + 808 = 2467 mm = 2.467 m = 8.09 ft`

Depending on whether 22 to 24 turns and 18 to 20 turns are used for the other two inductors, the total ranges from approximately 2383 mm to 2549 mm, or 2.383 m to 2.549 m (7.82 ft to 8.36 ft).

If the 6.2 uH inductor is instead wound with 14 turns on one core, while the recommended turn counts are used for the others:

`601 + 974 + 808 = 2383 mm = 2.383 m = 7.82 ft`

For one complete unit, the practical minimum purchase is 3 m (10 ft) of 22 AWG magnet wire. To leave enough wire for trimming mistakes or rewinding an inductor, buy approximately 5 m (16.4 ft), or the next larger standard spool size.
