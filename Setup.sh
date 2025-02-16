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

exit 0