@echo off

rem Delete copied files to bin folders
del /q Bin\Debug\*.dll
del /q Bin\Release\*.dll
del /q Bin\ReleaseDev\*.dll
del /q Bin\build_cmd\*.dll

rem Delete ini and text files
del /q *.ini
del /q *.txt

rem Delete generated files
del /q *.spv 
del /q *.spv-asm
del /q *.hlsl 
del /q *.glsl 
del /q *.asm 
del /q *.unknown

