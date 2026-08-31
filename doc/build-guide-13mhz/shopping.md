# Shopping List for Hot Wand (13.56 MHz version)

This is a practical shopping list for building **one** Hot Wand (13.56 MHz version). It supplements the fabrication files and the JLCPCB-assembled board; it is deliberately not a second BOM. Parts marked `JLC-DNP=2` in the schematic are not listed here because that value only records a temporary JLCPCB stock problem.

Exact manufacturer part numbers matter unless an entry explicitly permits a substitute. Quantities below are the installed quantities for one unit. Distributor stock changes, so a Digi-Key search link is used where a durable exact-product link is unavailable.

## Parts installed on the PCB

### 12 V regulator module

Quantity: **one Recom R-78K12-2.0**. Install it at IC1 to create the 12 V rail for the power and cooling circuitry. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/recom-power/R-78K12-2-0/18093036).

### 3.3 V regulator module

Quantity: **one Recom R-78K3.3-1.0**. Install it at IC2 to power the controller and low-voltage logic. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/recom-power/R-78K3-3-1-0/18093033).

### Inrush and reverse-polarity MOSFETs

Quantity: **three Vishay SiS862ADN-T1-GE3 MOSFETs**. Install them at Q4, Q5, and Q7 in the power input circuitry. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/vishay-siliconix/SIS862ADN-T1-GE3/10273791).

### Dual input-switch MOSFET

Quantity: **one Diodes Incorporated DMTH64M2LPDWQ-13**. Install it at Q6 in the USB-PD/input switching path. Shop using the exact-part [Digi-Key search](https://www.digikey.com/en/products/result?keywords=DMTH64M2LPDWQ-13).

### Main RF MOSFET

Quantity: **one STMicroelectronics STP19NF20**. Install it at Q1 as the main RF amplifier MOSFET. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/stmicroelectronics/STP19NF20/1852512).

### RF driver MOSFET

Quantity: **one Vishay IRF510PBF**. Install it at Q2 as the RF gate driver MOSFET. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/vishay-siliconix/IRF510PBF/811710).

### USB-C connector

Quantity: **one Same Sky UJC-H-G-SMT-P6-TR**. Install it at JP2 for USB-PD input power. The drilled shell stakes and six-pad charge-only footprint must match the PCB. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/same-sky-formerly-cui-devices/UJC-H-G-SMT-P6-TR/24818614).

### XT30 board connector

Quantity: **one XT30PW-M**. Install it at JP3 as the high-current DC input. It is the right-angle male PCB version. Search for this on your preferred online shop.

### USB-PD selection header

Quantity: **one six-pin segment cut from a 1-by-40, 2.54 mm right-angle male breakaway header**. Install the six-pin segment at JP5 so the input voltage and current request can be selected. A long strip is inexpensive and leaves plenty of spare header for other projects. Search for [2.54 mm right-angle breakaway headers on Amazon](https://www.amazon.com/s?k=2.54mm+right+angle+male+breakaway+header+1x40).

### USB-PD selection shunts

Quantity: **two installed; buy one bulk bag of generic 2.54 mm jumper shunts**. Fit two shunts to JP5 to make the independent USB-PD selections. Prefer the taller style with a finger tab or handle because it is much easier to move in the completed unit; search for [bulk 2.54 mm pull-tab jumper caps on Amazon](https://www.amazon.com/s?k=2.54mm+jumper+caps+with+handle+pull+tab).

### 470 uF bulk capacitor

Quantity: **one Nichicon UHW1H471MPD, 470 uF, 50 V capacitor**. Install it at C6 for input-energy storage. Its 10 mm diameter and 5 mm lead spacing fit the intended footprint. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/nichicon/UHW1H471MPD/3664388).

### 100 uF capacitors

Quantity: **two Panasonic EEU-FR1H101B, 100 uF, 50 V capacitors**. Install them at C2 and C4 for local rail filtering. Their 8 mm diameter and 5 mm lead spacing match the board. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/panasonic-industry/EEU-FR1H101B/2504106).

### Fuse clips

Quantity: **two Keystone 3518P clips**. Together they form the F1 holder for one 5-by-20 mm glass fuse. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/keystone-electronics/3518P/316011).

### Input fuse

Quantity: **one Littelfuse 0235007.MXP, 7 A, 5-by-20 mm fast-acting glass fuse**. Install it in F1 to protect the input wiring and board during a fault; buying spares is sensible, but one is installed per unit. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/littelfuse-inc/0235007-MXP/3424905).

### Input filter inductor

Quantity: **one Würth Elektronik 744750460220, 22 uH inductor**. Install it at L1 in the input filter. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/w%C3%BCrth-elektronik/744750460220/6598151).

