# Hot-Wand

Compact RF soldering iron station. Compatible with 13.56 MHz handpieces. If the handpiece uses 13.56 MHz and a F-type coaxial connector, then it is compatible.

The project's name, Hot-Wand, reflects how a RF soldering iron feels like magic, and justifies their typical multiple-hundreds-of-dollars price. The spirit of this project is not to lower the price, but to make it portable.

This is nearly a clone of [SergeyMax's design](https://github.com/SergeyMax/SolderingStation), but with modifications:

 * redrawn schematic for clarity
 * layout to fit in a Hammond MFG enclosure
 * removed AC power input, uses DC inputs (battery XT-30 connector) or USB-PD (with negotiation for 28V)
 * uses a 128x32 OLED display
 * options for low power mode, automatic sleep
 * uses a STM32F042F6P6 microcontroller (more flash memory)
 * optional cooling fan, temperature sensors
 * internal glass fuse
 * substituted parts that are only available from Russia

I implemented a rather fancy GUI, fancier than what is available on a Metcal soldering station. The firmware features/supports:

 * persistent settings, edited through a setup menu
 * 3 different power levels (for adapting to different USB bricks or power banks)
 * low battery warning/cutoff
 * user triggered sleep mode (basically, the power switch)
 * idle detection for automatic screen dimming and sleep
 * various fault detection

The XT-30 connector allows the user to connect a battery pack. If the user configures what kind of battery chemistry is being used, then the firmware will automatically determine the number of series cells in the battery pack and set an appropriate low battery cutoff threshold. When the battery voltage is considered too low, the power is automatically cutoff and the user is offered an option to override the cutoff.

The USB-C connector is connected to a USB-PD negotiation IC. The negotiator is configured to request 28V at the maximum allowable current (hopefully 5A). Depending on the USB-PD host's implementation, the actual offered voltage may be 28V or 20V. If the current required is more than the current available from the USB-PD host, the user can select one of the lower power modes to avoid unexpected shutdown.

The RF power output is through a F-type connector, so it is compatible with most 13.56 MHz handpieces, such as Metcal, Thermaltronics, Hakko.

On paper, the buck converter that is actually powering the RF amplifier is set to 20V output with a 5A current limit. Thinking this means a 80W heat output is optimistic, I would expect somewhere between 50W to 80W of actual heating power, while using about 120W total from the upstream. Thus, the highest power setting is meant to be compatible with 140W USB-PD GaN chargers. The lowest power setting is roughly 50W electrically, to be compatible with 65W USB-PD chargers. The medium setting is roughly 75W electrically and is meant to be used with 100W USB-PD chargers.

When using a battery, the buck converter cannot boost the voltage, so using any DC input below about 24V would mean maximum power cannot be reached. The user does not have to set a low power mode in these cases but just don't expect the power bar being displayed to ever reach the top of the chart. The system cannot function at all below 14V. The DC input is rated for up to a 36V input. There is a glass fuse inside that limits the total system power draw to TODO amps.

While this is a DIY project and not a commercial product, it is a difficult project as it requires hand-wound inductors and tuning the inductor coils. This project is a learning experience, as I had to study the (translated from Russian) blog post made by SergeyMax in detail. See my page on the design study TODO.

## Hot-Wand Lite

In the same spirit, a compact RF soldering iron station. Compatible with 470 kHz handpieces. This is the inexpensive version. It is a clone of the [Radio Thermal](https://radiothermal.com/), but with modifications

 * layout to fit in a Hammond MFG enclosure
 * uses battery XT-30 connector or USB-PD (with negotiation for 28V) for input
 * uses a Seeed Studio XIAO SAMD21 microcontroller
 * user selectable power levels via externally pluggable jumpers
 * internal glass fuse

Physically, Hot-Wand and Hot-Wand-Lite uses the same aluminum enclosure. The power input stage are almost identical. The RF output connector is a SMA connector. [Radio Thermal](https://radiothermal.com/) (the inspiration for this) has instructions on how to build the handpiece.

This cost saving design features no fault detection.

While this is a DIY project and not a commercial product, it is a difficult project as it requires hand-wound inductors and tuning the inductor coils. Regarding how this is copying a lot of the [Radio Thermal](https://radiothermal.com/) design, I am copying it verbatim, as they do not use anything weird from Russia, and the RF design education I've already gotten from the much more complicated 13.56 MHz version of Hot-Wand.

## What is a RF soldering iron anyways?

An RF soldering iron sends high-frequency electromagnetic energy into a purpose-built tip cartridge, where a thin magnetic heating layer produces heat very close to the working end; in Curie-regulated cartridges, that layer also changes how strongly it absorbs energy as it approaches its designed temperature. Compared with a conventional iron whose resistive heater must heat a larger structure and conduct that heat to the tip, the shorter thermal path and low effective thermal lag let an RF tip respond immediately when a solder joint draws heat and recover its temperature quickly afterward. That fast, load-responsive delivery is why a nominally 50 W RF iron can feel more capable than a nominally 100 W resistive iron: the wattage rating describes available electrical power, while the user's experience is dominated by how quickly and efficiently that power reaches the joint and restores the tip temperature.

The RF soldering iron's station unit does not feature a temperature setting or even have the ability to detect the iron's temperature. When you buy the iron cartridge (which is the tip and stem), it is specific to one temperature. It will always be dead-nuts on target because of the Curie-point behavior of the magnetic alloy built into the cartridge. Below its designed temperature, the alloy is magnetic and the skin effect concentrates RF current in its thin, relatively high-resistance outer layer, producing heat right at the tip. At the Curie point the alloy loses its magnetic properties, the skin effect collapses, and the current moves into the low-resistance copper core, so heating drops sharply. When a solder joint cools the tip, the alloy becomes magnetic again and full heating immediately resumes. The temperature feedback mechanism is therefore inside the heater material itself, directly at the working tip, instead of being a sensor-and-controller loop back in the station.
