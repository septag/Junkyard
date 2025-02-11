@echo off
setlocal EnableExtensions enableDelayedExpansion

pushd %~dp0

if not exist .downloads mkdir .downloads

set vars_ini=vars.ini
if not exist %vars_ini% (
    echo # Add command-aliases here > %vars_ini%
)

:InstallCodeDeps
choice /c YN /M "Install code dependencies"
if errorlevel 2 goto :InstallTracyClient
if errorlevel 1 goto :InstallCodeDepsStart

:InstallCodeDepsStart
:InstallSlang
set slang_dir=code\External\slang
echo Installing slang into %slang_dir% ...
pushd %slang_dir%
call Setup.bat
popd
if %errorlevel% neq 0 goto :End

:InstallISPC
set ispc_dir=code\External\ispc_texcomp
echo Installing ISPC Texture compressor into %ispc_dir%
pushd %ispc_dir%
call Setup.bat
popd
if %errorlevel% neq 0 goto :End

:InstallMeshOpt
set meshopt_dir=code\External\meshoptimizer
echo Installing meshoptimizer into %meshopt_dir%
pushd %meshopt_dir%
call Setup.bat
popd
if %errorlevel% neq 0 goto :End

:InstallTracyClient
choice /c YN /M "Download tracy profiler v0.10 "
if errorlevel 2 goto :InstallLivePP
if errorlevel 1 (
    if not exist .downloads\Tracy-0.10.7z (
        powershell Invoke-WebRequest -Uri "https://github.com/wolfpld/tracy/releases/download/v0.10/Tracy-0.10.7z" -OutFile .downloads\Tracy-0.10.7z
        if %errorlevel% neq 1 goto :End
    )
    powershell Invoke-WebRequest -Uri "https://github.com/wolfpld/tracy/releases/download/v0.10/tracy.pdf" -OutFile .downloads\tracy.pdf
    echo Tracy client saved to '.downloads\Tracy-0.10.7z'
    call .downloads\Tracy-0.10.7z
)

:InstallLivePP
choice /c YN /M "Download Live++ package v2.8.1 (Needs license for the broker)"
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
choice /c YN /M "Install vulkan 1.3.296 "
if errorlevel 2 goto :InstallPython 

set vulkanrt_filename=VulkanRT-1.3.296.0-Installer.exe
set vulkansdk_filename=VulkanSDK-1.3.296.0-Installer.exe
if errorlevel 1 (
    rem make sure to update the vulkan android layers (below) as well
    rem pc
    echo Installing Vulkan PC SDK runtime...
    if not exist .downloads\%vulkanrt_filename% (
        powershell Invoke-WebRequest -Uri "https://sdk.lunarg.com/sdk/download/1.3.296.0/windows/%vulkanrt_filename%" -OutFile .downloads\%vulkanrt_filename%
        if %errorlevel% neq 1 goto :End
    )

    call .downloads\%vulkanrt_filename% 

    echo Installing Vulkan PC SDK tools and layers...
    if not exist .downloads\%vulkansdk_filename% (
        powershell Invoke-WebRequest -Uri "https://sdk.lunarg.com/sdk/download/1.3.296.0/windows/%vulkansdk_filename%  " -OutFile .downloads\%vulkansdk_filename%
        if %errorlevel% neq 1 goto :End
    )

    call .downloads\%vulkansdk_filename%
)

:InstallPython
choice /c YN /M "Install python 3.11 "
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

choice /c YN /M "Install pywin32 (Not required, but useful for tools) "
if errorlevel 2 goto :SetupAndroid
if errorlevel 1 (
    python -m pip install --upgrade pywin32
    if %errorlevel% neq 1 goto :End
)

:SetupAndroid
choice /c YN /M "Setup android stuff "
if errorlevel 2 goto :End
if errorlevel 1 goto :AndroidSettingsIni

