@echo off
REM usage: build.cmd {source_file} {configuration}
REM env vars: DEFINES, COMPILE_FLAGS, LINK_FLAGS, LIBS

pushd %~dp0..\..\

set ROOT_DIR=%cd%
if not exist Bin mkdir Bin
if not exist Bin\build_cmd mkdir Bin\build_cmd

set SOURCE_FILE=%1
if not exist %SOURCE_FILE% (
    echo source file not found: %SOURCE_FILE%
    goto Quit
)

set CONFIG=%2
set CONFIG_KNOWN=0

if "%CONFIG%"=="" (
    echo No config is defined, setting to default 'debug'
    set CONFIG=debug
    set CONFIG_KNOWN=1
)

if "%CONFIG%"=="debug" (
    set COMPILE_FLAGS=%COMPILE_FLAGS% /Od /MDd /Zi /RTC1
    set DEFINES=%DEFINES% -D_DEBUG -D_ITERATOR_DEBUG_LEVEL=0
    set LINK_FLAGS=%LINK_FLAGS% /DEBUG:FULL
    set CONFIG_KNOWN=1
)

if "%CONFIG%"=="debugasan" (
    set COMPILE_FLAGS=%COMPILE_FLAGS% /Od /MDd /Zi /fsanitize=address
    set DEFINES=%DEFINES% -D_DEBUG
    set LINK_FLAGS=%LINK_FLAGS% /DEBUG:FULL
    set CONFIG_KNOWN=1
)

if "%CONFIG%"=="releasedev" (
    set COMPILE_FLAGS=%COMPILE_FLAGS% /O2 /Zi /MD 
    set DEFINES=%DEFINES% -D_ITERATOR_DEBUG_LEVEL=0 -DTRACY_ENABLE -DCONFIG_ENABLE_ASSERT=1
    set LINK_FLAGS=%LINK_FLAGS% /DEBUG:FULL
    set CONFIG_KNOWN=1
)

if "%CONFIG%"=="releaseasan" (
    set COMPILE_FLAGS=%COMPILE_FLAGS% /O2 /Zi /MD /fsanitize=address
    set DEFINES=%DEFINES% -D_ITERATOR_DEBUG_LEVEL=0 -DCONFIG_ENABLE_ASSERT=1
    set LINK_FLAGS=%LINK_FLAGS% /DEBUG:FULL
    set CONFIG_KNOWN=1
)

if "%CONFIG%"=="release" (
    set COMPILE_FLAGS=%COMPILE_FLAGS% /O2 /MT
    set DEFINES=%DEFINES% -D_ITERATOR_DEBUG_LEVEL=0 -DCONFIG_FINAL_BUILD=1
    set LIBS=%LIBS%
    set LINK_FLAGS=%LINK_FLAGS%
    set CONFIG_KNOWN=1
)

if %CONFIG_KNOWN% neq 1 (
    echo Invalid config value: %CONFIG%
    set errorlevel=1
    goto Quit
)


set DEFINES=%DEFINES% -D_MBCS -D_CRT_SECURE_NO_WARNINGS -DBUILD_UNITY 
set COMPILE_FLAGS=%COMPILE_FLAGS% /std:c++20 /GR- /EHs-
set LINK_FLAGS=%LINK_FLAGS% /INCREMENTAL:NO
set LIBS=%LIBS% "User32.lib"

pushd Bin\build_cmd
set OUTPUT_DIR=%cd%

cl %DEFINES% ^
   %COMPILE_FLAGS% ^
   "%ROOT_DIR%\%SOURCE_FILE%" ^
   /link %LINK_FLAGS% ^
   /LIBPATH:"%ROOT_DIR%\code\External\slang\lib" ^
   /LIBPATH:"%ROOT_DIR%\code\External\ispc_texcomp\lib\win64" ^
   /LIBPATH:"%ROOT_DIR%\code\External\meshoptimizer\lib\win64" ^
   %LIBS%
popd
if %errorlevel% neq 0 goto Quit

echo No | copy /-Y %ROOT_DIR%\code\External\slang\bin\slang.dll %OUTPUT_DIR%
echo No | copy /-Y %ROOT_DIR%\code\External\slang\bin\slang-glslang.dll %OUTPUT_DIR%
echo No | copy /-Y %ROOT_DIR%\code\External\dbghelp\dbghelp.dll %OUTPUT_DIR%
echo No | copy /-Y %ROOT_DIR%\code\External\ispc_texcomp\lib\win64\ispc_texcomp.dll %OUTPUT_DIR%
echo No | copy /-Y %ROOT_DIR%\code\External\meshoptimizer\lib\win64\meshoptimizer.dll %OUTPUT_DIR%

if %errorlevel%==0 (
    echo Success. Output binaries are in "Bin\build_cmd"
)

:Quit
popd

exit /b %errorlevel%
