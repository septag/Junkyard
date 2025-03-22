#!/bin/bash

pushd "$(dirname "$0")/../../" > /dev/null

if [ ! -d .build ]; then
    mkdir .build
fi

pushd .build > /dev/null

if [ ! -d Ninja ]; then
    mkdir Ninja
fi

pushd Ninja > /dev/null

if [[ "$(uname)" == "Linux" ]]; then
    extra_cmake_opts="-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang"
    echo Make sure you have installed these packages \(debian/ubuntu\):
    echo  pkg-config
    echo  libglfw3-dev
    echo  uuid-dev
    echo  libc++abi-dev
    echo  Install VulkanSDK using Setup.sh
    
    source ../../.downloads/vulkan-sdk/setup-env.sh
else
    extra_cmake_opts=""
fi

cmake ../../projects/CMake -GNinja $extra_cmake_opts $@

popd > /dev/null
popd > /dev/null
popd > /dev/null