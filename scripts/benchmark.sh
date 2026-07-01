#!/bin/sh

set -eu
cd "$(dirname "$0")/.."

if [ ! -x ./raytracer ]; then
    ./build.sh
fi

THREADS="${THREADS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
SCENES="scenes/default.json scenes/pbr_test.json scenes/mirror_glass_water.json scenes/mark.json"

for scene in $SCENES; do
    echo "== $scene / 1 thread =="
    ./raytracer --scene "$scene" --samples 16 --threads 1 --stats --out /tmp/benchmark.ppm
    echo "== $scene / $THREADS threads =="
    ./raytracer --scene "$scene" --samples 16 --threads "$THREADS" --stats --out /tmp/benchmark.ppm
done
