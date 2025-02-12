@echo off

pushd %~dp0\..\..\

if not exist .build mkdir .build
pushd .build

if not exist MSVC mkdir MSVC
pushd MSVC

echo %PATH%
cmake ..\..\projects\CMake %*

popd
popd
popd

