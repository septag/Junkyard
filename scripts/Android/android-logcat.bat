@echo off

rem TODO
set package_name=com.JunkyardAndroid

if not exist %ADB% (
	set ADB=adb
)

echo ADB=%ADB%

title %package_name%

:try_again
%ADB% shell "logcat  --format brief --pid $(ps -ef | grep -E "%package_name%\$" | awk '{print $2}')"
if %errorlevel% neq 0 ( 
    timeout 3 
    goto :try_again
)
