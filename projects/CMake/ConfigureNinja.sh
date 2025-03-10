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

echo "$PATH"

cmake ../../projects/CMake -GNinja "$@"

popd > /dev/null
popd > /dev/null
popd > /dev/null