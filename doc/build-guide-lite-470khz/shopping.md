# Shopping List for Hot Wand Lite (470 kHz version)

This is a practical shopping list for building **one** Hot Wand Lite. It supplements the fabrication files and the JLCPCB-assembled board; it is deliberately not a second BOM. Parts marked `JLC-DNP=2` in the schematic are not listed here because that value only records a temporary JLCPCB stock problem. Unvalued footprints marked `DNP`, solder jumpers, and PCB artwork are not purchases either.

Exact manufacturer part numbers matter unless an entry explicitly permits a substitute. Quantities below are the installed quantities for one unit, not the multi-unit quantities copied from the attached cart. Distributor stock changes, so a Digi-Key search link is used where a durable exact-product link is unavailable.

## Parts installed on the PCB

### 12 V regulator module

Quantity: **one Recom R-78K12-2.0**. Install it at IC1 to create the 12 V rail used by the power stage and fan; its pinout and ratings are part of the design. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/recom-power/R-78K12-2-0/18093036).

### Inrush and reverse-polarity MOSFETs

Quantity: **three Vishay SiS862ADN-T1-GE3 MOSFETs**. Install them at Q2, Q3, and Q5 in the LM74810 input protection and controlled-inrush circuit; their voltage rating, gate charge, and PowerPAK 1212-8 footprint are design requirements. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/vishay-siliconix/SIS862ADN-T1-GE3/10273791).

### Dual input-switch MOSFET

