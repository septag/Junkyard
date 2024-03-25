@echo off

rem call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

pushd %~dp0..\..\projects\Windows
msbuild Junkyard.sln -property:Configuration=Debug /t:JunkyardTool
popd

if %errorlevel% neq 0 exit /b -1

pushd %~dp0..\..\
bin\Debug\JunkyardTool.exe -ToolingEnableServer=1
popd





