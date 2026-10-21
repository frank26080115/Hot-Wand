# XIAO ESP32-C3 on a reworked V1 PCB

Select **PCB version `-1`** for this specific wiring. Ordinary V1 remains version `1`, and V2 remains version `2`.

## Hardware

With all power disconnected:

1. Isolate the XIAO's **D9 header connection** from the PCB's D9 contact. Leave the module's BOOT button and GPIO9 pull-up intact.
2. Jumper the **PCB D9/PWR-DIS contact** to the **PCB D3 contact**, which mates with **XIAO D3/GPIO5**. D3 is unused on the original V1 PCB. Keeping this jumper on the PCB lets the module remain removable for flashing.
3. Check that the PCB switch signal reaches D3 and is isolated from the module's D9.

| Function | XIAO pin | GPIO |
| --- | --- | ---: |
| RF output | D1 | 3 |
| External LED | D10 | 10 |
| Eco selection | D5 | 7 |
| Sport selection | D4 | 6 |
| Power switch, jumper from PCB D9 | D3 | 5 |
| Fan | D7 | 20 |
| Voltage measurement | D2 | 4 |
| Other voltage-divider connection, left as input | D8 | 8 |

## Build and flash

From `firmware-lite`, run:

```powershell
pio run -e seeed_xiao_esp32c3_v1_reworked
```

The environment selects `HOTWANDLITE_PCB_VERSION=-1` without changing the default PCB selection for other environments. It is restricted to the XIAO ESP32-C3.

**Remove the module from the PCB before flashing.** The PCB's voltage-divider connection to D8/GPIO8 can interfere with entering the ROM bootloader. Disconnect main power before removing or reinstalling the module. Upload while it is removed:

```powershell
pio run -e seeed_xiao_esp32c3_v1_reworked -t upload
```

This selects the pin map only; RF frequency and power-mode behavior are unchanged. The build must still be checked on the reworked hardware, including booting with both switch positions, switch operation, fan operation, and voltage measurement, before powering the RF stage normally.
