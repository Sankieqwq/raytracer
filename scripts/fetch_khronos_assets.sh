#!/bin/sh

set -eu

cd "$(dirname "$0")/.."
mkdir -p models/khronos

download() {
    url="$1"
    out="$2"
    if [ -f "$out" ]; then
        echo "Already exists: $out"
    else
        echo "Downloading $out"
        curl -L "$url" -o "$out"
    fi
}

download "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/WaterBottle/glTF-Binary/WaterBottle.glb" \
    "models/khronos/WaterBottle.glb"
download "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/TransmissionTest/glTF-Binary/TransmissionTest.glb" \
    "models/khronos/TransmissionTest.glb"
