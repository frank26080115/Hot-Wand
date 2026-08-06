@echo off
setlocal

pushd "%~dp0"
if errorlevel 1 goto :launch_error

where py >nul 2>&1
if errorlevel 1 goto :use_python

py -3 -B "%~dp0..\mfg\generate_gerbers.py" --board "%~dp0..\hot-wand-lite.brd" --output-dir "%~dp0gerbers" --archive-dir "%~dp0" --preview-script "%~dp0..\mfg\generate_gerbv_preview_svg.py" %*
set "EXIT_CODE=%ERRORLEVEL%"
goto :finished

:use_python
where python >nul 2>&1
if errorlevel 1 goto :python_missing
python -B "%~dp0..\mfg\generate_gerbers.py" --board "%~dp0..\hot-wand-lite.brd" --output-dir "%~dp0gerbers" --archive-dir "%~dp0" --preview-script "%~dp0..\mfg\generate_gerbv_preview_svg.py" %*
set "EXIT_CODE=%ERRORLEVEL%"
goto :finished

:python_missing
echo ERROR: Python 3 was not found in PATH.
set "EXIT_CODE=1"
goto :finished

:launch_error
echo ERROR: Could not enter the script directory.
set "EXIT_CODE=1"
goto :wait

:finished
popd

:wait
if not "%EXIT_CODE%"=="0" (
    echo.
    echo Gerber generation failed.
    pause
)

endlocal & exit /b %EXIT_CODE%
