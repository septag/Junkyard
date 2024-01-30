@echo off

del /Q *.spv 
del /Q *.hlsl 
del /Q *.glsl 
del /Q *.asm 
del /Q *.unknown

if "%1" == "all" ( 
	rmdir /q /s code\External\meshoptimizer\include
	rmdir /q /s code\External\meshoptimizer\lib

	rmdir /q /s code\External\ispc_texcomp\include
    rmdir /q /s code\External\ispc_texcomp\lib

	del /q code\External\slang\*.h
	rmdir /q /s code\External\slang\bin
	rmdir /q /s code\External\slang\docs
	rmdir /q /s code\External\slang\prelude
) 

