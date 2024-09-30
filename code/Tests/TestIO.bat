@echo off

pushd %~dp0

pushd ..\..\scripts\Data
pip install pillow
python generate_images.py --outputdir ..\..\data\TestIO
popd

pushd ..\..\data\TestIO
del "file_list.txt" 2>nul
for %%f in (*.tga) do (
    echo %%f >> file_list.txt
)
popd

popd