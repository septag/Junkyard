@echo off

pushd %~dp0
echo Closing existing logcat windows ...
python python/close_logcats.py --package %1
popd

if %errorlevel% neq 0 (
	echo Warining: could not run python helper scripts. Please install python 3+ with following packages:
	echo 	python -m pip install --upgrade pywin32
	set errorlevel=0
)

