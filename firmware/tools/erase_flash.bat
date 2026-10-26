@echo off
setlocal
rem Erase all firmware and saved settings through ST-Link.
rem Upload firmware again after running this script.
rem ST-Link NRST must be wired to the MCU reset pin.
rem CubeProgrammer's under-reset connection works on this board when OpenOCD
rem identifies the MCU but times out trying to halt it after SWD pin takeover.
rem Set STM32_PROGRAMMER_CLI to override the executable path if necessary.

if defined STM32_PROGRAMMER_CLI (
    set "CUBE_CLI=%STM32_PROGRAMMER_CLI%"
    goto check_programmer
)
set "CUBE_CLI=%ProgramFiles(x86)%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
if exist "%CUBE_CLI%" goto erase_flash
set "CUBE_CLI=%ProgramFiles%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"

:check_programmer
if not exist "%CUBE_CLI%" (
    echo STM32CubeProgrammer CLI not found at "%CUBE_CLI%". 1>&2
    echo Install STM32CubeProgrammer or set STM32_PROGRAMMER_CLI to its executable. 1>&2
    exit /b 1
)

:erase_flash
echo Connecting under hardware reset; ST-Link NRST must be connected.
echo Erasing all firmware and saved settings on the connected STM32F042...
"%CUBE_CLI%" -c port=SWD freq=100 mode=UR reset=HWrst -e all
exit /b %ERRORLEVEL%
