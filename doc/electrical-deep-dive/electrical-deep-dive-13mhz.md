# Main Power Buck Converter

This is the [switching DC buck converter](https://en.wikipedia.org/wiki/Buck_converter) that takes upstream voltage and converts it to the voltage that feeds the main RF amplifier (the amplifier that drives the RF signal that heats the RF soldering iron cartridge).

This particular implementation has two stages, 2 sets of inductor-capacitor pair structures. The feedback network is tapped off after the 1st stage. The second stage blocks any high frequency coming back from the main RF amplifier.

The output voltage is determined by the feedback network. This feedback network has three inputs: from the 1st stage of the buck converter, from the current transformer (the power factor meter), and from the microcontroller.

## U1

Value: TPS54560DDA

[LCSC: C31966](https://www.lcsc.com/product-detail/C31966.html)

This is the main IC that does the switching of this buck converter. It is capable of enough current for our needs. And it has internal overload protections, it will shutdown automatically if overheated.

The transistor that does the switching is internal. The switching frequency is adjustable.

## L1

Value: 33u

[Digi-Key: 744750460220 (22 uH fitted)](https://www.digikey.com/en/products/detail/w%C3%BCrth-elektronik/744750460220/6598151)

Buck converter first stage inductor, store energy as magnetic field when current flows through it, and that stored energy will cause more current flow as the field collapses.

## L2

Value: 4u7 IHLP2525CZ

[LCSC: C845027](https://www.lcsc.com/product-detail/C845027.html)

Buck converter second stage inductor, store energy as magnetic field when current flows through it, and that stored energy will cause more current flow as the field collapses.

## C2

Value: 100uF 50V

[Digi-Key: EEU-FR1H101B](https://www.digikey.com/en/products/detail/panasonic-industry/EEU-FR1H101B/2504106)

Buck converter first stage capacitor, it stores energy as electric charge, collected from the current coming from the inductors.

## C4

Value: 100uF 50V

[Digi-Key: EEU-FR1H101B](https://www.digikey.com/en/products/detail/panasonic-industry/EEU-FR1H101B/2504106)

Buck converter second stage capacitor, it stores energy as electric charge, collected from the current coming from the inductors.

## D1

Value: STPS3L60S

[LCSC: C2971007](https://www.lcsc.com/product-detail/C2971007.html)

This is the main catch diode for the buck converter. The catch diode provides a path for the inductor current when the buck converter's switching transistor turns off. Because the inductor resists sudden changes in current, the diode allows energy stored in its magnetic field to continue flowing into the load until the next switching cycle.

Section 8.2.2.5 in the TPS54560 datasheet talks more about this diode.

## C1

Value: 100nF

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

This is the capacitor for U1's bootstrap. It is needed to implement a charge pump. The charge pump is needed for U1's internal switching transistor.

The TPS54560 datasheet simply says this capacitor should be 0.1uF capacitance with a 10V (or greater) rating and X5R (or better) grade.

## VR1 and R4

VR1 Value: 10K

R4 Value: 22K

[LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

[Digi-Key: TC33X-2-103E](https://www.digikey.com/en/products/detail/bourns-inc/TC33X-2-103E/612858)

These two resistances are in series and forms the upper part of a voltage divider. The lower part of the voltage divider is R8.

This voltage divider divides the output voltage of the buck converter into a voltage seen by the feedback pin of U1, so it is the major part of the feedback network that determines the output voltage of the buck converter.

Adjusting VR1 is how to adjust the nominal output voltage of the buck converter. Normally we want the output to be about 18V to 20V depending on the soldering iron cartridge being used. Setting VR1 to the maximum 10 kiloohm will result in the maximum voltage being delivered.

## R37

Value: DNP

Placeholder for a resistor in case if VR1 is not being implemented. It is an option for a manufacturing process that does not need tuning.

## R8

Value: 1K

[LCSC: C21190](https://www.lcsc.com/product-detail/C21190.html)

The lower part of the feedback network voltage divider. See R4 and VR1 (these two form the upper part of the voltage divider) for details.

## C8

Value: 3n3

[LCSC: C1613](https://www.lcsc.com/product-detail/C1613.html)

Filtering capacitor in parallel with R8, which filters out high frequency noise for the feedback pin of U1.

## C3

Value: 15nF (DNP)

Not actually used. This is supposed to filter high frequency noise from the output of the buck converter's 1st stage, which would help the feedback network be less noisy.

## D2 and R5

D2 Value: BZT52C24

R5 Value: 1K

[D2 LCSC: C2109](https://www.lcsc.com/product-detail/C2109.html)

[R5 LCSC: C21190](https://www.lcsc.com/product-detail/C21190.html)

These two will leak current away if the output of the buck converter's first stage exceeds 24V. D2 is a 24V zener diode.

It's a leak, not a clamp. It's leaking into the feedback pin of U1 (not leaking into ground). It's trying to limit the output to 24V.

## D3

Value: SD103A

[LCSC: C7420360](https://www.lcsc.com/product-detail/C7420360.html)

The microcontroller is connected to the feedback network through R9 and with D3 in between. The job of D3 is to make sure the microcontroller cannot ever raise the buck converter voltage, it is only allowed to lower it. This is a just-in-case preventing firmware bugs from causing dangerous situations.

## R9

Value: 2K2

[LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

The microcontroller is connected to the feedback network through R9, using a high frequency PWM signal to affect the output voltage of the buck converter.

The square PWM signal is filtered into a smooth signal with the help of C58, R9 and C58 forms a low pass filter.

The resistance of R9 works together with the resistance of R8 to divide the voltage of the smoothed PWM signal. This basically sets the gain for this smoothed signal.

## C58

Value: 3n3

[LCSC: C1613](https://www.lcsc.com/product-detail/C1613.html)

C58 and R9 forms a low pass filter that smooths out the PWM signal coming from the microcontroller.

## TP2

Used to verify the PWM signal shape/timing when testing microcontroller firmware.

## R6

Value: 270K

[LCSC: C137755](https://www.lcsc.com/product-detail/C137755.html)

R6 sets the switching frequency of U1 to 450 kHz. 450 kHz was choosen simply because it won't interact poorly with other frequencies in our circuit. (you don't want some unintended resonance that blows something up)

## R7 and C7

R7 Value: 22K

[R7 LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

C7 Value: 100n

[C7 LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

U1's internal slope compensation is implemented by these. See TPS54560 datasheet section 8.2.2.11 on how to calculate these values.

## F1

Value: 7A 20x5mm

[Digi-Key: 3518P fuse clip](https://www.digikey.com/en/products/detail/keystone-electronics/3518P/316011)

This is the main input fuse. It will blow when something goes wrong (tm). "Something" is a really large problem space here.

## C5

Value: 10u

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

Input voltage decoupling

## C6

Value: 470uF

[Digi-Key: UHW1H471MPD](https://www.digikey.com/en/products/detail/nichicon/UHW1H471MPD/3664388)

The bulk input capacitor. Handles (smooths out) short power glitches from upstream power. If you overload a USB-C host briefly, this big capacitor is why the microcontroller can still stay alive and recover quickly when the host recovers.

## D13

Value: 2A 60V

[LCSC: C53340922](https://www.lcsc.com/product-detail/C53340922.html)

Reverse-current bypass diode for the buck converter. It provides an alternate path for current when the regulator's input power is suddenly disconnected while energy remains stored in the output inductor and capacitors. This prevents the switching node from rising excessively above the input supply, protecting the regulator's internal switching transistor from potentially damaging reverse-current conditions.

## TVS6

Value: SMAJ24CA

[LCSC: C148223](https://www.lcsc.com/product-detail/C148223.html)

This is supposed to clamp the final output of the buck converter to 24V.

Realistically it is useless. The only component that might even see a voltage spike is the input to L3, which is just a dumb (as in, it's literally just metal) inductor. The output of L3 is the drain of Q1, which can handle something like 200V.

Also, the design is supposed to operate at 18V to 20V as adjusted by VR1, at 20V, some of the iron cartridges are heating beyond specifications anyways.

This component is actually here because... if it explodes, it tells me I've done something wrong in the design, or the circuit is damaged. There's a good chance it fails-short, which would cause the main fuse to blow. (this isn't a guess...)

## R1, R2, R3

Value: 0R1

[LCSC: C188071](https://www.lcsc.com/product-detail/C188071.html)

These are current measurement resistors, U5 will read the voltage across these resistors to determine the current consumption.

# Main RF Power Amplifier Class E

## Q1

Value: STP19NF20

[Digi-Key: STP19NF20](https://www.digikey.com/en/products/detail/stmicroelectronics/STP19NF20/1852512)

## L3

Value: 9uH

## C9

Value: 100nF

[LCSC: C5182461](https://www.lcsc.com/product-detail/C5182461.html)

## C10

Value: 100nF

[LCSC: C5182461](https://www.lcsc.com/product-detail/C5182461.html)

## C11

Value: 100nF

[LCSC: C5182461](https://www.lcsc.com/product-detail/C5182461.html)

## C12

Value: 100nF

[LCSC: C5182461](https://www.lcsc.com/product-detail/C5182461.html)

## D4

Value: ES1J

[LCSC: C64879](https://www.lcsc.com/product-detail/C64879.html)

## TVS1

Value: SM15T150CA

[LCSC: C133715](https://www.lcsc.com/product-detail/C133715.html)

## R12

Value: 150Ω 2W

[LCSC: C175287](https://www.lcsc.com/product-detail/C175287.html)

## R13

Value: DNP

## D9

Value: STPS0540Z

[LCSC: C181835](https://www.lcsc.com/product-detail/C181835.html)

## TVS2

Value: SMBJ15CA

[LCSC: C78809](https://www.lcsc.com/product-detail/C78809.html)

## SJ6

Value: SJ-SMALL-1206-SJ

## SJ7

Value: SJ-SMALL-1206-SJ

# Matching Network

## L4

Value: 180nH

## C13

Value: 100pF 1000V

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

## C14

Value: 100pF 1000V

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

## C15

Value: 100pF 1000V

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

## C16

Value: 100pF 1000V

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

## C17

Value: 100pF 1000V

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

## C18

Value: 100pF 1000V

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

## C19

Value: DNP

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

## L5

Value: 400nH

## C20

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

## C21

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

## C22

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

## C23

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

## C24

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

## C25

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

## C26

Value: 100pF 1000V

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

## L6

Value: 540nH

## C27

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

## C28

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

## C29

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

## C30

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

## C31

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

## C32

Value: DNP

## JP1

Value: COAX-FTYPE-PCB-MNT

[Digi-Key: 0733300030](https://www.digikey.com/en/products/detail/molex/0733300030/2755903)

## T1

## C33

Value: 10pF 500V

[LCSC: C6629873](https://www.lcsc.com/product-detail/C6629873.html)

## D5

Value: 4148

[LCSC: C84367](https://www.lcsc.com/product-detail/C84367.html)

## D6

Value: 4148

[LCSC: C84367](https://www.lcsc.com/product-detail/C84367.html)

## D7

Value: 1N4148W

[LCSC: C84367](https://www.lcsc.com/product-detail/C84367.html)

## D8

Value: 4148

[LCSC: C84367](https://www.lcsc.com/product-detail/C84367.html)

## R10

Value: 100Ω

[LCSC: C22775](https://www.lcsc.com/product-detail/C22775.html)

## C34

Value: 1uF

[LCSC: C15849](https://www.lcsc.com/product-detail/C15849.html)

## R11

Value: 2K2

[LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

## VR4

Value: 5K

[Digi-Key: TC33X-2-502E](https://www.digikey.com/en/products/detail/bourns-inc/TC33X-2-502E/612861)

## SJ2

## STANDOFF2

Value: STANDOFF-M2.5-4.5MMHEX

# RF Amp Gate Driver

## U2

Value: 1EDN8511BXUSA1

[LCSC: C3655509](https://www.lcsc.com/product-detail/C3655509.html)

## Q2

Value: IRF510

[Digi-Key: IRF510PBF](https://www.digikey.com/en/products/detail/vishay-siliconix/IRF510PBF/811710)

## R16

Value: 3R6

[LCSC: C6301221](https://www.lcsc.com/product-detail/C6301221.html)

## R17

Value: 3R6

[LCSC: C6301221](https://www.lcsc.com/product-detail/C6301221.html)

## L7

Value: 9uH

## L8

Value: 180nH

## C37

Value: 100nF

[LCSC: C5182461](https://www.lcsc.com/product-detail/C5182461.html)

## C38

Value: 100nF

[LCSC: C5182461](https://www.lcsc.com/product-detail/C5182461.html)

## C39

Value: 100pF

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

## C40

Value: 100pF

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

## R14

Value: 100Ω

[LCSC: C22775](https://www.lcsc.com/product-detail/C22775.html)

## R15

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

## C35

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

## C36

Value: 1uF

[LCSC: C15849](https://www.lcsc.com/product-detail/C15849.html)

## SJ8

Value: SJ-SMALL-1206-SJ

# Tip Detection

## Q3

Value: BC846B

[LCSC: C181138](https://www.lcsc.com/product-detail/C181138.html)

## R21

Value: 22Ω

[LCSC: C5204054](https://www.lcsc.com/product-detail/C5204054.html)

## L9

Value: 22uH

[Digi-Key: AIAP-01-220K-T](https://www.digikey.com/en/products/detail/abracon-llc/AIAP-01-220K-T/3059827)

## C42

Value: 33nF

[LCSC: C21117](https://www.lcsc.com/product-detail/C21117.html)

## VR2

Value: 1K

[Digi-Key: TC33X-2-102E](https://www.digikey.com/en/products/detail/bourns-inc/TC33X-2-102E/612857)

## R19

Value: 470Ω

[LCSC: C23179](https://www.lcsc.com/product-detail/C23179.html)

## R20

Value: DNP

## VR3

Value: 1K

[Digi-Key: TC33X-2-102E](https://www.digikey.com/en/products/detail/bourns-inc/TC33X-2-102E/612857)

## R22

Value: 2K2

[LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

## R18

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

## C41

Value: 3n3

[LCSC: C1613](https://www.lcsc.com/product-detail/C1613.html)

# Microcontroller

## U3

Value: STM32F042F6P6

[LCSC: C2969989](https://www.lcsc.com/product-detail/C2969989.html)

## Y1

Value: 27.12 MHz 10pF

[LCSC: C1986817](https://www.lcsc.com/product-detail/C1986817.html)

## C55

Value: 10p

[LCSC: C1634](https://www.lcsc.com/product-detail/C1634.html)

## C56

Value: 10p

[LCSC: C1634](https://www.lcsc.com/product-detail/C1634.html)

## C53

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

## C54

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

## R33

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

## SJ1

Value: SOLDERJUMPERWS

## JP4

## R52

Value: 100Ω

[LCSC: C22775](https://www.lcsc.com/product-detail/C22775.html)

## TP1

Value: TEST-POINT-SQ-2MM

## U4

Value: SSD1306-4PIN-0.91N-VERT

## R35

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

## R36

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

## C57

Value: 10u

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

## SW1

[Digi-Key: MJTP1236D](https://www.digikey.com/en/products/detail/apem-inc/MJTP1236D/1798058)

## D12

Value: 3V6

[LCSC: C840638](https://www.lcsc.com/product-detail/C840638.html)

# Secondary Power Buck Converters

## IC1

Value: 12V R-78K12-2.0

[Digi-Key: R-78K12-2.0](https://www.digikey.com/en/products/detail/recom-power/R-78K12-2-0/18093036)

## IC2

Value: 3V3 R-78K3.3

[Digi-Key: R-78K3.3-1.0](https://www.digikey.com/en/products/detail/recom-power/R-78K3-3-1-0/18093033)

## C43

Value: 10u

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

## D10

Value: 2A

[LCSC: C698873](https://www.lcsc.com/product-detail/C698873.html)

## C44

Value: 10u

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

## D11

[LCSC: C698873](https://www.lcsc.com/product-detail/C698873.html)

## C45

Value: 10u

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

## D14

Value: 3V6

[LCSC: C840638](https://www.lcsc.com/product-detail/C840638.html)

## LED1

Value: LED

## R38

Value: 22K

[LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

## R39

Value: DNP

## LED2

Value: LED

## R40

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

## LED3

Value: LED

## R41

Value: 1K

[LCSC: C21190](https://www.lcsc.com/product-detail/C21190.html)

## LED4

Value: LED

## R42

Value: 22K

[LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

## R43

Value: DNP

# Thermistors

## THERM1

Value: 10K/~3950

[Digi-Key: B57891M0103K000](https://www.digikey.com/en/products/detail/tdk/B57891M0103K000/3500546)

## R50

Value: 2K2

[LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

## C59

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

## THERM2

Value: 10K/~3950

[Digi-Key: B57891M0103K000](https://www.digikey.com/en/products/detail/tdk/B57891M0103K000/3500546)

## R51

Value: 2K2

[LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

## C60

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

# Fan CTRL

## Q9

Value: Si2318

[LCSC: C14498](https://www.lcsc.com/product-detail/C14498.html)

## FAN1

Value: FAN-CONN-X4-CONN-X4

[Digi-Key: 0470533000](https://www.digikey.com/en/products/detail/molex/0470533000/3262217)

## R45

Value: 470Ω

[LCSC: C23179](https://www.lcsc.com/product-detail/C23179.html)

## R44

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

## SJ3

Value: SHORTED

## SJ4

## SJ5

## C61

Value: 10u

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

# Power Sensing

## U5

Value: INA138

[LCSC: C47914](https://www.lcsc.com/product-detail/C47914.html)

## R25

Value: 82K

[LCSC: C23254](https://www.lcsc.com/product-detail/C23254.html)

## C49

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

## C46

Value: 1uF

[LCSC: C15849](https://www.lcsc.com/product-detail/C15849.html)

## R23

Value: 22K

[LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

## R24

Value: 2K2

[LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

## C47

Value: 100n

## R26

Value: 22K

[LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

## R27

Value: 2K2

[LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

## C48

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

# XT30 DC Input

## U6

Value: LM74810QDRRRQ1

[LCSC: C3215601](https://www.lcsc.com/product-detail/C3215601.html)

## Q4

Value: SIS862ADN-T1-GE3

[Digi-Key: SIS862ADN-T1-GE3](https://www.digikey.com/en/products/detail/vishay-siliconix/SIS862ADN-T1-GE3/10273791)

## Q5

Value: SIS862ADN-T1-GE3

[Digi-Key: SIS862ADN-T1-GE3](https://www.digikey.com/en/products/detail/vishay-siliconix/SIS862ADN-T1-GE3/10273791)

## JP3

Value: XT-CONNECTOR-XT30PW

## TVS3

Value: 36V

[LCSC: C134984](https://www.lcsc.com/product-detail/C134984.html)

## C50

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

## C51

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

## C52

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

## R56

Value: 100Ω

[LCSC: C441975](https://www.lcsc.com/product-detail/C441975.html)

## C62

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

## C63

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

## R53

Value: 82K

[LCSC: C23254](https://www.lcsc.com/product-detail/C23254.html)

## R54

Value: 3K

[LCSC: C4211](https://www.lcsc.com/product-detail/C4211.html)

## R55

Value: 22K

[LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

## R34

Value: 22K

[LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

# USB-PD Input

## U7

Value: AP53781

[LCSC: C52730979](https://www.lcsc.com/product-detail/C52730979.html)

## Q6

Value: DMTH64M2LPDWQ-13

[Digi-Key: DMTH64M2LPDWQ-13](https://www.digikey.com/en/products/detail/diodes-incorporated/DMTH64M2LPDWQ-13/28171648)

## Q7

Value: SIS862ADN-T1-GE3

[Digi-Key: SIS862ADN-T1-GE3](https://www.digikey.com/en/products/detail/vishay-siliconix/SIS862ADN-T1-GE3/10273791)

## JP2

Value: USBC-CHGONLY-4M-DRILL

[Digi-Key: UJC-H-G-SMT-2-P6-TR](https://www.digikey.com/en/products/detail/same-sky-formerly-cui-devices/UJC-H-G-SMT-2-P6-TR/24818567)

## R31

Value: 100Ω

[LCSC: C441975](https://www.lcsc.com/product-detail/C441975.html)

## R32

Value: 100Ω

[LCSC: C441975](https://www.lcsc.com/product-detail/C441975.html)

## R30

Value: 4K3

[LCSC: C23159](https://www.lcsc.com/product-detail/C23159.html)

## R47

Value: 39K

[LCSC: C23153](https://www.lcsc.com/product-detail/C23153.html)

## R48

Value: 3K

[LCSC: C4211](https://www.lcsc.com/product-detail/C4211.html)

## R49

Value: 75K

[LCSC: C23242](https://www.lcsc.com/product-detail/C23242.html)

## JP5

## R46

Value: DNP

## R46B

Value: DNP

## Q8

Value: Si2318

[LCSC: C14498](https://www.lcsc.com/product-detail/C14498.html)

## R28

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

## R29

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

## TVS4

Value: 15V

[LCSC: C2987151](https://www.lcsc.com/product-detail/C2987151.html)

## TVS5

Value: 36V

[LCSC: C134984](https://www.lcsc.com/product-detail/C134984.html)

## C64

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)
