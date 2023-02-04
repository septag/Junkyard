@echo off

pushd %~dp0

set target_name=%1
set output_path=%2
set nm_path=%3\bin\llvm-nm.exe

echo Exporting symbols ..
if not exist symbols mkdir symbols
%nm_path% --demangle %output_path% > symbols\%target_name%.sym_txt
if %errorlevel% neq 0 set errorlevel=0
popd


