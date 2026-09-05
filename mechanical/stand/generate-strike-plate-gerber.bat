@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "BOARD=%SCRIPT_DIR%strike-plate.brd"
set "CAM_JOB=%SCRIPT_DIR%..\..\electrical\3rd-party\gerber-job-2layer.cam"
set "GERBER_SCRIPT=%SCRIPT_DIR%..\..\electrical\mfg\generate_gerbers.py"
set "OUTPUT_DIR=%SCRIPT_DIR%strike-plate-gerber"
set "OUTPUT_ZIP=%SCRIPT_DIR%strike-plate-gerber.zip"

where py >nul 2>&1
if errorlevel 1 goto :use_python

py -3 -B "%GERBER_SCRIPT%" --board "%BOARD%" --cam-job "%CAM_JOB%" --output-dir "%OUTPUT_DIR%" --archive-dir "%OUTPUT_DIR%" --no-preview --ignore-undefined-layers
set "EXIT_CODE=%ERRORLEVEL%"
goto :gerbers_finished

:use_python
where python >nul 2>&1
if errorlevel 1 goto :python_missing

python -B "%GERBER_SCRIPT%" --board "%BOARD%" --cam-job "%CAM_JOB%" --output-dir "%OUTPUT_DIR%" --archive-dir "%OUTPUT_DIR%" --no-preview --ignore-undefined-layers
set "EXIT_CODE=%ERRORLEVEL%"

:gerbers_finished
if not "%EXIT_CODE%"=="0" goto :failed

set "GENERATED_ZIP="
for %%F in ("%OUTPUT_DIR%\gerbers-strike-plate-*.zip") do set "GENERATED_ZIP=%%~fF"
if not defined GENERATED_ZIP (
    echo ERROR: The Gerber archive was not created.
    set "EXIT_CODE=1"
    goto :failed
)

move /Y "%GENERATED_ZIP%" "%OUTPUT_ZIP%" >nul
if errorlevel 1 (
    echo ERROR: Could not create "%OUTPUT_ZIP%".
    set "EXIT_CODE=1"
    goto :failed
)

echo Gerber files ready: "%OUTPUT_DIR%"
echo Gerber ZIP ready:   "%OUTPUT_ZIP%"
endlocal & exit /b 0

:python_missing
echo ERROR: Python 3 was not found in PATH.
set "EXIT_CODE=1"

:failed
echo.
echo Strike-plate Gerber generation failed.
pause
endlocal & exit /b %EXIT_CODE%
