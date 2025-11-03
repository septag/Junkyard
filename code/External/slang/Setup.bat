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
  exit /b 1
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

echo [INFO] Slang is out of date. Updating ...
call :Cleanup

set "SLANG_DIST=v%VERSION%/slang-%VERSION%-windows-x86_64.zip"
echo %SLANG_DIST%
if not exist %ROOT%slang.zip (
    powershell Invoke-WebRequest -Uri https://github.com/shader-slang/slang/releases/download/%SLANG_DIST% -OutFile %ROOT%slang.zip
)

if %errorlevel% neq 0 exit /b 1
powershell Expand-Archive -Force -Path %ROOT%slang.zip -DestinationPath %ROOT%

if %errorlevel% equ 0 (
    copy /y "%VERSION_FILE%" "%CUR_FILE%" >nul
)

del /q %ROOT%slang.zip

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
del /q %ROOT%LICENSE 2>nul
del /q %ROOT%README.md 2>nul
for /d %%D in ("%ROOT%*") do (
    rmdir /s /q "%%D" 2>nul
)
