@echo off

if not exist %ADB% (
	set ADB=adb
)

set drivers_dir=%~dp0..\tools\android-drivers\
pushd %drivers_dir%

for /f %%i in ('%ADB% shell "cat /proc/cpuinfo | grep Hardware | tail -c 5"') do set device_id=%%i
if "%device_id%"=="KONA" (
	echo Device is KONA
	set driver_name=com.qualcomm.qti.gpudrivers.kona.api30.signed.apk
)

if "%driver_name%"=="" (
	echo Device is not recognized
	exit /b -1
)

echo Installing driver: %driver_name%
%ADB% install -r -d -t %driver_name%

popd
