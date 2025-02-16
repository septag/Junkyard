#!/bin/bash

# Enable error handling and debugging
set -euo pipefail

# Change to the directory where the script is located
cd "$(dirname "$0")"

# Navigate to the data directory
pushd ../../data > /dev/null

# Create the TestBasicGfx directory if it doesn't exist
if [ ! -d "TestBasicGfx" ]; then
    mkdir TestBasicGfx
fi

# Navigate to the TestBasicGfx directory
pushd TestBasicGfx > /dev/null

# Download the TestBasicGfx.zip file using curl
if ! curl -f -O "http://septag.dev/files/TestBasicGfx.zip"; then
    echo "Failed to download TestBasicGfx.zip"
    popd > /dev/null
    popd > /dev/null
    exit 1
fi

# Unzip the TestBasicGfx.zip file
unzip -o TestBasicGfx.zip

# Delete the TestBasicGfx.zip file
rm -f TestBasicGfx.zip

# Return to the previous directories
popd > /dev/null
popd > /dev/null
