#!/usr/bin/env bash
# Build OpenMirrorPhone with the MSYS2 UCRT64 toolchain. Usage: ./build.sh [target]
set -euo pipefail
cd "$(dirname "$0")"
export MSYSTEM=UCRT64
exec /c/msys64/usr/bin/bash -lc "cd '$(pwd -W 2>/dev/null || pwd)' && cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build ${1:+--target $1}"
