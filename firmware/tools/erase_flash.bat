@echo off
setlocal
rem Erase all firmware and saved settings through ST-Link.
rem Upload firmware again after running this script.
set "PIO_ROOT=%PLATFORMIO_CORE_DIR%"
if not defined PIO_ROOT set "PIO_ROOT=%USERPROFILE%\.platformio"
set "OPENOCD_ROOT=%PIO_ROOT%\packages\tool-openocd"

if not exist "%OPENOCD_ROOT%\bin\openocd.exe" (
    echo OpenOCD not found at "%OPENOCD_ROOT%\bin\openocd.exe". 1>&2
    echo Install the project's PlatformIO dependencies first. 1>&2
    exit /b 1
)

echo Erasing all firmware and saved settings on the connected STM32F042...
"%OPENOCD_ROOT%\bin\openocd.exe" -s "%OPENOCD_ROOT%\openocd\scripts" -f interface/stlink.cfg -f target/stm32f0x.cfg -c "init; reset halt; stm32f0x mass_erase 0; shutdown"
exit /b %ERRORLEVEL%
