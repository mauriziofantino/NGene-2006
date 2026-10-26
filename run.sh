#!/bin/sh
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
if [ ! -x "$task_root/ngene" ]; then
    echo 'ngene is missing. Run sh build.sh first.' >&2
    exit 1
fi
cd -- "$task_root"
exec "$task_root/ngene" -n "$@"
