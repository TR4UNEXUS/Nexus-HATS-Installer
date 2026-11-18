#!/bin/sh

# Build the self-contained NRO (includes its repair payload; no ZIP).
# Use this for fast development iteration

# builds a preset
build_preset() {
    echo Configuring $1 ...
    cmake --preset $1
    echo Building $1 ...
    cmake --build --preset $1
}

echo "=== Building Nexus HATS Installer NRO only ==="
build_preset Release

echo "=== NRO built: build/Release/nexus-hats-installer.nro ==="
