@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
set "BOARD=%SCRIPT_DIR%..\hot-wand.brd"
set "SHOPPING_ULP=%SCRIPT_DIR%shopping_exporter.ulp"
set "JLC_ULP=%SCRIPT_DIR%..\3rd-party\jlcpcb_smta_exporter.ulp"

if defined EAGLECON_EXE (
    set "EAGLECON=%EAGLECON_EXE%"
) else (
    set "EAGLECON=C:\ProgramFiles\EAGLE-7.6.0\bin\eaglecon.exe"
)
if not "%~1"=="" set "EAGLECON=%~1"

if not exist "%EAGLECON%" (
    echo ERROR: eaglecon.exe was not found:
    echo   %EAGLECON%
    echo Set EAGLECON_EXE or pass its path as the first argument.
    set "EXIT_CODE=1"
    goto :finished
)
if not exist "%BOARD%" (
    echo ERROR: Board file was not found:
    echo   %BOARD%
    set "EXIT_CODE=1"
    goto :finished
)
if not exist "%SHOPPING_ULP%" (
    echo ERROR: Shopping exporter was not found:
    echo   %SHOPPING_ULP%
    set "EXIT_CODE=1"
    goto :finished
)
if not exist "%JLC_ULP%" (
    echo ERROR: JLCPCB exporter was not found:
    echo   %JLC_ULP%
    set "EXIT_CODE=1"
    goto :finished
)

set "STAGE_DIR=%TEMP%\hot-wand-bom-cpl-%RANDOM%-%RANDOM%"
mkdir "%STAGE_DIR%" >nul 2>&1
if errorlevel 1 (
    echo ERROR: Could not create temporary directory:
    echo   %STAGE_DIR%
    set "EXIT_CODE=1"
    goto :finished
)

copy /y "%BOARD%" "%STAGE_DIR%\hot-wand.brd" >nul
if errorlevel 1 (
    echo ERROR: Could not make a temporary copy of the board.
    set "EXIT_CODE=1"
    goto :cleanup
)

set "SHOPPING_ULP_EAGLE=%SHOPPING_ULP:\=/%"
set "JLC_ULP_EAGLE=%JLC_ULP:\=/%"
set "STAGE_DIR_EAGLE=%STAGE_DIR:\=/%"

echo Generating shopping BOM...
"%EAGLECON%" -N+ "-CRUN '%SHOPPING_ULP_EAGLE%' '%STAGE_DIR_EAGLE%'; QUIT;" "%STAGE_DIR%\hot-wand.brd"
if errorlevel 1 (
    echo ERROR: Shopping BOM generation failed.
    set "EXIT_CODE=1"
    goto :cleanup
)
if not exist "%STAGE_DIR%\hot-wand-bom.csv" (
    echo ERROR: Shopping exporter did not create hot-wand-bom.csv.
    set "EXIT_CODE=1"
    goto :cleanup
)

echo Generating JLCPCB BOM and CPL...
"%EAGLECON%" -N+ "-CRUN '%JLC_ULP_EAGLE%' '%STAGE_DIR_EAGLE%'; QUIT;" "%STAGE_DIR%\hot-wand.brd"
if errorlevel 1 (
    echo ERROR: JLCPCB BOM/CPL generation failed.
    set "EXIT_CODE=1"
    goto :cleanup
)
if not exist "%STAGE_DIR%\hot-wand-bom-jlc.csv" (
    echo ERROR: JLCPCB exporter did not create hot-wand-bom-jlc.csv.
    set "EXIT_CODE=1"
    goto :cleanup
)
if not exist "%STAGE_DIR%\hot-wand-cpl.csv" (
    echo ERROR: JLCPCB exporter did not create hot-wand-cpl.csv.
    set "EXIT_CODE=1"
    goto :cleanup
)

move /y "%STAGE_DIR%\hot-wand-bom.csv" "%SCRIPT_DIR%hot-wand-bom.csv" >nul
if errorlevel 1 goto :publish_error
move /y "%STAGE_DIR%\hot-wand-bom-jlc.csv" "%SCRIPT_DIR%hot-wand-bom-jlc.csv" >nul
if errorlevel 1 goto :publish_error
move /y "%STAGE_DIR%\hot-wand-cpl.csv" "%SCRIPT_DIR%hot-wand-cpl.csv" >nul
if errorlevel 1 goto :publish_error

echo.
echo Generated:
echo   %SCRIPT_DIR%hot-wand-bom.csv
echo   %SCRIPT_DIR%hot-wand-bom-jlc.csv
echo   %SCRIPT_DIR%hot-wand-cpl.csv
set "EXIT_CODE=0"
goto :cleanup

:publish_error
echo ERROR: Could not move generated files into:
echo   %SCRIPT_DIR%
set "EXIT_CODE=1"

:cleanup
if defined STAGE_DIR if exist "%STAGE_DIR%" rmdir /s /q "%STAGE_DIR%"

:finished
if not defined EXIT_CODE set "EXIT_CODE=1"
if not "%EXIT_CODE%"=="0" (
    echo.
    echo BOM/CPL generation failed.
    pause
)

endlocal & exit /b %EXIT_CODE%
