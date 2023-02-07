@echo off

set slang_dist=v0.24.52/slang-0.24.52-win64.zip

if not exist slang.zip (
    powershell Invoke-WebRequest -Uri https://github.com/shader-slang/slang/releases/download/%slang_dist% -OutFile slang.zip
)

if %errorlevel% neq 0 goto :End
powershell Expand-Archive -Force -Path slang.zip -DestinationPath .
del slang.zip

:End
exit /b %errorlevel%