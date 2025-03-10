#!/bin/bash

slang_ver="2025.5.1"
os_name=$(uname -s)

# Linux: https://github.com/shader-slang/slang/releases/download/v2025.5.1/slang-2025.5.1-linux-x86_64.zip
# Mac: https://github.com/shader-slang/slang/releases/download/v2025.5.1/slang-2025.5.1-macos-aarch64.zip

# Determine OS-specific distribution name
case "$os_name" in
    Linux*)     slang_dist="slang-$slang_ver-linux-x86_64.zip";;
    Darwin*)    slang_dist="slang-$slang_ver-macos-aarch64.zip";;
    *)          echo "Unsupported OS"; exit 1;;
esac

download_url="https://github.com/shader-slang/slang/releases/download/v$slang_ver/$slang_dist"

# Download if doesn't exist
if [ ! -f "slang.zip" ]; then
    echo "Downloading slang..."
    if ! curl -fL -o slang.zip "$download_url"; then
        echo "Download failed"
        rm -f slang.zip  # Cleanup partial download
        exit 1
    fi
fi

# Extract and clean up
if ! unzip -o slang.zip; then
    echo "Extraction failed"
    exit 1
fi
rm -f slang.zip