### Axial 22 uH inductor

Quantity: **one Bourns 5300-17-RC**. Install it at L9. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/bourns-inc/5300-17-RC/3193284).

### F-type handpiece connector

Quantity: **one Molex 0733300030 PCB F-type coaxial jack**. This is a F-type coaxial RF connector. Install it at JP1 as the 13.56 MHz handpiece output. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/molex/0733300030/2755903).

### Pushbutton

Quantity: **one APEM MJTP1236D**. Install it at SW1 as the front-panel control button; Shop at [Digi-Key](https://www.digikey.com/en/products/detail/apem-inc/MJTP1236D/1798058).

### OLED display module

Quantity: **one 0.91-inch, 128-by-32, four-pin SSD1306 I2C module**. Install it at U4 for the user interface. Buy only a module with the board's expected physical size and pin order, **GND, VCC, SCL, SDA**; visually similar modules are wired differently. A practical source is this exact-description, [Amazon search](https://www.amazon.com/s?k=0.91+SSD1306+128x32).

### SWD programming header

Quantity: **one six-pin segment cut from a long 2.54 mm female breakaway socket strip**. Install it at JP4 on the underside of the board so the controller can be flashed and debugged. Buy a long strip and cut it to six positions rather than ordering a single expensive connector; search for [2.54 mm female breakaway headers on Amazon](https://www.amazon.com/s?k=2.54mm+female+breakaway+header+1x40).

### 10 kΩ trimmer

Quantity: **one Bourns TC33X-2-103E**. Install it at VR1 for the adjustment assigned by the schematic. Shop using the exact-part [Digi-Key search](https://www.digikey.com/en/products/result?keywords=TC33X-2-103E).

### 1 kΩ trimmers

Quantity: **two Bourns TC33X-2-102E**. Install them at VR2 and VR3 for calibration of the two sensing channels. Shop using the exact-part [Digi-Key search](https://www.digikey.com/en/products/result?keywords=TC33X-2-102E).

### 4.7 kΩ trimmer

Quantity: **one Bourns TC33X-2-472E**. Install it at VR4 for its schematic adjustment. Shop using the exact-part [Digi-Key search](https://www.digikey.com/en/products/result?keywords=TC33X-2-472E).

### Power-MOSFET thermistors

Quantity: **two Mitsubishi BN35-3T103FB-100 thermistors**. Install them at THERM1 and THERM2 against the two TO-220 devices so firmware can detect unsafe device temperatures. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/mitsubishi-materials-u-s-a-corporation/BN35-3T103FB-100/14308036).

### Four-pin fan header

Quantity: **one Molex 0470531000**. Install it at FAN1 for the 12 V PWM fan. It is the keyed four-position PC-fan header expected by the fan cable. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/molex/0470531000/2421261).

### Optional indicator LEDs

Quantity: **up to four 0603 LEDs**, one each for LED1 through LED4. They are visual indicators and the build guide permits them to be omitted; using different colors makes the indications easier to distinguish. Choose suitable low-current 0603 LEDs using the [Digi-Key 0603 LED search](https://www.digikey.com/en/products/result?keywords=0603%20indicator%20LED).

## Custom magnetics

### Fair-Rite toroid cores

Quantity: **three Fair-Rite 5961004901 cores**. Two are used for L3 and L7, and the third is used for transformer T1. Their material and dimensions determine the winding values. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/fair-rite-products-corp/5961004901/8594132).

### Amidon T130-6 cores

Quantity: **three Amidon T130-6 cores**. Wind L4, L5, and L6 on one core each; the `-6` material is essential to the stated turns and inductance. Shop from [Amidon](https://www.amidoncorp.com/t130-6/).

### 22 AWG magnet wire

Quantity: **at least 3 m (10 ft) of solid 22 AWG enamelled copper magnet wire**. It winds L3, L7, L8, and both T1 secondaries, with margin for the one-turn primary, trimming, and one rewind. A small hobby spool is much more economical than distributor-cut wire; search for [22 AWG enamelled copper magnet wire on Amazon](https://www.amazon.com/s?k=22+AWG+enameled+copper+magnet+wire). Make sure it is solid copper, not copper-clad aluminum.

### 16 AWG magnet wire

Quantity: **at least 1.5 m (5 ft) of solid 16 AWG enamelled copper magnet wire**. It winds the three large T130-6 inductors L4, L5, and L6 with working margin. Buy a small hobby spool from [Amazon](https://www.amazon.com/s?k=16+AWG+enameled+copper+magnet+wire), and make sure it is solid copper rather than copper-clad aluminum.

## Cooling and enclosure hardware

### Aluminum enclosure

Quantity: **one unpainted Hammond 1590T enclosure**. Shop at [Digi-Key](https://www.digikey.com/en/products/detail/hammond-manufacturing/1590T/131037).

### Main heatsink

Quantity: **one HSB21-454515 heatsink, 45-by-45-by-15 mm**. It cools Q1 and Q2 and transfers their heat to the enclosure wall. Shop using the exact-part [Digi-Key search](https://www.digikey.com/en/products/result?keywords=HSB21-454515).

### Cooling fan

Quantity: **one Noctua NF-A4x10 PWM, 12 V, 40-by-40-by-10 mm fan**. Shop from [Noctua's product page](https://www.noctua.at/en/products/nf-a4x10-pwm).

This can be substituted for any cheap 40mm x 40mm x 10mm 12V cooling fan.

### TO-220 thermal pads

Quantity: **two electrically insulating silicone TO-220 pads**. Place one between each of Q1 and Q2 and the heatsink; they are required because both transistor tabs must remain electrically isolated. T-Global `DC0011/06-TI900-0.12-2A` is suitable; shop at [Digi-Key](https://www.digikey.com/en/products/detail/t-global-technology/DC0011-06-TI900-0-12-2A/3466708).

### TO-220 shoulder washers

Quantity: **two nylon shoulder washers for #4 screws, approximately 3.7 mm outside diameter and 2 mm shoulder depth**. Fit one into each TO-220 tab hole to isolate its mounting screw. Shop for `12SWS0432` using the [Digi-Key search](https://www.digikey.com/en/products/result?keywords=12SWS0432).

### #4-40 enclosure and heatsink screws

Quantity: **eleven #4-40-by-3/8-inch screws; buy a small box**. They fasten Q1, Q2, the heatsink, the exhaust duct, and the faceplate. Search for [#4-40-by-3/8-inch machine screws on Amazon](https://www.amazon.com/s?k=%234-40+x+3%2F8+machine+screws), or buy them from a local hardware store.

### Brass PCB standoffs

Quantity: **six female-female M2.5 standoffs, 6 mm long with a 4.5 mm hex body, made of brass**. Search for [M2.5-by-6 mm brass female-female standoffs on Amazon](https://www.amazon.com/s?k=M2.5+6mm+female+female+brass+standoff), checking the product drawing for the required body dimensions.

### M2.5 PCB screws

Quantity: **twelve M2.5-by-4 mm screws**. Use six to attach the standoffs to the PCB and six to attach the standoffs to the enclosure lid. Buy a small pack or metric electronics-hardware assortment from [Amazon](https://www.amazon.com/s?k=M2.5+x+4mm+screws).

### Fan screws

Quantity: **two M3-by-20 mm screws**. They pass through the fan and its printed mounting feature.

This can be substituted liberally as long as you also substitute the nuts and washers these will screw into.

### Fan washers

Quantity: **two M3 flat washers**. They spread the fan-fastener load on the fan.

This can be substituted liberally as long as they fit the screws being used with it.

### Fan locknuts

Quantity: **two M3 nylon-insert locknuts**. They keep the fan fasteners from loosening under vibration.

This can be substituted liberally as long as they fit the screws being used with it.

### Rubber feet

Quantity: **four self-adhesive rubber bumper feet**. Put them on the enclosure lid so the finished unit does not slide or rest directly on its screw heads. Buy a generic sheet or bulk pack of [small adhesive rubber feet from Amazon](https://www.amazon.com/s?k=small+self+adhesive+rubber+bumper+feet).

### Copper sheet for PCB fins

Quantity: **one small piece of 0.5 mm (approximately 0.020 inch) copper sheet**. Cut it into the 5 mm-tall fins described in the assembly supplement to move heat away from the regulator area into the fan airflow. A small hobby sheet is sufficient; search for [0.5 mm copper sheet on Amazon](https://www.amazon.com/s?k=0.5mm+copper+sheet), or check a local hobby or metal shop.

### Thermal paste

Quantity: **one small tube of ordinary non-electrically-conductive CPU thermal paste**. Apply it between the main heatsink and the aluminum enclosure wall to reduce their thermal contact resistance; it is not a substitute for the insulating pads beneath Q1 and Q2. Any inexpensive generic paste is adequate. Search [Amazon](https://www.amazon.com/s?k=non+conductive+CPU+thermal+paste), and do not use liquid metal.

### High-temperature epoxy

Quantity: **one small package of high-temperature two-part epoxy**. Use small amounts to secure the two thermistors in reliable thermal contact with Q1 and Q2, following the build guide and keeping it off electrical joints. A consumer metal-repair epoxy such as original J-B Weld is sufficient; search [Amazon](https://www.amazon.com/s?k=high+temperature+two+part+epoxy+metal), or buy it from an automotive or hardware store.

### Small cable ties

Quantity: **at least two small cable ties; buy one inexpensive bulk bag**. They provide strain relief for the thermistor leads and the fan cable at the PCB tie-down features. Search for [4-inch nylon cable ties on Amazon](https://www.amazon.com/s?k=4+inch+nylon+cable+ties+bulk), or buy them from a local hardware store.

### 3D-printer filament

Quantity: **enough PETG for the final plastic parts and enough PLA for the disposable templates, if printing them yourself**. Use PETG for the faceplate, airflow parts, and anything that remains near the hot enclosure. PLA is adequate for drilling and cutting templates that are discarded after use. One standard spool of each is far more than a single unit consumes, so use filament already on hand or ask someone to print the parts.

## Handpiece and tip

Choose **one** of the handpiece systems below. The Thermaltronics, Metcal, and Hakko choices all use a standard F-type RF connection and operate at 13.56 MHz. Thermaltronics and Metcal share the STTC/SMTC cartridge ecosystem, while the Hakko FX-1001 uses its own T31 cartridges.

### Thermaltronics SHP-1 handpiece

The least-expensive new, directly compatible option. The SHP-1 is made for Thermaltronics' 13.56 MHz TMT-9000 system and is also specified for Metcal MX systems. It accepts Thermaltronics M-series and Metcal STTC/SMTC-series cartridges and connects through the F-type handpiece interface used by this project. See the official [Thermaltronics SHP-1 datasheet](https://www.thermaltronics.com/downloads/SHP-1_Datasheet.pdf).

### Metcal MX-RM3E handpiece

An alternative to the SHP-1. This is the conventional genuine Metcal MX handpiece, uses the F-type RF connection, and accepts STTC and SMTC cartridges as well as several newer compatible Metcal cartridge families. It is directly compatible, but costs more when purchased new; used units can be a good value. See the official [Metcal MX-RM3E page](https://store.metcal.com/en-us/shop/soldering-desoldering/hand-pieces/MX-RM3E).

### Metcal MX-H1-AV advanced handpiece

Another genuine Metcal alternative. It uses the F-type RF connection and accepts STTC and SMTC cartridges. It is directly compatible but is normally the most expensive of the straightforward choices. See the official [Metcal MX-H1-AV page](https://store.metcal.com/en-us/shop/soldering-desoldering/hand-pieces/MX-H1-AV).

### Hakko FX-1001 handpiece

A directly connectable alternative to the Thermaltronics and Metcal handpieces. The FX-1001 has a standard F-type connector, operates at 13.56 MHz, and uses Hakko T31 cartridges. Hakko explicitly confirms that the FX-1001 and T31 cartridges can be used with 13.56 MHz Metcal supplies equipped with a standard F-type connector; see Hakko's official article, [Can I use my HAKKO FX-1001 Soldering Iron Handpiece and T31 Series Tips with my Metcal Power Supply?](https://kb.hakkousa.com/Knowledgebase/10637/Can-I-use-my-HAKKO-FX1001-Soldering-Iron-Handpiece-and-T31-Series-Tips-with-my-Metcal%C2%AE-Power-Supply). Also see the official [Hakko FX-1001 handpiece page](https://hakkousa.com/fx-1001-rf-induction-heating-soldering-iron-handpiece-only.html) and [FX-100 specifications](https://www.hakko.com/english/products/hakko_fx100_spec.html).

The connector and operating frequency are compatible without an adapter. However, because the Hot Wand's matching network and tip-disconnect detector were developed around an STTC cartridge, verify normal heating, power delivery, and disconnect detection with the selected T31 cartridge during bring-up.

### STTC/SMTC or Thermaltronics M-series cartridge

When using a Thermaltronics SHP-1 or compatible Metcal handpiece. Select the temperature series and tip geometry for the intended work. STTC and SMTC cartridges fit both the SHP-1 and the Metcal choices above; Thermaltronics M-series cartridges are specified for the SHP-1 but should not be assumed to fit every Metcal handle. Browse Metcal's official [cartridge and handpiece compatibility guide](https://www.metcal.com/wp-content/uploads/2026/03/Metcal_Cartridge-Guide-EN.pdf).

### Hakko T31 cartridge

Quantity: **one**, when using the Hakko FX-1001 option. T31 cartridges are made for the FX-1001 and are not mechanically interchangeable with STTC, SMTC, or Thermaltronics M-series cartridges. Choose the desired fixed temperature series and geometry from Hakko's official [T31 cartridge list](https://www.hakko.com/english/products/hakko_fx100_tips.html).
