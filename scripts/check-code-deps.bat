@echo off

pushd %~dp0
python python/check_code_deps.py --codedir ..\code
popd
