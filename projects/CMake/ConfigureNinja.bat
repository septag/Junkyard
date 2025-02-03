@echo off

pushd %~dp0\..\..\

if not exist .build mkdir .build
pushd .build

if not exist Ninja mkdir Ninja
pushd Ninja

echo %PATH%
cmake ..\..\projects\CMake -GNinja %*

popd
popd
popd

