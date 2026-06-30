#!/bin/sh

set -eu

cd "$(dirname "$0")"

CXX="${CXX:-g++}"
CXXFLAGS="${CXXFLAGS:--std=c++17 -O2 -Wall -Wextra}"

echo "Building raytracer with $CXX"
"$CXX" $CXXFLAGS -Iinclude -Ithird_party -o raytracer src/main.cpp
echo "Wrote ./raytracer"

echo "Building raytracer_blender_bridge with $CXX"
"$CXX" $CXXFLAGS -Iinclude -Ithird_party -o raytracer_blender_bridge src/blender_bridge.cpp
echo "Wrote ./raytracer_blender_bridge"
