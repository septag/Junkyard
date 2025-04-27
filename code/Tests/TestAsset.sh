#!/bin/bash

# Enable error handling and debugging
set -euo pipefail

# Change to the directory where the script is located
cd "$(dirname "$0")"

# Navigate to the Data directory and run Python commands
pushd ../../scripts/Data > /dev/null
pip install pillow
python generate_images.py --outputdir ../../data/TestAsset --prefix TexBox
popd > /dev/null

# Navigate to the TestAsset directory
pushd ../../data/TestAsset > /dev/null

# Copy the HighPolyBox.bin file
cp -f ../../data/models/HighPolyBox/HighPolyBox.bin .

# Delete the file_list.txt if it exists
rm -f file_list.txt

# Initialize a counter
count=0

# Loop through all .tga files in the directory
for file in *.tga; do
    # Increment the counter
    count=$((count + 1))

    # Copy the HighPolyBox.gltf file and rename it
    cp -f ../../data/models/HighPolyBox/HighPolyBox.gltf "Box${count}.gltf"

    # Append the new gltf file name to file_list.txt
    echo "Box${count}.gltf" >> file_list.txt
done

# Return to the original directory
popd > /dev/null
