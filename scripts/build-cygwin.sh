#!/usr/bin/env bash
# Build the Windows POSIX preservation target using Cygwin tools exclusively.
set -eu
export PATH=/usr/bin:/bin
task_source=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd -- "$task_source"
cmake -S . -B build/windows-cygwin -G 'Unix Makefiles' \
    -DCMAKE_BUILD_TYPE=Debug -DNGENE_BUILD_PLATFORM_CHECKS=ON
cmake --build build/windows-cygwin --parallel 2
if [ "${1:-}" = '--install' ]; then
    cmake --install build/windows-cygwin --prefix "$HOME/.ngene"
fi
