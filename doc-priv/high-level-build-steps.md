## 0. Circuit Board Reception

Get the circuit board from JLCPCB

Populate all out-of-stock components that JLCPCB did not populate

## 1. Input Power

Populate power input MOSFETs

Populate the USB-C

Populate the XT30 connector

Populate JP5, which is the header for the shunt jumpers that configures USB-PD negotiation

Test input power. First test XT30 connector and check if the ideal-diode works. I recommend using a smoke-stopper device for this test if using a battery, otherwise, use a non-battery DC input with a XT30 connector.

Then test USB-C negotiation with a variety of configurations (20V vs 28V, 5A limit vs automatic current limit)

During the USB-C tests, the shunt jumpers should have been installed to JP5

(OPTIONAL) Populate LEDs

Populate the potentiometers. Make note of their orientation and correlate spin direction with resistances.

Populate the fuse holder. Do a quick check to make sure it doesn't blow a fuse immediately.

Populate all through hole capacitors

## 2. Power Supplies

Populate the 12V buck converter. Populate the 3.3V buck converter (or voltage regulator).

Test the output of these buck converters (and/or voltage regulator). Power up the circuit using a current limited power supply of some kind, and measure the 12V and 3.3V nodes using a multimeter, making sure they are measuring very close (within +/- 5%) to 12V and 3.3V.

Add the custom cut copper fins to the buck converter heat dissipation area.

Attach all brass standoffs to the bottom of the circuit board, using M2.5 x 6mm screws. Use low or medium strength thread-locker if available. Using tooth-lock washers is also optional.

Populate the L1 inductor. This is the main buck converter's first stage inductor.

Now, when powered up, the main buck converter will start outputting some voltage. Adjust the potentiometer to vary the voltage, test up to the limit, then set it to the lowest possible setting.

Populate the L9 inductor. This is the filter inductor for the tip detector.

## 3. Controls and Firmware Bring-Up

Populate tactile button, OLED screen, coax connector

Populate the fan connector

Configure the fan solder jumpers 

Populate the debug header. It is a 6 pin female header with 0.1 inch pin pitch on the bottom of the circuit board.

Flash firmware

(DEVELOPMENT ONLY) Test firmware as much as possible, see all bring-up tests

## 4. Custom Inductors and Measurement Jumpers

Wind custom inductors (way more detailed instructions)

(DEVELOPMENT ONLY) measure custom wound inductor for inductance

Populate custom inductors

(DEVELOPMENT ONLY) Sanity check buck converter voltage isn't affected by power factor detector circuit

Either short out with solder, or populate SJ6 with a 0.01 ohm measurement resistor, which is used to measure drain current of Q1.

Either short out with solder, or populate SJ8 with a 0.1 ohm measurement resistor, which is used to measure the current consumption of the 12V rail, to determine the gate driver's efficiency

## 5. Printed Parts and Machining Templates

3D print all drilling and cutting templates

3D print: face plate, air intake grille, air exhaust duct, internal air blockers

## 6. Power MOSFET and Heatsink Assembly

Drill and thread tap the heatsink. Cut some of the fins off the heatsink where screws will sit over. Clean metal shavings from heatsink.

Attach big MOSFETs to heatsink. Clean the surface of the heatsink and the back of both MOSFETs with rubbing alcohol. Place a silicone thermal pad between each MOSFET and the heatsink. Insert a nylon shoulder washer (for #4 screws with 3.7mm OD and 2mm depth) into the tab's hole. Fasten the MOSFET to the heatsink using a #4-40 x 3/8 inch long screw through the nylon shoulder washer. Before tightening completely, make sure the MOSFET legs are aligned with their perspective footprints on the circuit board. Tighten the screw completely after alignment is ensured. Cut off the excess silicone thermal pad with an Xacto knife.

Check to make sure heatsink is not causing continuity problems. The drain tab of each MOSFET must not be conductive with the heatsink, nor with each other.

Bend the MOSFET legs, refer to the diagram. The goal is so that the heatsink can mate with the box's face once the box is dropped down on the circuit board.

Solder MOSFETs to PCB, continue to adjust the angle of the MOSFET's legs as needed.

Wire up the NTC thermistors and attach them to the MOSFET's plastic face. First, ziptie the wires of the NTC resistors to the drain leg of the MOSFET. Then, use high temperature epoxy to adhere the bead of the NTC thermistor to the surface of the MOSFET. Please refer to photograph.

Perform mandatory safety related firmware tests (tip detection, NTC thermistors, warning near 8S input voltages)

(DEVELOPMENT ONLY) full functionality testing while not enclosed, tune the air-core inductor in the gate driver circuit, perform burn-in tests, see detailed test plan

## 7. Enclosure Preparation

Drill and cut enclosure box and lid. Please refer to diagram. Use the cutting and drilling templates to help.

Thread tap enclosure box where needed. Please refer to diagram.

For a cleaner build, the holes on the bottom lid can be countersunk, and then countersunk screws can be used instead.

Clean all metal shavings, debur all drilled and cut edges, dull all sharp edges

## 8. Final Assembly

Fasten PCB to bottom lid, using the brass standoffs installed previously and M2.5 x 6mm screws.

Assemble cooling fan and the air intake grille to the box. See diagram for details.

Clean the heatsink surface and the surface of the box where it will sit, use rubbing alcohol.

Apply thermal paste to the box where the heatsink will sit

Plug in the fan to the circuit board

Drop the box over the whole assembly

Secure the heatsink with screws and washers

Clean up thermal paste that got squished out

Fasten on the air exhaust duct

Fasten on the face plate

## 9. Enclosed Development

(DEVELOPMENT ONLY) further firmware development
