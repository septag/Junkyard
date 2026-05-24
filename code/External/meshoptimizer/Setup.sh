#!/usr/bin/env bash
set -euo pipefail

# === Config ===
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/"
VERSION_FILE="${ROOT}VERSION.TXT"
CUR_FILE="${ROOT}_CURVERSION.TXT"

# === Helper: read and trim a file ===
read_trimmed() {
    local filepath="$1"
    if [[ ! -f "$filepath" ]]; then
        echo ""
        return 1
    fi
    # Read, strip BOM if present, trim leading/trailing whitespace
    sed 's/^\xEF\xBB\xBF//' "$filepath" | tr -d '\r' | awk '{$1=$1};1' | head -n1
}

# === Read version strings ===
VERSION="$(read_trimmed "$VERSION_FILE" || true)"
CUR="$(read_trimmed "$CUR_FILE" || true)"

if [[ -z "$VERSION" ]]; then
    echo "[ERROR] $VERSION_FILE not found or empty. Aborting."
    exit 2
fi

if [[ -n "$CUR" ]]; then
    echo "REQUIRED_VERSION: $VERSION"
    echo "CURRENT_VERSION:  $CUR"
fi

# === Compare and decide ===
if [[ -n "$CUR" && "${CUR,,}" == "${VERSION,,}" ]]; then
    echo "[OK]"
    exit 0
fi

echo "[INFO] meshoptimizer is out of date. Updating ..."

# === Cleanup ===
cleanup() {
    rm -rf "${ROOT}include" 2>/dev/null || true
    rm -rf "${ROOT}lib"     2>/dev/null || true
}
cleanup

# === Download ===
# Determine OS-specific filename
os_name=$(uname -s)
case "$os_name" in
    Linux*)  MESHOPT_DIST="v${VERSION}/meshopt_dist-linux-x86_64.zip";;
    Darwin*) MESHOPT_DIST="v${VERSION}/meshopt_dist-mac-arm64.zip";;
    *)       echo "Unsupported OS"; exit 1;;
esac

ZIPFILE="${ROOT}meshopt.zip"

if [[ ! -f "$ZIPFILE" ]]; then
    curl -fL \
        "https://github.com/septag/meshoptimizer/releases/download/${MESHOPT_DIST}" \
        -o "$ZIPFILE"
fi

# === Extract ===
unzip -o "$ZIPFILE" -d "$ROOT"
cp -f "$VERSION_FILE" "$CUR_FILE"

# === Cleanup zip ===
rm -f "$ZIPFILE"
