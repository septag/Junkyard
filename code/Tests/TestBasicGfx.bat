@echo off
setlocal enableextensions enabledelayedexpansion

pushd %~dp0
pushd ..\..\data

if not exist TestBasicGfx mkdir TestBasicGfx

pushd TestBasicGfx
powershell Invoke-WebRequest -Uri "http://septag.dev/files/TestBasicGfx.zip" -OutFile TestBasicGfx.zip
if %errorlevel% neq 0 goto :End

powershell Expand-Archive -Force -Path TestBasicGfx.zip -DestinationPath .\
del TestBasicGfx.zip

python ..\..\..\scripts\Data\make_gltf_metadata.py --recurse-dir .

:End
popd 


popd
popd
endlocal