:AndroidSettingsIni
choice /c YN /M "Create android app Settings.ini "
if errorlevel 2 goto :AndroidLayers
if errorlevel 1 goto :AndroidSettingsIniStart

:AndroidSettingsIniStart
set android_config_ini=projects\Android\JunkyardAndroid\app\src\main\assets\Settings.ini
if exist %android_config_ini% del /F %android_config_ini%
echo Initializing android app config '%android_config_ini% ...

echo # See Settings.ini.template for more details > %android_config_ini%
echo. >> %android_config_ini%
echo [Engine] >> %android_config_ini%
echo connectToServer = true >> %android_config_ini%

set ip_address_string="IPv4 Address"
rem Uncomment the following line when using older versions of Windows without IPv6 support (by removing "rem")
rem set ip_address_string="IP Address"
for /f "usebackq tokens=2 delims=:" %%f in (`ipconfig ^| findstr /c:%ip_address_string%`) do (
    echo remoteServicesUrl = %%f:6006 >> %android_config_ini%
    echo Found IP Address: %%f 
    powershell Invoke-Item %android_config_ini%

    goto :AndroidLayers
)

:AndroidLayers
choice /c YN /M "Install vulkan android layers "
if errorlevel 2 goto :AndroidScrCpy
if errorlevel 1 goto :AndroidLayersStart

:AndroidLayersStart
echo Installing Vulkan Android layers ...
if not exist .downloads\android-binaries-1.3.296.0.zip (
    powershell Invoke-WebRequest -Uri "https://github.com/KhronosGroup/Vulkan-ValidationLayers/releases/download/vulkan-sdk-1.3.296.0/android-binaries-1.3.296.0.zip" -OutFile .downloads\android-binaries-1.3.231.1.zip
    if %errorlevel% neq 1 goto :End
)

powershell Expand-Archive -Force -Path .downloads\android-binaries-1.3.296.0.zip -DestinationPath code\External\vulkan\bin\android
if %errorlevel% neq 1 goto :End

:AndroidScrCpy
choice /c YN /M "Download ScrCpy (for remote android viewing) "
if errorlevel 2 goto :Gnirehtet
if errorlevel 1 goto :AndroidScrCpyStart

:AndroidScrCpyStart
if not exist .downloads\scrcpy-win64-v1.24.zip (
    powershell Invoke-WebRequest -Uri "https://github.com/Genymobile/scrcpy/releases/download/v1.24/scrcpy-win64-v1.24.zip" -OutFile .downloads\scrcpy-win64-v1.24.zip
    if %errorlevel% neq 1 goto :End
)
powershell Expand-Archive -Force -Path .downloads\scrcpy-win64-v1.24.zip -DestinationPath .downloads\scrcpy
echo ScrCpy installed to: %cd%\downloads\scrcpy
echo ScrCpy = .downloads\scrcpy\scrcpy.exe >> %vars_ini%
powershell Invoke-Item %vars_ini%

:Gnirehtet
choice /c YN /M "Download Gnirehtet (for reverse usb tethering) "
if errorlevel 2 goto :End
if errorlevel 1 goto :GnirehtetStart

:GnirehtetStart
if not exist .downloads\gnirehtet-rust-win64-v2.5.zip (
    powershell Invoke-WebRequest -Uri "https://github.com/Genymobile/gnirehtet/releases/download/v2.5/gnirehtet-rust-win64-v2.5.zip" -OutFile .downloads\gnirehtet-rust-win64-v2.5.zip
    if %errorlevel% neq 1 goto :End
)

powershell Expand-Archive -Force -Path .downloads\gnirehtet-rust-win64-v2.5.zip -DestinationPath .downloads
echo gnirehtet installed to: %cd%\downloads\gnirehtet-rust-win64
echo Run 'scripts/android-rtether.bat' to do reverse tether, then remove the 'remoteServicesUrl' field from Settings.ini
:End
popd
endlocal
