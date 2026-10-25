#!/usr/bin/env sh
# Erase all firmware and saved settings through ST-Link.
# Upload firmware again after running this script.
set -eu

pio_root="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
openocd_root="$pio_root/packages/tool-openocd"
openocd="$openocd_root/bin/openocd"
if [ ! -x "$openocd" ] && [ -f "$openocd.exe" ]; then
    openocd="$openocd.exe"
fi

if [ ! -x "$openocd" ]; then
    printf 'OpenOCD not found at "%s".\nInstall the project\047s PlatformIO dependencies first.\n' "$openocd" >&2
    exit 1
fi

printf 'Erasing all firmware and saved settings on the connected STM32F042...\n'
exec "$openocd" -s "$openocd_root/openocd/scripts" \
    -f interface/stlink.cfg -f target/stm32f0x.cfg \
    -c 'init; reset halt; stm32f0x mass_erase 0; shutdown'
