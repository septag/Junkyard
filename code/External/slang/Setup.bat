@echo off

set slang_dist=v2024.1.6/slang-2024.1.6-win64.zip

if not exist slang.zip (
    powershell Invoke-WebRequest -Uri https://github.com/shader-slang/slang/releases/download/%slang_dist% -OutFile slang.zip
)

if %errorlevel% neq 0 goto :End
powershell Expand-Archive -Force -Path slang.zip -DestinationPath .
del slang.zip

:End
exit /b %errorlevel%