@echo off

set LIBS="ispc_texcomp.lib" "slang.lib" "meshoptimizer.lib"
set DEFINES=
set COMPILE_FLAGS=
set LINK_FLAGS=

call build.cmd code\Tests\AssetTest.cpp %1