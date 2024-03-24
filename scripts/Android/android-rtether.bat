@echo off

pushd %~dp0..\..

if not exist .downloads/gnirehtet-rust-win64/gnirehtet-run.cmd (
    echo Error: gnirehtet not installed in '.downloads/gnirehtet-rust-win64', run Setup.bat
    exit /b -1
)

%ADB% reverse tcp:6006 tcp:6006

pushd ".downloads/gnirehtet-rust-win64"
call gnirehtet-run.cmd
popd
if %errorlevel% neq 0 exit /b -1

popd
