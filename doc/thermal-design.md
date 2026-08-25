When the project started, it didn't look like SergeyMax focused on thermal very much. The box is plain looking without vent holes and the MOSFETs only have very small heatsinks.

![](imgs/sergeymax_thermal_first.png)

The builder over at EEVblog named `rfmerrill` has a photo of his own build and he stuck the MOSFETs against the enclosure wall with some thermal pads in between.

![](imgs/rfmerrill_heatsink.jpg)

`rfmerrill` did mention that:

> recommend connecting a nearby ground to Q2's screw to shunt the capacitive coupled noise that will otherwise return through the housing and output jack

also

> Switch the main power transistor back to the "good" silpad from the cheap amazon one (which I used briefly because I couldn't find the good ones)

Years later, SergeyMax released a follow-up video. In it, his own unit has operated for a few years but he added a gigantic heatsink and a small cooling fan to it

![](imgs/sergeymax_update_heatsink.png)
![](imgs/sergeymax_update_fan.png)

This prompted me to update my design. I will put a Noctua NF-A4x10 fan inside mine. I use these fans in many places, such as on my 3D printer and a they also cool my archival external hard-drives. They are quiet and last for years.

The plan is to mount it inside the enclosure on the side, on the wall where the power input connectors are. I chose a side mount instead a top mount to lessen the chance of something falling inside the enclosure.

The MOSFETs will be mounted against a large square heatsink. The legs will have to be bent a lot for the back of the TO-220 to reach the heatsink. The heatsink is also attached to the outside of the aluminum enclosure. The assembly of this arrangement should be straight forward.

![](imgs/thermal_3d_1.png)

It's hidden but there is an air exit at the foot of the MOSFETs. There is a 3D printed air-duct that will redirect exiting airflow up through the heatsink fins.

![](imgs/thermal_3d_duct_cross.png)

To encourage the airflow through this exit, all other air escape gaps not useful for cooling are covered by 3D printed features.

![](imgs/thermal_3d_air_gaps_covered.png)

To further prevent debris from entering the enclosure, a louvered air intake grille is 3D printed, with slats facing downwards.

![](imgs/louvered_intake_grille.png)

## Buck Converter Cooling

The PCB is made with 2oz thick copper for better heat dissipation. This is important for the `TPS54560` buck converter. There are some vent air exit slits near where the buck converter is. That region has a brass standoff connecting that copper pour to the bottom plate of the enclosure. I also soldered on some copper cooling fins to the exposed copper pour in that region.

The `TPS54560` buck converter has its own junction thermal limit.

## Fan Control

The fan is powered by 12V. The 12V comes from a secondary buck converter and is rated for 2A. The fan is controlled by a small MOSFET. As the fan only needs about 50mA, none of this is concerning.

The fan is controlled by the microcontroller. The microcontroller drives the MOSFET, and the MOSFET is used for low-side switching the fan. Additionally, the circuit can be reconfigured:

 * the microcontroller can drive a PWM signal directly
 * the MOSFET can drive a PWM signal as open-drain (the microcontroller will invert the signal)
 * the MOSFET can be driven with PWM and attempt to speed-control a fan that lack PWM inputs

The firmware allows the user to pick between 16 different fan control modes, in three broad categories:

 * simple and force the fan to a certain speed
 * automatic modes triggered by a particular temperature threshold
 * adaptive modes that adjusts fan speed based on temperature detected

## Temperature Sensors

The microcontroller always has its own internal temperature sensor that is being monitored, but it is questionable if this is useful, as it is placed very far away from RF and power components.

There are connections and voltage dividers for two NTC thermistors. These are optional, but I have attached the NTC thermistors to the plastic casing of the two main MOSFETs.

## Resistor Changes from Original Design

In SergeyMax's design, he used two 2.2 ohm resistors in series (doing both source and sink) between the output of his MAX17602 and the IRF510 MOSFET. The foorprint are 0805 and the power rating is not specified, but I think 0805 can go up to 0.5W if you shop hard enough. In my design, I have two 3.6 ohm resistors here doing the same job, one for source and one for sink. Calculations showed that these should be worst case 0.75W rated so I used a 1206 footprint instead.

The 22 ohm resistor used at the input of the tip-detector circuit is an ordinary 0805 footprint resistor in SergeyMax's design. Many EEVBlog users reported that this resistor tended to fail when the tip is actually removed. In my design, these have been beefed up to a 2512 footprint resistor rated for 2W. Better safe than sorry.

At the gate of the main amp MOSFET, there are 150 ohm resistors that SergeyMax specified to be 2W rated and he used a axial package. In my design, I still used a 2W rating but I used a SMD 2512 package, with plenty of ground stitching vias (and 2oz copper PCB). for 12V DC conditions, 150 ohm should cause a bit under 1W of heat, and this net is supposed to be a 12V RF gate clock so we expect a bit less.