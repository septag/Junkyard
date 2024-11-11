@echo off
setlocal enableextensions enabledelayedexpansion

pushd %~dp0

pushd ..\..\scripts\Data
pip install pillow
python generate_images.py --outputdir ..\..\data\TestAsset --prefix TexBox
popd

pushd ..\..\data\TestAsset

copy /Y ..\..\data\models\HighPolyBox\HighPolyBox.bin .

del "file_list.txt" 2>nul
set count=0
for %%f in (*.tga) do (
    set /a count+=1
    copy /Y ..\..\data\models\HighPolyBox\HighPolyBox.gltf .\Box!count!.gltf
    echo Box!count!.gltf >> file_list.txt
)
popd

popd
endlocal