@echo off
setlocal

rem Package current working-tree contents of Git-tracked mechanical files.
rem Run from any directory. Requires Git and Windows tar; no Python needed.
if not "%~1"=="" (
    echo Usage: %~nx0
    exit /b 2
)
where git.exe >nul 2>&1
if errorlevel 1 (
    echo ERROR: Git was not found on PATH. >&2
    exit /b 1
)
where tar.exe >nul 2>&1
if errorlevel 1 (
    echo ERROR: Windows tar was not found on PATH. >&2
    exit /b 1
)

pushd "%~dp0.." || exit /b 1
set "PACKAGE_TEMP=%TEMP%\hot-wand-mechanical-%RANDOM%-%RANDOM%"
set "PACKAGE_OUTPUT=mechanical\hot-wand-mechanical.zip"
set "RESULT=1"

rem NUL-separated paths preserve spaces and other special characters.
rem Exclude the output too, in case it was ever force-added to Git.
git.exe ls-files --cached --deduplicate -z -- mechanical ":(exclude)mechanical/metcal-grip" ":(exclude,glob)mechanical/**/.gitignore" ":(exclude)mechanical/hot-wand-mechanical.zip" > "%PACKAGE_TEMP%.files"
if errorlevel 1 goto :cleanup
for %%F in ("%PACKAGE_TEMP%.files") do if %%~zF EQU 0 (
    echo ERROR: No tracked mechanical files found. >&2
    goto :cleanup
)

tar.exe --format zip -cf "%PACKAGE_TEMP%.zip" --null -T "%PACKAGE_TEMP%.files"
if errorlevel 1 goto :cleanup

rem Replace the previous package only after the new archive is complete.
move /y "%PACKAGE_TEMP%.zip" "%PACKAGE_OUTPUT%" >nul
if errorlevel 1 goto :cleanup
echo Created %CD%\%PACKAGE_OUTPUT%
set "RESULT=0"

:cleanup
if exist "%PACKAGE_TEMP%.files" del /q "%PACKAGE_TEMP%.files"
if exist "%PACKAGE_TEMP%.zip" del /q "%PACKAGE_TEMP%.zip"
popd
exit /b %RESULT%
