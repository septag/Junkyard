#!/bin/bash

# Enable error handling and change to the script's directory
set -e
cd "$(dirname "$0")"

# Create .downloads directory if it doesn't exist
if [ ! -d ".downloads" ]; then
    mkdir .downloads
fi

# Check if vars.ini exists, create it if it doesn't
vars_ini="vars.ini"
if [ ! -f "$vars_ini" ]; then
    echo "# Add command-aliases here" > "$vars_ini"
fi

# Install Ubuntu packages
prompt_choice() {
    read -p "Install required system packages/libs? (Y/N): " choice
    case "$choice" in
        Y|y) return 0 ;;
        N|n) return 1 ;;
        *) echo "Invalid choice. Please enter Y or N." ; prompt_choice ;;
    esac
}

if prompt_choice; then
    # Check if apt package manager exists (Ubuntu-based system)
    if command -v apt &> /dev/null; then
        echo "Ubuntu package manager detected. Installing required packages ..."
        sudo apt install pkg-config libglfw3-dev uuid-dev clang libc++-dev libc++abi-dev cmake
    else
        echo "Ubuntu/apt package manager is not detected."
        echo "Skipping package installation."
        echo "Make sure you have these packages installed for your distro:"
        echo "  clang"
        echo "  cmake"
        echo "  pkg-config"
        echo "  libglfw3-dev"
        echo "  uuid-dev"
        echo "  libc++-dev"
        echo "  libc++abi-dev"
    fi    
fi

# Function to prompt user for choice
prompt_choice() {
    read -p "Install code dependencies? (Y/N): " choice
    case "$choice" in
        Y|y) return 0 ;;
        N|n) return 1 ;;
        *) echo "Invalid choice. Please enter Y or N." ; prompt_choice ;;
    esac
}

# Install code dependencies if user chooses to
if prompt_choice; then
    # Install Slang
    slang_dir="code/External/slang"
    echo "Installing slang into $slang_dir ..."
    pushd "$slang_dir" > /dev/null
    ./Setup.sh
    popd > /dev/null
    if [ $? -ne 0 ]; then
        exit $?
    fi

    # Install ISPC Texture Compressor
    ispc_dir="code/External/ispc_texcomp"
    echo "Installing ISPC Texture Compressor into $ispc_dir ..."
    pushd "$ispc_dir" > /dev/null
    ./Setup.sh
    popd > /dev/null
    if [ $? -ne 0 ]; then
        exit $?
    fi

    # Install MeshOptimizer
    meshopt_dir="code/External/meshoptimizer"
    echo "Installing meshoptimizer into $meshopt_dir ..."
    pushd "$meshopt_dir" > /dev/null
    ./Setup.sh
    popd > /dev/null
    if [ $? -ne 0 ]; then
        exit $?
    fi
fi

# Install vulkan sdk
vulkan_sdk_version="1.3.296.0"

prompt_choice() {
    read -p "Download Vulkan SDK $vulkan_sdk_version? (Y/N): " choice
    case "$choice" in
        Y|y) return 0 ;;
        N|n) return 1 ;;
        *) echo "Invalid choice. Please enter Y or N." ; prompt_choice ;;
    esac
}

if prompt_choice; then
    vulkan_dir=".downloads/vulkan-$vulkan_sdk_version"
    vulkan_url="https://sdk.lunarg.com/sdk/download/$vulkan_sdk_version/linux/vulkansdk-linux-x86_64-$vulkan_sdk_version.tar.xz"
    dist_file=".downloads/vulkan-sdk.tar.xz"

    mkdir -p $vulkan_dir
    if ! curl -fL -o "$dist_file" "$vulkan_url"; then
        echo "Download failed"
        rm -f "$dist_file" 2>/dev/null
        exit 1
    fi

    # Extract archive
    if ! tar xvf "$dist_file" -C "$vulkan_dir" --strip-components=1; then
        echo "Extraction failed"
        exit 1
    fi

    rm -f "$dist_file"

    echo Modifying .vscode/settings.json
    sed -i "s|\(/.downloads/\)[^/]*\(/setup-env.sh\)|\1vulkan-$vulkan_sdk_version\2|g" .vscode/settings.json
    sed -i "s|\(/.downloads/\)[^/]*\(/x86_64\)|\1vulkan-${vulkan_sdk_version}\2|g" .vscode/settings.json

    echo Modifying .vscode/launch.json
    sed -i "s|\(/.downloads/\)[^/]*\(/x86_64\)|\1vulkan-${vulkan_sdk_version}\2|g" .vscode/launch.json

    echo Installed vulkan-sdk into $vulkan_dir
fi

# Download tracy
prompt_choice() {
    read -p "Download tracy profiler 0.11.1? (Y/N): " choice
    case "$choice" in
        Y|y) return 0 ;;
        N|n) return 1 ;;
        *) echo "Invalid choice. Please enter Y or N." ; prompt_choice ;;
    esac
}

if prompt_choice; then
    tracy_dir=".downloads/Tracy-0.11.1"
    download_url="https://github.com/septag/tracy/releases/download/v0.11.1/tracy-profiler-linux-x86_64.zip"
    dist_file="$tracy_dir/Tracy-0.11.1.zip"

    mkdir -p $tracy_dir
    if ! curl -fL -o "$dist_file" "$download_url"; then
        echo "Download failed"
        rm -f "$dist_file" 2>/dev/null
        exit 1
    fi

    # Extract archive
    if ! unzip -o "$dist_file" -d "$tracy_dir"; then
        echo "Extraction failed"
        exit 1
    fi

    rm -f "$dist_file"

    curl -fL -o "$tracy_dir/tracy.pdf" "https://github.com/wolfpld/tracy/releases/download/v0.11.1/tracy.pdf"

    echo Downloaded tracy into $tracy_dir
fi


exit 0