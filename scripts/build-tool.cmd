@echo off

set LIBS="ispc_texcomp.lib" "slang.lib" "meshoptimizer.lib"
set DEFINES=-DTOOL_MODE=1
set COMPILE_FLAGS=
set LINK_FLAGS=

call build.cmd code\Tool\ToolMain.cpp %1