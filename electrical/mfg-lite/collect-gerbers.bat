@echo off
setlocal

set "SOURCE_DIR=%~dp0.."
set "GERBER_DIR=%~dp0gerbers"
set "PROJECT_NAME=hot-wand-lite"

if not exist "%GERBER_DIR%" (
    echo Creating "%GERBER_DIR%"...
    mkdir "%GERBER_DIR%"
    if errorlevel 1 goto :error
)

echo Moving CAM processor artifacts...
set "MOVED_ANY=0"
for %%F in ("%SOURCE_DIR%\%PROJECT_NAME%-*.*") do (
    if exist "%%~fF" (
        move /Y "%%~fF" "%GERBER_DIR%\" >nul
        if errorlevel 1 goto :error
        set "MOVED_ANY=1"
    )
)

if "%MOVED_ANY%"=="0" (
    echo Warning: no CAM processor artifacts were found.
)

echo Removing CAM report files...
del /Q "%SOURCE_DIR%\*.gpi" >nul 2>&1
del /Q "%SOURCE_DIR%\*.dri" >nul 2>&1
del /Q "%GERBER_DIR%\*.gpi" >nul 2>&1
del /Q "%GERBER_DIR%\*.dri" >nul 2>&1

if exist "%SOURCE_DIR%\*.gpi" goto :error
if exist "%SOURCE_DIR%\*.dri" goto :error
if exist "%GERBER_DIR%\*.gpi" goto :error
if exist "%GERBER_DIR%\*.dri" goto :error

echo Gerber artifacts are ready in:
echo %GERBER_DIR%
goto :wait

:error
echo.
echo ERROR: Gerber artifact collection did not complete successfully.

:wait
echo.
pause
endlocal
