Get the circuit board from JLCPCB

Populate all out-of-stock components that JLCPCB did not populate

Populate power input MOSFETs

Populate JP5, which is the header for the shunt jumpers that configures USB-PD negotiation

Populate the USB-C

Populate the XT30 connector

Test input power. First test XT30 connector and check if the ideal-diode works. Then test USB-C negotiation with a variety of voltages

During the USB-C tests, the shunt jumpers should have been installed to JP5

(OPTIONAL) populate LEDs

Populate the potentiometers. Make note of their orientation and correlate spin direction with resistances.

Populate the fuse holder. Do a quick check to make sure it doesn't blow a fuse immediately. LEDs might light up.

Populate all through hole capacitors

Populate the 12V buck converter, and test its output.

Populate the 3.3V buck converter (or voltage regulator), and test its output.

Add the custom cut copper fins to the buck converter heat dissipation area.

Attach all brass standoffs

Populate the L1 inductor

Now, when powered up, the main buck converter will start outputting some voltage. Adjust the potentiometer to vary the voltage, test up to the limit, then set it to the lowest possible setting.

Populate the L9 inductor

Populate tactile button, OLED screen, coax connector

Populate the fan connector

Configure the fan solder jumpers 

Populate the debug header

Flash firmware

(DEVELOPMENT ONLY) Test firmware as much as possible, see all bring-up tests

Perform mandatory safety related firmware tests (tip detection, NTC thermistors, warning near 8S input voltages)

Wind custom inductors (way more detailed instructions)

(DEVELOPMENT ONLY) measure custom wound inductor for inductance

Populate custom inductors

(DEVELOPMENT ONLY) Sanity check buck converter voltage isn't affected by power factor detector circuit

Either short out or populate SJ6, which is used to measure drain current of Q1

Either short out or populate SJ8, which is used to measure the current consumption of the 12V rail, to determine the gate driver's efficiency

3D print all drilling and cutting templates

3D print: face plate, air intake grille, air exhaust duct, internal air blockers

Drill and thread tap the heatsink. Cut some of the fins off the heatsink where screws will sit over. Clean metal shavings from heatsink.

Attach big MOSFETs to heatsink (more details)

Check to make sure heatsink is not causing continuity problems

Bend the MOSFET legs

Solder MOSFETs to PCB

Wire up the NTC thermistors and attach them to the MOSFETs

(DEVELOPMENT ONLY) full functionality testing while not enclosed, tune the air-core inductor in the gate driver circuit, perform burn-in tests, see detailed test plan

Drill and cut enclosure box and lid

Thread tap enclosure box where needed

Clean all metal shavings

Fasten PCB to bottom lid

Assemble cooling fan and the air intake grille to the box

Clean the heatsink surface and the surface of the box where it will sit, use rubbing alcohol

Apply thermal paste to the box where the heatsink will sit

Plug in the fan to the circuit board

Drop the box over the whole assembly

Secure the heatsink with screws and washers

Clean up thermal paste that got squished out

Fasten on the air exhaust duct

Fasten on the face plate

(DEVELOPMENT ONLY) further firmware development
