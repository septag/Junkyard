@echo off

set meshopt_dist=v0.18/meshopt_dist-win64.zip

if not exist meshopt_dist.zip (
    powershell Invoke-WebRequest -Uri https://github.com/septag/meshoptimizer/releases/download/%meshopt_dist% -OutFile meshopt_dist.zip
)

if %errorlevel% neq 0 goto :End
powershell Expand-Archive -Force -Path meshopt_dist.zip -DestinationPath .
del meshopt_dist.zip

:End
exit /b %errorlevel%