Quantity: **one Diodes Incorporated DMTH64M2LPDWQ-13**. Install it at Q4 in the USB-PD/input switching path. This is a dual MOSFET in the specified PowerDI5060-8 package, not two interchangeable discrete transistors. Shop using the exact-part [Digi-Key search](https://www.digikey.com/en/products/result?keywords=DMTH64M2LPDWQ-13).

### Main 470 kHz RF MOSFET

Quantity: **one**, installed at Q1 as the main RF power switch. **Do not order this part from the cart yet:** the current schematic specifies only `NCHAN` in a TO-247-combo footprint and does not name an approved manufacturer part number. The cart's STP19NF20 belongs to the 13.56 MHz design, is TO-220, and is not a justified Lite substitute. Select and validate Q1's voltage rating, current rating, switching loss, thermal behavior, and exact pinout before purchasing from the [Digi-Key TO-247 N-channel MOSFET selection](https://www.digikey.com/en/products/filter/single-fets-mosfets/278).

### USB-C connector

Quantity: **one Same Sky UJC-H-G-SMT-P6-TR**. Install it at JP2 for USB-PD input power. The drilled shell stakes and six-pad charge-only footprint must match the PCB. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/same-sky-formerly-cui-devices/UJC-H-G-SMT-P6-TR/24818614).

### XT30 board connector

Quantity: **one AMASS XT30PW-M**. Install it at JP3 as the alternate high-current DC input. It is the right-angle male PCB version, not an in-line cable connector. Shop at [LCSC](https://www.lcsc.com/product-detail/plug_changzhou-amass-elec-xt30pw-m_C431092.html) or [Out of Darts](https://outofdarts.com/products/xt30pw-m-board-mount-connector-male-pcb).

### USB-PD selection header

Quantity: **one 1-by-6, 2.54 mm male header**. Install it at JP5 so the input voltage and current request can be selected. A breakaway header is acceptable if it is cut cleanly to six positions; shop using this [Digi-Key search](https://www.digikey.com/en/products/result?keywords=1x6%202.54mm%20male%20header).

### USB-PD selection shunts

Quantity: **two 2.54 mm open-top shunts**. Fit them to JP5 to make the two independent USB-PD selections. Shop for Sullins SPC02SYAN at [Digi-Key](https://www.digikey.com/en/products/detail/sullins-connector-solutions/SPC02SYAN/76375).

### 470 uF bulk capacitors

Quantity: **two Nichicon UHW1H471MPD, 470 uF, 50 V capacitors**. Install them at C12 and C13 for the 940 uF input-energy reservoir after the fuse and controlled input switch. Their 10 mm diameter and 5 mm lead spacing fit the intended footprints; do not assume every 470 uF capacitor in the cart has the same diameter. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/nichicon/UHW1H471MPD/3664388).

### Fuse clips

Quantity: **two Keystone 3518P clips**. Together they form the F1 holder for one 5-by-20 mm glass fuse. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/keystone-electronics/3518P/316011).

### Input fuse

Quantity: **one Littelfuse 0235007.MXP, 7 A, 5-by-20 mm fast-acting glass fuse**. Install it in F1 to protect the input wiring and board during a fault; buying spares is sensible, but one is installed per unit. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/littelfuse-inc/0235007-MXP/3424905).

### SMA handpiece connector

Quantity: **one SMA-J-P-H-ST-EM1 edge-launch jack**. Install it at JP1, in the orientation shown by the build guide, as the 470 kHz handpiece output. Its launch geometry must match the board footprint. Shop using the exact-part [Digi-Key search](https://www.digikey.com/en/products/result?keywords=SMA-J-P-H-ST-EM1).

### Slide switch

Quantity: **one Same Sky SLW-864574-5A-RA-N-D**. Install it at SW1 as the Lite unit's external control switch; the right-angle actuator and footprint are mechanically specific. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/same-sky-formerly-cui-devices/SLW-864574-5A-RA-N-D/24399231).

### XIAO controller module

Quantity: **one Seeed Studio XIAO SAMD21 with pre-soldered headers, part 102010388**. Install it at U1 to run the user interface, protection logic, and power control. Other XIAO-family controllers require the matching firmware and must retain compatible pins and dimensions; the SAMD21 is the module named by the current schematic. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/seeed-technology-co-ltd/102010388/13572076).

### XIAO socket headers

Quantity: **two 1-by-7, 2.54 mm female headers**. Install them at U1 so the XIAO can be removed or replaced without desoldering the module. Sullins PPPC071LFBN-RC is a suitable straight socket; shop using the exact-part [Digi-Key search](https://www.digikey.com/en/products/result?keywords=PPPC071LFBN-RC).

### Four-pin fan header

Quantity: **one Molex 0470531000**. Install it at FAN1 for the 12 V PWM fan. It is the keyed four-position PC-fan header expected by the fan cable. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/molex/0470531000/2421261).

### Optional output-selection header

Quantity: **one 1-by-4, 2.54 mm male header**. Install it at JP6 only if the selectable output-level interface will be used; it exposes the signal-name selections documented in the build guide. Shop using the [Digi-Key 1-by-4 header search](https://www.digikey.com/en/products/result?keywords=1x4%202.54mm%20male%20header).

### Optional output-selection shunts

Quantity: **two 2.54 mm open-top shunts**. Fit them across the two signal-to-ground pairs at JP6 when the output-level selection header is installed. They are additional to the two shunts used at JP5. Shop for Sullins SPC02SYAN at [Digi-Key](https://www.digikey.com/en/products/detail/sullins-connector-solutions/SPC02SYAN/76375).

### Optional external-switch header

Quantity: **one 1-by-2, 2.54 mm male header**. Install it at JP4 only if an external or development switch connection is wanted; it parallels the associated control signal and ground. Shop using the [Digi-Key 1-by-2 header search](https://www.digikey.com/en/products/result?keywords=1x2%202.54mm%20male%20header).

### Optional indicator LED

Quantity: **one 0603 LED**. Install it at LED2 only if the optional visual indication is wanted; it may be omitted without preventing normal operation. Choose a suitable low-current part using the [Digi-Key 0603 LED search](https://www.digikey.com/en/products/result?keywords=0603%20indicator%20LED).

## Custom inductors

### Kool Mu cores

Quantity: **five Magnetics 0077932A7 Kool Mu cores**. Use two stacked cores for L2 and one core each for L3, L4, and L5. The material, dimensions, and nominal 32 nH/turn² factor determine the winding instructions. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/magnetics-a-division-of-spang-co/0077932A7/18626819).

### 22 AWG magnet wire

Quantity: **at least 5 m (16.4 ft) of solid 22 AWG enamelled copper wire**. It winds L2 through L5 and leaves useful margin for trimming, measurement corrections, or one rewind. Shop for Remington 22H200P wire at [Digi-Key](https://www.digikey.com/en/products/detail/remington-industries/22H200P-125/11612956).

### Core-stacking epoxy

Quantity: **one small package of rigid epoxy**. Use a small amount to join the two 0077932A7 cores used by L2 before winding them; the joint must be thin enough that the winding-length guidance remains useful. Shop from the [McMaster-Carr epoxy selection](https://www.mcmaster.com/products/epoxy-adhesives/).

## Cooling and enclosure hardware

### Aluminum enclosure

Quantity: **one unpainted Hammond 1590T enclosure**. It is the chassis, shield, and mechanical protection for the finished unit; the project drilling templates are made for this box. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/hammond-manufacturing/1590T/131037).

### MOSFET heatsinks

Quantity: **two Boyd 504222B00000G heatsinks**. Stack them around Q1 as shown in the Lite build guide to cool the main RF switch and brace it against the enclosure wall. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/boyd-laconia-llc/504222B00000G/5833).

### Cooling fan

Quantity: **one Noctua NF-A4x10 PWM, 12 V, 40-by-40-by-10 mm fan**. It connects to FAN1 and moves air through the enclosure. The `FAN2` schematic object is only a mechanical cable-tie feature; its stale value is not the fan to buy. Shop from [Noctua's product page](https://www.noctua.at/en/products/nf-a4x10-pwm).

### TO-220/TO-247 thermal pad

Quantity: **one electrically insulating silicone transistor pad** large enough to cover Q1's metal tab where it meets the heatsink. It is mandatory because the drain tab must remain electrically isolated from the grounded mounting hardware. T-Global DC0011/06-TI900-0.12-2A is the cart's TO-220-sized option; verify that the final Q1 package is fully covered before using it, and buy a larger cut-to-size pad if Q1 is TO-247. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/t-global-technology/DC0011-06-TI900-0-12-2A/3466708).

### Transistor shoulder washer

Quantity: **one nylon shoulder washer for a #4 screw, approximately 3.7 mm outside diameter and 2 mm shoulder depth**. Fit it into Q1's tab hole to isolate the screw from the drain tab. Confirm it fits the final Q1 package. Shop for 12SWS0432 using the [Digi-Key search](https://www.digikey.com/en/products/result?keywords=12SWS0432).

### Q1 mounting screw

Quantity: **one #4-40-by-1/2-inch screw**. It passes through the shoulder washer, Q1, thermal pad, and heatsink stack shown in the build guide. Shop at [McMaster-Carr](https://www.mcmaster.com/products/socket-head-screws/thread-size~4-40/length~1-2/).

### Q1 mounting locknut

Quantity: **one #4-40 nylon-insert locknut**. It clamps the Q1 heatsink stack without loosening in service. Shop at [McMaster-Carr](https://www.mcmaster.com/products/nuts/nut-type~locknut/thread-size~4-40/).

### Brass PCB standoffs

Quantity: **four female-female M2.5 standoffs, 6 mm long with a 4.5 mm hex body**. They space and support the PCB above the enclosure lid at the four mounting holes. Shop from the [McMaster-Carr M2.5 standoff selection](https://www.mcmaster.com/products/standoffs/thread-size~m2-5/), checking the drawing for the required body dimensions.

### M2.5 PCB screws

Quantity: **eight M2.5-by-4 mm screws**. Use four to attach the standoffs to the PCB and four to attach the standoffs to the enclosure lid. Shop from the [McMaster-Carr M2.5 screw selection](https://www.mcmaster.com/products/socket-head-cap-screws/thread-size~m2-5/).

### Fan screws

Quantity: **two M3-by-20 mm screws**. They pass through the fan and its printed mounting feature. Shop at [McMaster-Carr](https://www.mcmaster.com/products/screws/thread-size~m3/length~20-mm/).

### Fan washers

Quantity: **two M3 flat washers**. They spread the fan-fastener load on the printed parts. Shop at [McMaster-Carr](https://www.mcmaster.com/products/washers/specifications-met~iso-7092/).

### Fan locknuts

Quantity: **two M3 nylon-insert locknuts**. They keep the fan fasteners from loosening under vibration. Shop at [McMaster-Carr](https://www.mcmaster.com/products/nuts/nut-type~locknut/thread-size~m3/).

### Rubber feet

Quantity: **four self-adhesive rubber bumper feet**. Put them on the enclosure lid so the finished unit does not slide or rest directly on its screw heads. Choose a small size that clears the drilling pattern from [McMaster-Carr](https://www.mcmaster.com/products/bumper-feet/).

### Thermal paste

Quantity: **one small tube**. Apply it only at the metal-to-metal heatsink interfaces shown in the build guide to reduce thermal contact resistance; it is not a substitute for the electrically insulating pad at Q1. Shop at [McMaster-Carr](https://www.mcmaster.com/products/thermal-paste/).

### PETG filament

Quantity: **one spool if printing the final plastic parts yourself**. Use PETG for the intake grille and any other piece that remains in the completed unit because it is exposed to enclosure heat. A 1 kg spool is far more than one unit consumes; shop at [Digi-Key](https://www.digikey.com/en/products/detail/douglas-innovation/P663/29571970).

### PLA filament

Quantity: **one spool if printing the disposable drilling and cutting templates yourself**. PLA is appropriate for the temporary templates, but not for final parts near the hot enclosure. Shop using the [Digi-Key PLA search](https://www.digikey.com/en/products/result?keywords=1.75mm%20PLA%201kg).

## Handpiece and tip

Choose either the open Radio Thermal handpiece path or the commercial Thermaltronics path; one complete unit does not require both.

### Open 470 kHz handpiece

Quantity: **one complete Radio Thermal handpiece**. It is the low-cost 470 kHz handpiece option connected to JP1. This is a subassembly rather than a single catalog part, so obtain the individual items and quantities from the [Radio Thermal handpiece project](https://github.com/RadioThermal/RadioThermal_Soldering_OSHW/tree/main/Handpiece).

### Commercial 470 kHz handpiece

Quantity: **one Thermaltronics SHP-KM**, if using the commercial path instead of the Radio Thermal handpiece. It holds Thermaltronics K-series cartridges; its original connector must be adapted or replaced to mate with the Hot Wand Lite's SMA output. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/thermaltronics/SHP-KM/22491869).

### K-series soldering cartridge

Quantity: **one Thermaltronics K-series cartridge**, if using the SHP-KM path. Select the geometry and temperature that suit the work; K75C002 is one example, not a universal choice. Browse the [Thermaltronics K-series catalog](https://www.thermaltronics.com/k_series.php).
