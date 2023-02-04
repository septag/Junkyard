@echo off

rem https://developer.android.com/ndk/guides/graphics/validation-layer#enable-layers-outside-app

if not exist %ADB% (
	set ADB=adb
)

set package_name=com.JunkyardAndroid
set debuglayer_binary=%~dp0\..\code\External\vulkan\bin\android\arm64-v8a\libVkLayer_khronos_validation.so

adb push %debuglayer_binary% /data/local/tmp
if %errorlevel% neq 0 exit /b 1
adb shell run-as %package_name% cp /data/local/tmp/libVkLayer_khronos_validation.so .
if %errorlevel% neq 0 exit /b 1
adb shell run-as %package_name% ls libVkLayer_khronos_validation.so
if %errorlevel% neq 0 exit /b 1

adb shell settings put global enable_gpu_debug_layers 1
if %errorlevel% neq 0 exit /b 1
adb shell settings put global gpu_debug_app %package_name%
if %errorlevel% neq 0 exit /b 1
adb shell settings put global gpu_debug_layers VK_LAYER_KHRONOS_validation
if %errorlevel% neq 0 exit /b 1
adb shell settings put global gpu_debug_layer_app %package_name%
if %errorlevel% neq 0 exit /b 1

echo Validation layer activated.