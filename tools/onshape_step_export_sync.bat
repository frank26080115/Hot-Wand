@echo off
setlocal

rem ---------------------------------------------------------------------------
rem Configuration
rem ---------------------------------------------------------------------------
set "PYTHON_EXE=python"
set "REPO_ROOT=%~dp0.."
set "ONSHAPE_EXPORTER=%~dp0onshape_step_export.py"
set "BODY_EXPORTER=%~dp0step_body_exporter.py"
set "FACE_EXTRACTOR=%~dp0step_largest_face_extractor.py"
set "MECH_DIFF_CLEANUP=%~dp0mech_git_diff_cleanup.py"
set "TEMP_DIR=mechanical\temp_downloads"

set "MAIN_URL=https://cad.onshape.com/documents/cc6f54d915a2fd2fc0c48bed/w/7a4e9b2004b48c29d2344960/e/7e597e7e1c496a051551f752"
set "ECONO_URL=https://cad.onshape.com/documents/cc6f54d915a2fd2fc0c48bed/w/7a4e9b2004b48c29d2344960/e/e510d1fe4dec97f3745869c8"
set "LITE_URL=https://cad.onshape.com/documents/cc6f54d915a2fd2fc0c48bed/w/7a4e9b2004b48c29d2344960/e/99b40fd88f29385d35df5f1e"
set "STAND_URL=https://cad.onshape.com/documents/cfeb2fa9b83109508c6ff5e8/w/69b4ef0849f116bf37eeaccd/e/969a8f9c953454fe4aa374bb"

set "MAIN_STEP=%TEMP_DIR%\main.step"
set "ECONO_STEP=%TEMP_DIR%\econo.step"
set "LITE_STEP=%TEMP_DIR%\lite.step"
set "STAND_STEP=%TEMP_DIR%\stand.step"

set "SKIP_DOWNLOAD=0"
if /I "%~1"=="--skip-download" (
    set "SKIP_DOWNLOAD=1"
) else if not "%~1"=="" (
    echo Usage: %~nx0 [--skip-download]
    exit /b 2
)

pushd "%REPO_ROOT%" || goto :failure

call :ensure_directory "%TEMP_DIR%"
if errorlevel 1 goto :failure_popd
call :ensure_directory "mechanical\main"
if errorlevel 1 goto :failure_popd
call :ensure_directory "mechanical\econo"
if errorlevel 1 goto :failure_popd
call :ensure_directory "mechanical\lite"
if errorlevel 1 goto :failure_popd
call :ensure_directory "mechanical\stand"
if errorlevel 1 goto :failure_popd

rem ---------------------------------------------------------------------------
rem Main Part Studio
rem Edit or add mappings here as: call :export_body INPUT BODY_NAME OUTPUT
rem ---------------------------------------------------------------------------
call :download_if_needed "%MAIN_URL%" "%MAIN_STEP%"
if errorlevel 1 goto :failure_popd

call :export_body "%MAIN_STEP%" "mold-box" "mechanical\main\box-template.step"
if errorlevel 1 goto :failure_popd
call :export_body "%MAIN_STEP%" "mold-lid" "mechanical\main\lid-template.step"
if errorlevel 1 goto :failure_popd
call :export_body "%MAIN_STEP%" "Heat-Sink-Drill-Template" "mechanical\main\heatsink-drill-template.step"
if errorlevel 1 goto :failure_popd
call :export_body "%MAIN_STEP%" "Heatsink-Exhaust-Duct" "mechanical\main\heatsink-exhaust-duct.step"
if errorlevel 1 goto :failure_popd
call :export_body "%MAIN_STEP%" "Louvered-Air-Intake-Grille" "mechanical\main\air-intake-grille.step"
if errorlevel 1 goto :failure_popd
call :export_body "%MAIN_STEP%" "Internal-Anchor-Plate" "mechanical\main\internal-anchor-plate.step"
if errorlevel 1 goto :failure_popd
call :export_body "%MAIN_STEP%" "Air-Gap-Blocker" "mechanical\main\air-gap-blocker.step"
if errorlevel 1 goto :failure_popd
call :export_body "%MAIN_STEP%" "Face-Plate" "mechanical\main\face-plate.step"
if errorlevel 1 goto :failure_popd
call :export_body "%MAIN_STEP%" "1590T Box__Solid1" "mechanical\main\1590t-enclosure-body.step"
if errorlevel 1 goto :failure_popd
call :export_body "%MAIN_STEP%" "1590T Lid__Solid1" "mechanical\main\1590t-enclosure-lid.step"
if errorlevel 1 goto :failure_popd

