@echo off

call build-test
if %errorlevel% neq 0 (
    exit /b -1
)

call build-tool
if %errorlevel% neq 0 (
    exit /b -1
)

pushd ..\projects\msvc
msbuild Junkyard.sln -target:Rebuild -Property:Configuration=Debug -verbosity:minimal
if %errorlevel% neq 0 (
    popd
    exit /b -1
)

msbuild Junkyard.sln -target:Rebuild -Property:Configuration=ReleaseDev -verbosity:minimal
if %errorlevel% neq 0 (
    popd
    exit /b -1
)

msbuild Junkyard.sln -target:Rebuild -Property:Configuration=Release -verbosity:minimal
if %errorlevel% neq 0 (
    popd
    exit /b -1
)
popd

pushd ..\projects\msvc\JunkyardAndroid
msbuild JunkyardAndroid.sln -target:Rebuild -Property:Configuration=Debug -verbosity:minimal
if %errorlevel% neq 0 (
    popd
    exit /b -1
)

msbuild JunkyardAndroid.sln -target:Rebuild -Property:Configuration=ReleaseDev -verbosity:minimal
if %errorlevel% neq 0 (
    popd
    exit /b -1
)

msbuild JunkyardAndroid.sln -target:Rebuild -Property:Configuration=Release -verbosity:minimal
if %errorlevel% neq 0 (
    popd
    exit /b -1
)
popd


