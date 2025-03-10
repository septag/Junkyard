#!/bin/bash

version="v0.20"
os_name=$(uname -s)

# https://github.com/septag/meshoptimizer/releases/download/v0.20/meshopt_dist-linux-x86_64.zip
# https://github.com/septag/meshoptimizer/releases/download/v0.20/meshopt_dist-mac-arm64.zip

# Determine OS-specific filename
case "$os_name" in
    Linux*)  dist_file="meshopt_dist-linux-x86_64.zip";;
    Darwin*) dist_file="meshopt_dist-mac-arm64.zip";;
    *)       echo "Unsupported OS"; exit 1;;
esac

download_url="https://github.com/septag/meshoptimizer/releases/download/$version/$dist_file"

# Download with error handling
if [ ! -f "$dist_file" ]; then
    echo "Downloading meshoptimizer..."
    if ! curl -fL -o "$dist_file" "$download_url"; then
        echo "Download failed"
        rm -f "$dist_file" 2>/dev/null
        exit 1
    fi
fi

# Extract archive
echo "Extracting files..."
if ! unzip -o "$dist_file"; then
    echo "Extraction failed"
    exit 1
fi

# Cleanup
rm -f "$dist_file"
