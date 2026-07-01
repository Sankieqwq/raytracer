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
download "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/CompareMetallic/glTF-Binary/CompareMetallic.glb" \
    "models/khronos/CompareMetallic.glb"
download "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/CompareRoughness/glTF-Binary/CompareRoughness.glb" \
    "models/khronos/CompareRoughness.glb"
download "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/AttenuationTest/glTF-Binary/AttenuationTest.glb" \
    "models/khronos/AttenuationTest.glb"
download "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/TransmissionRoughnessTest/glTF-Binary/TransmissionRoughnessTest.glb" \
    "models/khronos/TransmissionRoughnessTest.glb"
download "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/GlassVaseFlowers/glTF-Binary/GlassVaseFlowers.glb" \
    "models/khronos/GlassVaseFlowers.glb"
download "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/GlassBrokenWindow/glTF-Binary/GlassBrokenWindow.glb" \
    "models/khronos/GlassBrokenWindow.glb"
download "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/GlassHurricaneCandleHolder/glTF-Binary/GlassHurricaneCandleHolder.glb" \
    "models/khronos/GlassHurricaneCandleHolder.glb"
