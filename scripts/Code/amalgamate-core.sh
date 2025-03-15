#!/bin/bash

# Change to the script's directory
pushd "$(dirname "$(realpath "$0")")" > /dev/null

# Run the Python script with the given arguments
python amalgamate-core.py --rootdir ../../code/Core --outputname Core --ignore-comment-lines --verbose --outputdir "$1"

# Return to the previous directory
popd > /dev/null