rem ---------------------------------------------------------------------------
rem Econo Part Studio
rem ---------------------------------------------------------------------------
call :download_if_needed "%ECONO_URL%" "%ECONO_STEP%"
if errorlevel 1 goto :failure_popd

call :export_body "%ECONO_STEP%" "Box-Body" "mechanical\econo\box-body.step"
if errorlevel 1 goto :failure_popd
call :export_body "%ECONO_STEP%" "Bottom-Lid" "mechanical\econo\bottom-lid.step"
if errorlevel 1 goto :failure_popd
call :export_body "%ECONO_STEP%" "Heat-Sink-Drill-Template" "mechanical\econo\heatsink-drill-template.step"
if errorlevel 1 goto :failure_popd

rem ---------------------------------------------------------------------------
rem Lite Part Studio
rem ---------------------------------------------------------------------------
call :download_if_needed "%LITE_URL%" "%LITE_STEP%"
if errorlevel 1 goto :failure_popd

call :export_body "%LITE_STEP%" "3DP-Box-Body" "mechanical\lite\box-body.step"
if errorlevel 1 goto :failure_popd
call :export_body "%LITE_STEP%" "3DP-Box-Bottom" "mechanical\lite\bottom-lid.step"
if errorlevel 1 goto :failure_popd

rem ---------------------------------------------------------------------------
rem Stand Part Studio
rem ---------------------------------------------------------------------------
call :download_if_needed "%STAND_URL%" "%STAND_STEP%"
if errorlevel 1 goto :failure_popd

call :export_body "%STAND_STEP%" "Strike-Plate" "mechanical\stand\strike-plate.step"
if errorlevel 1 goto :failure_popd
call :extract_largest_face "mechanical\stand\strike-plate.step" "mechanical\stand\strike-plate.dxf"
if errorlevel 1 goto :failure_popd
call :export_body "%STAND_STEP%" "Handle-Rest" "mechanical\stand\handle-rest.step"
if errorlevel 1 goto :failure_popd
call :export_body "%STAND_STEP%" "Tip-Shroud" "mechanical\stand\tip-shroud.step"
if errorlevel 1 goto :failure_popd
call :export_body "%STAND_STEP%" "Foot-Right" "mechanical\stand\foot-right.step"
if errorlevel 1 goto :failure_popd
call :export_body "%STAND_STEP%" "Foot-Left" "mechanical\stand\foot-left.step"
if errorlevel 1 goto :failure_popd

echo.
echo Cleaning metadata-only mechanical export changes
"%PYTHON_EXE%" -B "%MECH_DIFF_CLEANUP%"
if errorlevel 1 goto :failure_popd

echo.
echo Onshape STEP sync completed successfully.
popd
exit /b 0

:download
echo.
echo Downloading %~1
"%PYTHON_EXE%" "%ONSHAPE_EXPORTER%" "%~1" "%~2"
exit /b %errorlevel%

:download_if_needed
if "%SKIP_DOWNLOAD%"=="1" (
    if not exist "%~2" (
        echo ERROR: Cannot skip download because "%~2" does not exist.
        exit /b 1
    )
    echo.
    echo Using existing download "%~2"
    exit /b 0
)
call :download "%~1" "%~2"
exit /b %errorlevel%

:export_body
echo Exporting body "%~2" to "%~3"
"%PYTHON_EXE%" "%BODY_EXPORTER%" "%~1" "%~2" "%~3" --force
exit /b %errorlevel%

:extract_largest_face
echo Extracting largest face from "%~1" to "%~2"
"%PYTHON_EXE%" "%FACE_EXTRACTOR%" "%~1" "%~2" --force
exit /b %errorlevel%

:ensure_directory
if not exist "%~1" mkdir "%~1"
exit /b %errorlevel%

:failure_popd
popd

:failure
echo.
echo ERROR: Onshape STEP sync failed.
exit /b 1
