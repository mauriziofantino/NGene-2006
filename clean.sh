#!/bin/sh
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
if [ "$#" -ne 0 ]; then
    echo 'Usage: sh clean.sh' >&2
    exit 2
fi
if [ -f "$task_root/build/linux/CMakeCache.txt" ]; then
    cmake --build "$task_root/build/linux" --target clean
fi
rm -f -- "$task_root/ngene"
echo 'Clean completed. Configuration, recordings and Windows build preserved.'
