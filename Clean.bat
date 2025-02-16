@echo off

rem Delete copied files to bin folders
del /q Bin\Debug\*.dll
del /q Bin\Release\*.dll
del /q Bin\ReleaseDev\*.dll
del /q Bin\build_cmd\*.dll

rem Delete ini files
del /q *.ini

rem Delete generated files
del /q *.spv 
del /q *.hlsl 
del /q *.glsl 
del /q *.asm 
del /q *.unknown

rem Delete all prebuilt dependencies
if "%1" == "all" ( 
	rmdir /q /s code\External\meshoptimizer\include
	rmdir /q /s code\External\meshoptimizer\lib

	rmdir /q /s code\External\ispc_texcomp\include
	rmdir /q /s code\External\ispc_texcomp\lib

	del /q code\External\slang\LICENSE
	del /q code\External\slang\README.md
	for /d %%D in ("code\External\slang\*") do (
		rmdir /s /q "%%D"
	)
) 



