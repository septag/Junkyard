@echo off

set ispc_dist=02.2023/ispc_dist-win64.zip

if not exist ispc_dist.zip (
    powershell Invoke-WebRequest -Uri https://github.com/septag/ISPCTextureCompressor/releases/download/%ispc_dist% -OutFile ispc_dist.zip
)

if %errorlevel% neq 0 goto :End
powershell Expand-Archive -Force -Path ispc_dist.zip -DestinationPath .
del ispc_dist.zip

:End
exit /b %errorlevel%