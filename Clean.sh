#!/bin/bash

# Delete copied files in bin folders
rm -f Bin/Debug/*.dll
rm -f Bin/Release/*.dll
rm -f Bin/ReleaseDev/*.dll
rm -f Bin/build_cmd/*.dll

# Delete ini files
rm -f *.ini

# Delete generated files
rm -f *.spv
rm -f *.hlsl
rm -f *.glsl
rm -f *.asm
rm -f *.unknown

# Delete all prebuilt dependencies if "all" argument is provided
if [ "$1" == "all" ]; then
    rm -rf code/External/meshoptimizer/include
    rm -rf code/External/meshoptimizer/lib

    rm -rf code/External/ispc_texcomp/include
    rm -rf code/External/ispc_texcomp/lib

	rm -rf code/External/slang/LICENSE
	rm -rf code/External/slang/README.md
    find code/External/slang -mindepth 1 -maxdepth 1 -type d -exec rm -rf {} +
fi
