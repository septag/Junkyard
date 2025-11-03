@echo off
setlocal EnableExtensions

rem === Config ===
set "ROOT=%~dp0"
set "VERSION_FILE=%ROOT%VERSION.TXT"
set "CUR_FILE=%ROOT%_CURVERSION.TXT"

rem === Read version strings (trimmed) ===
call :ReadTrimmed "%VERSION_FILE%" VERSION
call :ReadTrimmed "%CUR_FILE%" CUR

if not defined VERSION (
  echo [ERROR] %VERSION_FILE% not found or empty. Aborting.
  exit /b 2
)

if defined CUR (
  echo REQUIRED_VERSION: %VERSION%
  echo CURRENT_VERSION: %CUR%
)

rem === Compare and decide ===
if defined CUR if /I "%CUR%"=="%VERSION%" (
  echo [OK]
  exit /b 0
)

echo [INFO] meshoptimizer is out of date. Updating ...
call :Cleanup

set "MESHOPT_DIST=v%VERSION%/MESHOPT_DIST-win64.zip"

if not exist %ROOT%meshopt.zip (
    powershell Invoke-WebRequest -Uri https://github.com/septag/meshoptimizer/releases/download/%MESHOPT_DIST% -OutFile %ROOT%meshopt.zip
)

if %errorlevel% neq 0 exit /b 1
powershell Expand-Archive -Force -Path %ROOT%meshopt.zip -DestinationPath %ROOT%

if %errorlevel% equ 0 (
    copy /y "%VERSION_FILE%" "%CUR_FILE%" >nul
)

del /q %ROOT%meshopt.zip

exit /b %errorlevel%

rem --------------------------------------------------------------------------------------------------------------------
:ReadTrimmed
setlocal
set "p=%~1"
if not exist "%p%" (
  endlocal & set "%~2=" & exit /b 1
)

rem Use PowerShell to read whole file and Trim() whitespace/newlines/BOM
for /f "usebackq delims=" %%A in (`powershell -NoProfile -Command ^
  "try { (Get-Content -Raw -LiteralPath '%p%').Trim() } catch { '' }"`) do (
  set "val=%%A"
)

endlocal & set "%~2=%val%"
exit /b 0

rem --------------------------------------------------------------------------------------------------------------------
:Cleanup
rmdir /q /s %ROOT%include 2>nul
rmdir /q /s %ROOT%lib 2>nul
