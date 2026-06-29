#!/bin/sh

set -eu

cd "$(dirname "$0")"

if [ ! -x ./raytracer ]; then
    echo "Error: ./raytracer not found or not executable. Run ./build.sh first." >&2
    exit 1
fi

./raytracer "$@"
