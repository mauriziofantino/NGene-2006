#!/bin/sh
# Native Linux build; configuration and data live beside the delivered binary.
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
task_build="$task_root/build/linux"

if [ "$(uname -s)" != Linux ]; then
    echo 'This script builds on Linux. On Windows use build.cmd.' >&2
    exit 1
fi
case "${1:-}" in
    '') task_clean= ;;
    clean) task_clean=--clean-first ;;
    *) echo 'Usage: sh build.sh [clean]' >&2; exit 2 ;;
esac
if [ "$#" -gt 1 ]; then
    echo 'Usage: sh build.sh [clean]' >&2
    exit 2
fi
for task_tool in cmake ctest pkg-config; do
    if ! command -v "$task_tool" >/dev/null 2>&1; then
        echo "Missing $task_tool. Install the packages listed in docs/LINUX.md." >&2
        exit 1
    fi
done
cmake -S "$task_root" -B "$task_build" -DCMAKE_BUILD_TYPE=Release \
    -DNGENE_BUILD_PLATFORM_CHECKS=ON -DNGENE_RX_PROFILING=OFF
cmake --build "$task_build" --parallel "${NGENE_BUILD_JOBS:-8}" ${task_clean:+"$task_clean"}
ctest --test-dir "$task_build" --output-on-failure

# Copy missing templates only. Existing user settings and data are preserved.
for task_folder in configuration otherData; do
    task_origin="$task_root/sampleConfigurationFiles/$task_folder"
    task_destination="$task_root/$task_folder"
    mkdir -p -- "$task_destination"
    (cd -- "$task_origin" && find . -type f -exec sh -c '
        origin=$1; destination=$2; shift 2
        for relative do
            target="$destination/$relative"
            if [ ! -e "$target" ]; then
                mkdir -p -- "$(dirname -- "$target")"
                cp -- "$origin/$relative" "$target"
            fi
        done
    ' sh "$task_origin" "$task_destination" {} +)
done
cp -- "$task_build/ngene" "$task_root/ngene"
printf 'Executable: %s/ngene\nConfiguration: %s/configuration/GNSS_RX.conf\n' "$task_root" "$task_root"
echo 'Start: sh run.sh    Recording: sh run.sh --input recordings/native-ab.raw'
