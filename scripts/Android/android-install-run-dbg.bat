@echo off

if not exist %ADB% (
	set ADB=adb
)

set outdir=%~dp0..\..\bin\android\JunkyardAndroid\Debug

pushd %outdir%
%ADB% install JunkyardAndroid.apk
if %errorlevel% neq 0 (
	exit /b 1
)

%ADB% shell am start com.JunkyardAndroid/android.app.NativeActivity
popd
