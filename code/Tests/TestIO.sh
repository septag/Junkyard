#!/bin/bash

# Enable error handling and debugging
set -euo pipefail

# Change to the directory where the script is located
cd "$(dirname "$0")"

# Navigate to the Data directory and run Python commands
pushd ../../scripts/Data > /dev/null
pip install pillow
python generate_images.py --outputdir ../../data/TestIO
popd > /dev/null

# Navigate to the TestIO directory
pushd ../../data/TestIO > /dev/null

# Delete the file_list.txt if it exists
rm -f file_list.txt

# Loop through all .tga files in the directory and append their names to file_list.txt
for file in *.tga; do
    echo "$file" >> file_list.txt
done

# Return to the original directory
popd > /dev/null
