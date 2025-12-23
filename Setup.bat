@echo off
setlocal EnableExtensions enableDelayedExpansion

pushd %~dp0

if not exist .downloads mkdir .downloads

set vars_ini=vars.ini
if not exist %vars_ini% (
    echo # Add command-aliases here > %vars_ini%
)

:InstallCodeDeps
del /q Bin\Debug\*.dll 2>nul
del /q Bin\Release\*.dll 2>nul
del /q Bin\ReleaseDev\*.dll 2>nul
del /q Bin\build_cmd\*.dll 2>nul

:InstallSlang
set slang_dir=code\External\slang
echo Checking slang shader compiler: %slang_dir% ...
pushd %slang_dir%
call Setup.bat
popd
if %errorlevel% neq 0 goto :End

:InstallISPC
set ispc_dir=code\External\ispc_texcomp
echo Checking ISPCTextureCompressor: %ispc_dir%
pushd %ispc_dir%
call Setup.bat
popd
if %errorlevel% neq 0 goto :End

:InstallMeshOpt
set meshopt_dir=code\External\meshoptimizer
echo Checking meshoptimizer: %meshopt_dir%
pushd %meshopt_dir%
call Setup.bat
popd
if %errorlevel% neq 0 goto :End

:ExtractExampleAssets
choice /c YN /M "Extract assets for basic graphics examples "
if errorlevel 2 goto :InstallTracyClient
if errorlevel 1 (
    powershell Expand-Archive -Force -Path data\TestBasicGfx.zip -DestinationPath data\
)

:InstallTracyClient
choice /c YN /M "Download tracy profiler v0.11.1 (Optional. Used for profiling) "
if errorlevel 2 goto :InstallLivePP
if errorlevel 1 (
    if not exist .downloads\Tracy-0.11.zip (
        powershell Invoke-WebRequest -Uri "https://github.com/wolfpld/tracy/releases/download/v0.11.1/windows-0.11.1.zip" -OutFile .downloads\Tracy-0.11.1.zip
        if %errorlevel% neq 1 goto :End
    )

    if not exist .downloads\Tracy-0.11.1 mkdir .downloads\Tracy-0.11.1
    powershell Expand-Archive -Force -Path .downloads\Tracy-0.11.1.zip -DestinationPath .downloads\Tracy-0.11.1
    del /F .downloads\Tracy-0.11.1.zip

    powershell Invoke-WebRequest -Uri "https://github.com/wolfpld/tracy/releases/download/v0.11.1/tracy.pdf" -OutFile .downloads\Tracy-0.11.1\tracy.pdf

    echo Tracy client saved to '.downloads\Tracy-0.11.1'
)

:InstallLivePP
choice /c YN /M "Download Live++ package v2.8.1 (Optional. Needs license for the broker) "
if errorlevel 2 goto :InstallVulkan
if errorlevel 1 (
    if not exist .downloads\LPP_2_8_1.zip (
        powershell Invoke-WebRequest -Uri "https://liveplusplus.tech/downloads/LPP_2_8_1.zip" -OutFile .downloads\LPP_2_8_1.zip
        if %errorlevel% neq 1 goto :End
    )
    powershell Expand-Archive -Force -Path .downloads\LPP_2_8_1.zip -DestinationPath .downloads
    echo Live++ package saved to '.downloads\LivePP'
)

if not exist code\External\LivePP mkdir code\External\LivePP
xcopy .downloads\LivePP\Agent code\External\LivePP\Agent /E /I /Y
xcopy .downloads\LivePP\API code\External\LivePP\API /E /I /Y

:InstallVulkan
set VULKAN_SDK_VERSION=1.4.328.1
choice /c YN /M "Install vulkan %VULKAN_SDK_VERSION% "
if errorlevel 2 goto :InstallPython 

set vulkanrt_filename=VulkanRT-X64-%VULKAN_SDK_VERSION%-Installer.exe
set vulkansdk_filename=vulkansdk-windows-X64-%VULKAN_SDK_VERSION%.exe
if errorlevel 1 (
    rem pc

    echo Installing Vulkan PC SDK runtime...
    if not exist .downloads\%vulkanrt_filename% (
        powershell Invoke-WebRequest -Uri "https://sdk.lunarg.com/sdk/download/%VULKAN_SDK_VERSION%/windows/%vulkanrt_filename%" -OutFile .downloads\%vulkanrt_filename%
        if %errorlevel% neq 1 goto :End
    )

    call .downloads\%vulkanrt_filename% 

    echo Installing Vulkan PC SDK tools and layers...
    if not exist .downloads\%vulkansdk_filename% (
        powershell Invoke-WebRequest -Uri "https://sdk.lunarg.com/sdk/download/%VULKAN_SDK_VERSION%/windows/%vulkansdk_filename%" -OutFile .downloads\%vulkansdk_filename%
        if %errorlevel% neq 1 goto :End
    )

    call .downloads\%vulkansdk_filename%
)

:InstallPython
choice /c YN /M "Install python 3.11 (Optional. if you want to run scripts) "
if errorlevel 2 goto :SetupAndroid
if errorlevel 1 (
    if not exist .downloads\python-3.11.1-amd64.exe (
        powershell Invoke-WebRequest -Uri "https://www.python.org/ftp/python/3.11.1/python-3.11.1-amd64.exe" -OutFile .downloads\python-3.11.1-amd64.exe
        if %errorlevel% neq 1 goto :End
    )

    .downloads\python-3.11.1-amd64.exe
    
    python.exe -m pip install --upgrade pip
    if %errorlevel% neq 1 goto :End
)

choice /c YN /M "Install pywin32 (Optional. used in some scripts) "
if errorlevel 2 goto :End
if errorlevel 1 (
    python -m pip install --upgrade pywin32
    if %errorlevel% neq 1 goto :End
)

:End
popd
endlocal
