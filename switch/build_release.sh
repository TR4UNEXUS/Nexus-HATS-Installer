#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
cmake --preset Release
cmake --build --preset Release --parallel "${BUILD_JOBS:-4}"
python3 tools/package_release.py
