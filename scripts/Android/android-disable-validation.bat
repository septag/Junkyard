rem https://developer.android.com/ndk/guides/graphics/validation-layer#enable-layers-outside-app

@echo off

if not exist %ADB% (
	set ADB=adb
)

%ADB% shell settings delete global enable_gpu_debug_layers
if %errorlevel% neq 0 exit /b 1

%ADB% shell settings delete global gpu_debug_app
if %errorlevel% neq 0 exit /b 1

%ADB% shell settings delete global gpu_debug_layers
if %errorlevel% neq 0 exit /b 1

%ADB% shell settings delete global gpu_debug_layer_app
if %errorlevel% neq 0 exit /b 1

echo Validation layer deactivated.