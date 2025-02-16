@echo off

set slang_ver=2025.5.1
set slang_dist=v%slang_ver%/slang-%slang_ver%-windows-x86_64.zip

if not exist slang.zip (
    powershell Invoke-WebRequest -Uri https://github.com/shader-slang/slang/releases/download/%slang_dist% -OutFile slang.zip
)

if %errorlevel% neq 0 goto :End
powershell Expand-Archive -Force -Path slang.zip -DestinationPath .
del slang.zip

:End
exit /b %errorlevel%