@echo off

rem This script depends on cloc utility to be on PATH (https://github.com/AlDanial/cloc)

pushd %~dp0

pushd ..\..\code

echo WITHOUT EXTERNALS:
cloc --include-ext=cpp,c,h,hpp,inl . --exclude-dir=External --by-percent=c
echo.

echo WITH EXTERNALS:
cloc --include-ext=cpp,c,h,hpp,inl . --exclude-dir=vulkan --by-percent=c
echo.

popd
popd
