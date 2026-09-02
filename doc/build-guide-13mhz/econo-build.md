# Economy Build for Hot Wand 13.56 MHz version

There's a way to build the 13.56 MHz version of Hot Wand for cheap, using a 3D printed enclosure and skipping a lot of components.

If you wish to build this way, then you need to be smart and pay attention to both documents to figure out what to buy and what not to buy.

Do not buy any of the brass standoffs.

Do not buy the aluminum enclosure.

The screws used are `M2.5 x 8mm` and `#4-40 x 0.5 in`. There are no nuts or washers required at all.

Do not buy thermal paste, it is not needed.

You do still need to make the custom heatsink fins for the buck converter out of the copper sheet.

Note: This design only works for a heatsink that is 45mm x 45mm x 15mm, `HSB21-454515`. This heatsink is cheaper on Digi-Key than ones on Amazon even in bulk.

## 3D Printed Box

The box is designed as two pieces. 3D print them both out a temperature resistant material, such as PETG.

The heatsink needs a drilling template, you can print this out of PLA.

## MOSFET and Heatsink Mounting

![](imgs/econo_mosfet_stack.png)

The MOSFET legs do NOT need to be bent.

See original instructions about attaching the thermistors.

## Fan Installation

The fan is inserted into the plastic enclosure and held in place using hot glue. No fasteners are used.

![](imgs/econo_install_fan.png)

Make sure the airflow direction is into the box.

Plug in the fan before closing the box.

## Screwing the PCB Down

**Perform a final review of all soldering, including test points, test resistors, solder jumpers.**

Clean the entire PCB, using an antistatic brush and rubbing alcohol.

At this point, you may apply conformal coating over the circuit board if you wish.

Use M2.5 x 8mm screws to screw down the PCB. Brass standoffs are not used.

![](imgs/econo_screw_down_pcb.png)

Plug in the fan before closing the box.

## Final Box Closure

Use M2.5 x 8mm screws to fasten the enclosure box to the enclosure bottom lid.

![](imgs/econo_screw_down_box.png)
