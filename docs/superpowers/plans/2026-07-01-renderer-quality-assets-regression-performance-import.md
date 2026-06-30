# Renderer Quality, Assets, Regression, Performance, and Import Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stabilize the current PBR/emissive fixes, improve display output, add official GLB acceptance assets, add deterministic golden image tests, measure/optimize CPU performance, and improve OBJ/GLB material import.

**Architecture:** Keep the current header-only C++17 structure and add small, focused helpers rather than a broad renderer rewrite. Rendering remains CPU path tracing; output conversion, asset fixtures, regression harnesses, benchmarking, and importer mapping are layered around the existing scene/material/mesh pipeline.

**Tech Stack:** C++17, CMake/CTest, shell scripts, Python 3 for golden image comparison, stb_image/stb_image_write, tinyobjloader, Khronos glTF Sample Assets.

---

## File Structure

- `src/main.cpp`: CLI parsing, render loop, stats timing, seed/exposure/tone-map options.
- `include/raytracer/render/image.h`: display conversion, tone mapping, PPM/PNG output dispatch.
- `include/raytracer/math/util.h`: deterministic seedable random utilities and per-thread RNG helpers.
- `include/raytracer/scene/scene.h`: scene settings such as exposure/seed and emissive area precomputation.
- `include/raytracer/scene/obj.h`: OBJ `.mtl` parsing and material/texture propagation.
- `include/raytracer/scene/glb.h`: GLB material texture, emissive, volume, and double-sided metadata parsing.
- `include/raytracer/material/material.h`: material fields used by import enhancements, especially dielectric absorption if implemented with volume data.
- `tests/regression_tests.cpp`: C++ behavior tests for tone mapping, output dispatch helpers, importer parsing, and deterministic configuration.
- `tests/golden/compare_images.py`: deterministic image comparison and optional golden update.
- `tests/golden/scenes/*.json`: small deterministic scene files for image regression.
- `tests/golden/images/*.png`: committed golden reference images.
- `scripts/fetch_khronos_assets.sh`: explicit, license-aware asset downloader.
- `scripts/benchmark.sh`: repeatable render benchmark runner.
- `models/khronos/LICENSES.md`: source URL, credits, and license summary for each imported Khronos asset.
- `README.md`: user-facing usage, supported import matrix, asset/license notes, and testing commands.

---

### Task 1: Verify and Commit Current PBR/Emissive Baseline

**Files:**
- Modify already pending: `CMakeLists.txt`
- Modify already pending: `README.md`
- Modify already pending: `include/raytracer/material/material.h`
- Modify already pending: `include/raytracer/scene/scene.h`
- Modify already pending: `src/main.cpp`
- Modify already pending: `tests/regression_tests.cpp`
- Create already pending: `scenes/mirror_glass_water.json`

- [ ] **Step 1: Inspect the pending baseline diff**

Run:

```bash
git status --short
git diff --stat
```

Expected: only the PBR/emissive consistency changes and `scenes/mirror_glass_water.json` are pending.

- [ ] **Step 2: Run script build**

Run:

```bash
./build.sh
```

Expected: exits 0 and writes `./raytracer`. Warnings from `third_party/tiny_obj_loader.h` unused functions are acceptable.

- [ ] **Step 3: Run CMake build and tests**

Run:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: `100% tests passed, 0 tests failed`.

- [ ] **Step 4: Run high-sample acceptance render**

Run:

```bash
./run.sh --scene scenes/mirror_glass_water.json --samples 512 --out /tmp/mirror_glass_water_hq.ppm
sips -s format png /tmp/mirror_glass_water_hq.ppm --out /tmp/mirror_glass_water_hq.png
file /tmp/mirror_glass_water_hq.ppm /tmp/mirror_glass_water_hq.png
```

Expected: PPM and PNG are both 640x360 images.

- [ ] **Step 5: Commit only the baseline fix**

Run:

```bash
git add CMakeLists.txt README.md include/raytracer/material/material.h include/raytracer/scene/scene.h src/main.cpp tests/regression_tests.cpp scenes/mirror_glass_water.json
git commit -m "Fix PBR and emissive light sampling consistency"
```

Expected: commit succeeds and working tree is clean except for any later plan-only files.

---

### Task 2: Add Display Conversion and PNG Output Tests

**Files:**
- Modify: `tests/regression_tests.cpp`
- Modify: `CMakeLists.txt`
- Modify later: `include/raytracer/render/image.h`
- Add later: `third_party/stb_image_write.h`

- [ ] **Step 1: Write failing tone-map tests**

Add these tests near the existing helper tests in `tests/regression_tests.cpp`:

```cpp
void test_display_color_exposure_and_tone_mapping() {
    ImageOutputOptions opts;
    opts.exposure = 2.0;
    opts.tone_map = ToneMapMode::Reinhard;
    Color out = to_display_color(Color(1.0, 0.5, 0.0), 1, opts);

    check(out.x > out.y, "tone mapped red channel should remain brighter than green");
    check(out.x < 1.0 && out.y < 1.0, "tone mapped display color should stay below one");
    check(out.z == 0.0, "zero input channel should stay zero");
}

void test_output_format_detection() {
    check(output_format_for_path("image.ppm") == ImageOutputFormat::PPM,
          "ppm extension should select PPM output");
    check(output_format_for_path("image.png") == ImageOutputFormat::PNG,
          "png extension should select PNG output");
    check(output_format_for_path("image.unknown") == ImageOutputFormat::PPM,
          "unknown extension should preserve old PPM behavior");
}
```

Call both tests from `main()`.

- [ ] **Step 2: Run tests and verify failure**

Run:

```bash
cmake --build build --target raytracer_regression_tests
ctest --test-dir build --output-on-failure
```

Expected: compile fails because `ImageOutputOptions`, `ToneMapMode`, `to_display_color`, `output_format_for_path`, and `ImageOutputFormat` are not defined.

- [ ] **Step 3: Add stb_image_write to build inputs**

Download the single-header writer from upstream stb:

```bash
curl -L https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h -o third_party/stb_image_write.h
```

In `src/main.cpp`, add:

```cpp
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
```

Expected: implementation is available only once, matching the current `stb_image.h` pattern.

---

### Task 3: Implement Tone Mapping, Exposure, and PNG Output

**Files:**
- Modify: `include/raytracer/render/image.h`
- Modify: `include/raytracer/scene/scene.h`
- Modify: `src/main.cpp`
- Modify: `README.md`
- Modify: `tests/regression_tests.cpp`
- Create: `third_party/stb_image_write.h`

- [ ] **Step 1: Implement output option types and display conversion**

Replace `include/raytracer/render/image.h` with a structure equivalent to:

```cpp
// Module D: image -- image output, tone mapping, and display conversion
#ifndef RT_IMAGE_H
#define RT_IMAGE_H

#include "raytracer/math/vec3.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <string>
#include <vector>

extern int stbi_write_png(char const* filename, int w, int h, int comp,
                          const void* data, int stride_in_bytes);

enum class ToneMapMode { None, Reinhard, ACES };
enum class ImageOutputFormat { PPM, PNG };

struct ImageOutputOptions {
    double exposure = 1.0;
    ToneMapMode tone_map = ToneMapMode::ACES;
};

inline std::string lower_ext(std::string path) {
    std::string ext;
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos) ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

inline ImageOutputFormat output_format_for_path(const std::string& path) {
    return lower_ext(path) == ".png" ? ImageOutputFormat::PNG : ImageOutputFormat::PPM;
}

inline ToneMapMode parse_tone_map_mode(const std::string& value) {
    if (value == "none") return ToneMapMode::None;
    if (value == "reinhard") return ToneMapMode::Reinhard;
    if (value == "aces") return ToneMapMode::ACES;
    throw std::runtime_error("Unknown tone map mode: " + value);
}

inline Color aces_tonemap(const Color& c) {
    auto aces = [](double x) {
        double a = 2.51;
        double b = 0.03;
        double cc = 2.43;
        double d = 0.59;
        double e = 0.14;
        return std::clamp((x * (a * x + b)) / (x * (cc * x + d) + e), 0.0, 1.0);
    };
    return Color(aces(c.x), aces(c.y), aces(c.z));
}

inline Color apply_tone_map(const Color& c, ToneMapMode mode) {
    if (mode == ToneMapMode::None) return c;
    if (mode == ToneMapMode::Reinhard) {
        return Color(c.x / (1.0 + c.x), c.y / (1.0 + c.y), c.z / (1.0 + c.z));
    }
    return aces_tonemap(c);
}

inline Color to_display_color(const Color& raw, int samples_per_pixel,
                              const ImageOutputOptions& options) {
    double scale = 1.0 / std::max(1, samples_per_pixel);
    Color c = raw * scale * options.exposure;
    c = apply_tone_map(c, options.tone_map);
    return gamma2(c);
}

inline unsigned char display_byte(double v) {
    double clamped = std::clamp(v, 0.0, 0.999);
    return static_cast<unsigned char>(256 * clamped);
}

inline void write_ppm(const std::string& path, int w, int h,
                      const std::vector<Color>& pixels,
                      int samples_per_pixel,
                      const ImageOutputOptions& options = ImageOutputOptions()) {
    std::ofstream out(path);
    out << "P3\n" << w << ' ' << h << "\n255\n";
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            Color c = to_display_color(pixels[j * w + i], samples_per_pixel, options);
            out << int(display_byte(c.x)) << ' '
                << int(display_byte(c.y)) << ' '
                << int(display_byte(c.z)) << '\n';
        }
    }
}

inline void write_png(const std::string& path, int w, int h,
                      const std::vector<Color>& pixels,
                      int samples_per_pixel,
                      const ImageOutputOptions& options = ImageOutputOptions()) {
    std::vector<unsigned char> bytes(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            Color c = to_display_color(pixels[j * w + i], samples_per_pixel, options);
            size_t idx = (static_cast<size_t>(j) * static_cast<size_t>(w) + static_cast<size_t>(i)) * 3;
            bytes[idx + 0] = display_byte(c.x);
            bytes[idx + 1] = display_byte(c.y);
            bytes[idx + 2] = display_byte(c.z);
        }
    }
    if (!stbi_write_png(path.c_str(), w, h, 3, bytes.data(), w * 3)) {
        throw std::runtime_error("Failed to write PNG: " + path);
    }
}

inline void write_image(const std::string& path, int w, int h,
                        const std::vector<Color>& pixels,
                        int samples_per_pixel,
                        const ImageOutputOptions& options = ImageOutputOptions()) {
    if (output_format_for_path(path) == ImageOutputFormat::PNG) {
        write_png(path, w, h, pixels, samples_per_pixel, options);
    } else {
        write_ppm(path, w, h, pixels, samples_per_pixel, options);
    }
}

#endif
```

- [ ] **Step 2: Extend Scene with image output options**

In `include/raytracer/scene/scene.h`, add to `Scene`:

```cpp
ImageOutputOptions output_options;
```

Also include:

```cpp
#include "raytracer/render/image.h"
```

In the `image` parsing block inside `load_scene`, add:

```cpp
if (img.has("exposure")) scene.output_options.exposure = img.at("exposure").numVal;
if (img.has("tone_map")) scene.output_options.tone_map = parse_tone_map_mode(img.at("tone_map").strVal);
```

- [ ] **Step 3: Add CLI flags**

In `src/main.cpp`, add local CLI overrides:

```cpp
double exposure_override = -1.0;
std::string tone_map_override;
```

Add parsing branches before `--direct-only`:

```cpp
} else if (arg == "--exposure" && i + 1 < argc) {
    exposure_override = std::stod(argv[++i]);
} else if (arg == "--tone-map" && i + 1 < argc) {
    tone_map_override = argv[++i];
```

After scene loading:

```cpp
if (exposure_override > 0) scene.output_options.exposure = exposure_override;
if (!tone_map_override.empty()) scene.output_options.tone_map = parse_tone_map_mode(tone_map_override);
```

Update usage text:

```cpp
              << "  --exposure <n>     display exposure multiplier (default: scene/default 1.0)\n"
              << "  --tone-map <mode>  tone mapping: aces, reinhard, none\n"
```

- [ ] **Step 4: Switch final writer**

Replace the final write in `src/main.cpp`:

```cpp
write_ppm(scene.output, scene.width, scene.height, pixels, scene.samples);
```

with:

```cpp
write_image(scene.output, scene.width, scene.height, pixels, scene.samples, scene.output_options);
```

- [ ] **Step 5: Verify tests pass**

Run:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: regression tests pass.

- [ ] **Step 6: Smoke render PNG**

Run:

```bash
./build/raytracer --scene scenes/mirror_glass_water.json --samples 64 --out /tmp/mirror_glass_water_tone.png --tone-map aces --exposure 1.2
file /tmp/mirror_glass_water_tone.png
```

Expected: PNG image data, 640 x 360.

- [ ] **Step 7: Update README and commit**

Update `README.md` command-line table with `--exposure` and `--tone-map`, and mention `.png` output support in the preview/output section.

Run:

```bash
git add README.md src/main.cpp include/raytracer/render/image.h include/raytracer/scene/scene.h tests/regression_tests.cpp third_party/stb_image_write.h
git commit -m "Improve display output with tone mapping and PNG support"
```

Expected: commit succeeds.

---

### Task 4: Add Khronos Asset Fetching and Acceptance Scenes

**Files:**
- Create: `scripts/fetch_khronos_assets.sh`
- Create: `models/khronos/LICENSES.md`
- Create: `scenes/khronos_water_bottle.json`
- Create: `scenes/khronos_transmission_test.json`
- Modify: `README.md`

- [ ] **Step 1: Create fetch script**

Create `scripts/fetch_khronos_assets.sh`:

```sh
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
```

Run:

```bash
chmod +x scripts/fetch_khronos_assets.sh
```

- [ ] **Step 2: Add license document**

Create `models/khronos/LICENSES.md`:

```markdown
# Khronos Sample Asset Licenses

Assets in this directory come from the KhronosGroup glTF Sample Assets repository:
https://github.com/KhronosGroup/glTF-Sample-Assets

## WaterBottle.glb

- Source: https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/WaterBottle
- License: CC0 1.0 Universal
- Credit: Microsoft for Everything

## TransmissionTest.glb

- Source: https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/TransmissionTest
- License: CC0 1.0 Universal
- Credit: Adobe for Everything
```

- [ ] **Step 3: Add WaterBottle scene**

Create `scenes/khronos_water_bottle.json`:

```json
{
    "image": {
        "width": 640,
        "height": 360,
        "samples": 32,
        "max_depth": 32,
        "output": "khronos_water_bottle.png",
        "tone_map": "aces",
        "exposure": 1.0
    },
    "camera": {
        "auto": true,
        "vfov": 35
    },
    "lighting": {
        "ambient": [0.035, 0.035, 0.04]
    },
    "lights": [
        {
            "type": "directional",
            "direction": [-0.5, -1.2, -0.8],
            "color": [1.0, 0.96, 0.9],
            "intensity": 0.8
        },
        {
            "type": "point",
            "position": [2.5, 3.5, 4.0],
            "color": [0.8, 0.9, 1.0],
            "intensity": 5.0
        }
    ],
    "ground": {
        "enabled": true
    },
    "objects": [
        {
            "type": "mesh",
            "file": "../models/khronos/WaterBottle.glb",
            "auto_fit": true,
            "fit_size": 3.0,
            "override_material": false,
            "material": {
                "type": "lambertian",
                "albedo": [0.7, 0.7, 0.7]
            }
        }
    ]
}
```

- [ ] **Step 4: Add TransmissionTest scene**

Create `scenes/khronos_transmission_test.json`:

```json
{
    "image": {
        "width": 640,
        "height": 360,
        "samples": 32,
        "max_depth": 32,
        "output": "khronos_transmission_test.png",
        "tone_map": "aces",
        "exposure": 1.0
    },
    "camera": {
        "auto": true,
        "vfov": 35
    },
    "lighting": {
        "ambient": [0.035, 0.035, 0.04]
    },
    "lights": [
        {
            "type": "directional",
            "direction": [-0.5, -1.2, -0.8],
            "color": [1.0, 0.96, 0.9],
            "intensity": 0.8
        },
        {
            "type": "point",
            "position": [2.5, 3.5, 4.0],
            "color": [0.8, 0.9, 1.0],
            "intensity": 5.0
        }
    ],
    "ground": {
        "enabled": true
    },
    "objects": [
        {
            "type": "mesh",
            "file": "../models/khronos/TransmissionTest.glb",
            "auto_fit": true,
            "fit_size": 3.0,
            "override_material": false,
            "material": {
                "type": "lambertian",
                "albedo": [0.7, 0.7, 0.7]
            }
        }
    ]
}
```

- [ ] **Step 5: Fetch assets and smoke render**

Run:

```bash
./scripts/fetch_khronos_assets.sh
./build/raytracer --scene scenes/khronos_water_bottle.json --samples 8 --out /tmp/khronos_water_bottle.png
./build/raytracer --scene scenes/khronos_transmission_test.json --samples 8 --out /tmp/khronos_transmission_test.png
file /tmp/khronos_water_bottle.png /tmp/khronos_transmission_test.png
```

Expected: both files are PNG images.

- [ ] **Step 6: Update README and commit**

Update `README.md` scene list and add a short section naming the Khronos source and license location.

Run:

```bash
git add README.md scripts/fetch_khronos_assets.sh models/khronos/LICENSES.md models/khronos/WaterBottle.glb models/khronos/TransmissionTest.glb scenes/khronos_water_bottle.json scenes/khronos_transmission_test.json
git commit -m "Add Khronos GLB acceptance assets and scenes"
```

Expected: commit succeeds. If asset size or license review fails, do not add `.glb` files; commit the fetch script, license document, README, and scenes only.

---

### Task 5: Add Deterministic Seed Support

**Files:**
- Modify: `include/raytracer/math/util.h`
- Modify: `include/raytracer/scene/scene.h`
- Modify: `src/main.cpp`
- Modify: `tests/regression_tests.cpp`

- [ ] **Step 1: Write failing seed tests**

Add to `tests/regression_tests.cpp`:

```cpp
void test_random_seed_repeats_sequence() {
    set_random_seed(1234);
    double a = random_double();
    double b = random_double();
    set_random_seed(1234);
    check(near(a, random_double()), "first random sample should repeat after seed reset");
    check(near(b, random_double()), "second random sample should repeat after seed reset");
}
```

Call it from `main()`.

- [ ] **Step 2: Run test and verify failure**

Run:

```bash
cmake --build build --target raytracer_regression_tests
ctest --test-dir build --output-on-failure
```

Expected: compile fails because `set_random_seed` does not exist.

- [ ] **Step 3: Implement seedable RNG**

Replace random helpers in `include/raytracer/math/util.h` with:

```cpp
inline unsigned int& random_seed_storage() {
    static unsigned int seed = std::random_device{}();
    return seed;
}

inline std::mt19937& random_generator() {
    thread_local std::mt19937 gen(random_seed_storage());
    return gen;
}

inline void set_random_seed(unsigned int seed) {
    random_seed_storage() = seed;
    random_generator().seed(seed);
}

inline double random_double() {
    thread_local std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(random_generator());
}
```

- [ ] **Step 4: Add Scene seed option and CLI**

In `Scene`, add:

```cpp
unsigned int seed = 0;
bool has_seed = false;
```

In image parsing:

```cpp
if (img.has("seed")) {
    scene.seed = static_cast<unsigned int>(img.at("seed").numVal);
    scene.has_seed = true;
}
```

In `src/main.cpp`, add `--seed <n>` parsing and after scene loading:

```cpp
if (seed_override >= 0) {
    set_random_seed(static_cast<unsigned int>(seed_override));
} else if (scene.has_seed) {
    set_random_seed(scene.seed);
}
```

- [ ] **Step 5: Verify and commit**

Run:

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
git add include/raytracer/math/util.h include/raytracer/scene/scene.h src/main.cpp tests/regression_tests.cpp
git commit -m "Add deterministic render seed support"
```

Expected: tests pass and commit succeeds.

---

### Task 6: Add Golden Image Regression Harness

**Files:**
- Create: `tests/golden/compare_images.py`
- Create: `tests/golden/scenes/golden_default.json`
- Create: `tests/golden/scenes/golden_mirror_glass_water.json`
- Create: `tests/golden/images/*.png`
- Modify: `CMakeLists.txt`
- Modify: `README.md`

- [ ] **Step 1: Create golden scene for default**

Create `tests/golden/scenes/golden_default.json`:

```json
{
    "image": {
        "width": 128,
        "height": 72,
        "samples": 8,
        "max_depth": 16,
        "output": "/tmp/golden_default.png",
        "tone_map": "aces",
        "exposure": 1.0,
        "seed": 12345
    },
    "camera": {
        "lookfrom": [0, 0, 0],
        "lookat": [0, 0, -1],
        "vfov": 60
    },
    "objects": [
        {
            "type": "sphere",
            "center": [0, 0, -1],
            "radius": 0.5,
            "material": { "type": "lambertian", "albedo": [0.1, 0.2, 0.5] }
        },
        {
            "type": "sphere",
            "center": [0, -100.5, -1],
            "radius": 100,
            "material": { "type": "lambertian", "albedo": [0.8, 0.8, 0.0] }
        }
    ]
}
```

- [ ] **Step 2: Create golden scene for mirror/glass/water**

Copy `scenes/mirror_glass_water.json` to `tests/golden/scenes/golden_mirror_glass_water.json`, then set:

```json
"width": 128,
"height": 72,
"samples": 8,
"output": "/tmp/golden_mirror_glass_water.png",
"seed": 23456
```

- [ ] **Step 3: Create compare script**

Create `tests/golden/compare_images.py`:

```python
#!/usr/bin/env python3
import argparse
from pathlib import Path
from PIL import Image

def load_rgb(path):
    return Image.open(path).convert("RGB")

def compare(actual_path, expected_path, max_mean, max_pixel):
    actual = load_rgb(actual_path)
    expected = load_rgb(expected_path)
    if actual.size != expected.size:
        raise SystemExit(f"size mismatch: {actual.size} != {expected.size}")

    total = 0
    max_delta = 0
    count = actual.size[0] * actual.size[1] * 3
    for a, e in zip(actual.getdata(), expected.getdata()):
        for av, ev in zip(a, e):
            delta = abs(av - ev)
            total += delta
            max_delta = max(max_delta, delta)

    mean = total / count
    print(f"mean={mean:.4f} max={max_delta}")
    if mean > max_mean or max_delta > max_pixel:
        raise SystemExit(f"image diff too large: mean={mean:.4f}, max={max_delta}")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--actual", required=True)
    parser.add_argument("--expected", required=True)
    parser.add_argument("--update", action="store_true")
    parser.add_argument("--max-mean", type=float, default=1.0)
    parser.add_argument("--max-pixel", type=int, default=8)
    args = parser.parse_args()

    actual = Path(args.actual)
    expected = Path(args.expected)
    if args.update:
        expected.parent.mkdir(parents=True, exist_ok=True)
        expected.write_bytes(actual.read_bytes())
        print(f"updated {expected}")
        return
    compare(actual, expected, args.max_mean, args.max_pixel)

if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Add CMake golden test**

In `CMakeLists.txt`, find Python:

```cmake
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME golden_default
        COMMAND sh -c "$<TARGET_FILE:raytracer> --scene tests/golden/scenes/golden_default.json --threads 1 --out /tmp/golden_default.png && ${Python3_EXECUTABLE} tests/golden/compare_images.py --actual /tmp/golden_default.png --expected tests/golden/images/golden_default.png")
    set_tests_properties(golden_default PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
```

Add a second `golden_mirror_glass_water` test with matching paths.

- [ ] **Step 5: Generate initial golden images**

Run:

```bash
./build/raytracer --scene tests/golden/scenes/golden_default.json --threads 1 --out /tmp/golden_default.png
python3 tests/golden/compare_images.py --actual /tmp/golden_default.png --expected tests/golden/images/golden_default.png --update
./build/raytracer --scene tests/golden/scenes/golden_mirror_glass_water.json --threads 1 --out /tmp/golden_mirror_glass_water.png
python3 tests/golden/compare_images.py --actual /tmp/golden_mirror_glass_water.png --expected tests/golden/images/golden_mirror_glass_water.png --update
```

Expected: two PNG files are created under `tests/golden/images/`.

- [ ] **Step 6: Run tests and commit**

Run:

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
git add CMakeLists.txt README.md tests/golden
git commit -m "Add deterministic golden image regression tests"
```

Expected: all CTest tests pass and commit succeeds.

---

### Task 7: Add Stats, Benchmarking, and Low-Risk Performance Optimizations

**Files:**
- Modify: `src/main.cpp`
- Modify: `include/raytracer/scene/scene.h`
- Create: `scripts/benchmark.sh`
- Modify: `README.md`

- [ ] **Step 1: Add stats option and timing**

In `RenderOptions`, add:

```cpp
bool stats = false;
```

Parse:

```cpp
} else if (arg == "--stats") {
    render_options.stats = true;
```

Add `#include <chrono>` to `src/main.cpp`, then time scene loading and render loop with `std::chrono::steady_clock`.

- [ ] **Step 2: Precompute emissive total area**

In `Scene`, add:

```cpp
double emissive_total_area = 0.0;
```

At the end of `collect_emissive_objects`, add:

```cpp
scene.emissive_total_area = 0.0;
for (const EmissiveObject& eo : scene.emissive_objects) {
    scene.emissive_total_area += eo.geometry->area();
}
```

Replace calls to `total_emissive_area(scene)` in `src/main.cpp` with `scene.emissive_total_area`, then remove the helper if unused.

- [ ] **Step 3: Print stats**

After rendering, if stats are enabled, print:

```cpp
if (render_options.stats) {
    std::cout << "Stats:\n"
              << "  load_ms=" << load_ms << "\n"
              << "  render_ms=" << render_ms << "\n"
              << "  total_ms=" << total_ms << "\n"
              << "  emissive_area=" << scene.emissive_total_area << "\n";
}
```

- [ ] **Step 4: Create benchmark script**

Create `scripts/benchmark.sh`:

```sh
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
```

Run:

```bash
chmod +x scripts/benchmark.sh
```

- [ ] **Step 5: Verify and commit**

Run:

```bash
./build.sh
./scripts/benchmark.sh
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
git add src/main.cpp include/raytracer/scene/scene.h scripts/benchmark.sh README.md
git commit -m "Add render benchmarking and stats output"
```

Expected: benchmark prints stats for each scene and tests pass.

---

### Task 8: Add Basic OBJ MTL Loading

**Files:**
- Modify: `include/raytracer/scene/obj.h`
- Modify: `include/raytracer/scene/scene.h`
- Create: `models/obj/mtl_test.obj`
- Create: `models/obj/mtl_test.mtl`
- Create: `textures/mtl_test.ppm`
- Create: `scenes/obj_mtl_test.json`
- Modify: `tests/regression_tests.cpp`
- Modify: `README.md`

- [ ] **Step 1: Write failing MTL fixture test**

Add to `tests/regression_tests.cpp`:

```cpp
void test_obj_loader_reads_mtl_diffuse_and_texture() {
    ObjMeshData mesh = load_model_mesh("models/obj/mtl_test.obj");
    check(mesh.materials.size() == 1, "OBJ MTL fixture should load one material");
    if (!mesh.materials.empty()) {
        check(near_vec(mesh.materials[0].albedo, Color(0.25, 0.5, 0.75)),
              "Kd should map to loaded material albedo");
        check(mesh.materials[0].base_color_texture == 0,
              "map_Kd should map to base color texture index");
        check(!mesh.textures.empty(), "map_Kd should create a loaded texture entry");
    }
}
```

Call it from `main()`.

- [ ] **Step 2: Add fixture files**

Create `models/obj/mtl_test.obj`:

```text
mtllib mtl_test.mtl
v 0 0 0
v 1 0 0
v 0 1 0
vt 0 0
vt 1 0
vt 0 1
usemtl blue
f 1/1 2/2 3/3
```

Create `models/obj/mtl_test.mtl`:

```text
newmtl blue
Kd 0.25 0.5 0.75
map_Kd ../../textures/mtl_test.ppm
```

Create `textures/mtl_test.ppm`:

```text
P3
1 1
255
64 128 192
```

- [ ] **Step 3: Run test and verify failure**

Run:

```bash
cmake --build build --target raytracer_regression_tests
ctest --test-dir build --output-on-failure
```

Expected: test fails because OBJ material data is not populated.

- [ ] **Step 4: Implement MTL mapping with tinyobjloader data**

In `load_obj_mesh(const std::string& path, const Mat4& transform)`, after successful `tinyobj::LoadObj`, convert `materials`:

```cpp
std::filesystem::path obj_dir = std::filesystem::absolute(path).parent_path();
for (const tinyobj::material_t& src : materials) {
    LoadedMaterialData dst;
    dst.name = src.name;
    dst.albedo = Color(src.diffuse[0], src.diffuse[1], src.diffuse[2]);
    if (!src.diffuse_texname.empty()) {
        LoadedTextureData tex;
        std::filesystem::path tex_path(src.diffuse_texname);
        if (tex_path.is_relative()) tex_path = obj_dir / tex_path;
        tex.path = tex_path.lexically_normal().string();
        tex.name = src.diffuse_texname;
        dst.base_color_texture = static_cast<int>(mesh.textures.size());
        mesh.textures.push_back(tex);
    }
    mesh.materials.push_back(dst);
}
```

Keep existing triangle `material_index` propagation.

- [ ] **Step 5: Add scene and verify render**

Create `scenes/obj_mtl_test.json` with one mesh pointing to `../models/obj/mtl_test.obj`, `override_material: false`, and a fallback Lambertian material.

Run:

```bash
./build/raytracer --scene scenes/obj_mtl_test.json --samples 4 --out /tmp/obj_mtl_test.png
file /tmp/obj_mtl_test.png
```

Expected: PNG image data.

- [ ] **Step 6: Commit**

Run:

```bash
git add include/raytracer/scene/obj.h include/raytracer/scene/scene.h tests/regression_tests.cpp models/obj/mtl_test.obj models/obj/mtl_test.mtl textures/mtl_test.ppm scenes/obj_mtl_test.json README.md
git commit -m "Add basic OBJ MTL material loading"
```

Expected: commit succeeds.

---

### Task 9: Extend GLB Material Mapping and Final Documentation

**Files:**
- Modify: `include/raytracer/scene/obj.h`
- Modify: `include/raytracer/scene/glb.h`
- Modify: `include/raytracer/scene/scene.h`
- Modify: `tests/regression_tests.cpp`
- Modify: `README.md`

- [ ] **Step 1: Extend loaded material data**

In `LoadedMaterialData`, add:

```cpp
int metallic_roughness_texture = -1;
int normal_texture = -1;
Color emissive = Color(0, 0, 0);
int emissive_texture = -1;
bool double_sided = false;
Color attenuation_color = Color(1, 1, 1);
double attenuation_distance = infinity;
```

- [ ] **Step 2: Parse GLB material fields**

In `glb_material_data`, inside `pbrMetallicRoughness`:

```cpp
if (pbr.has("metallicRoughnessTexture")) {
    data.metallic_roughness_texture = json_int(pbr.at("metallicRoughnessTexture"), "index", -1);
}
```

At material root:

```cpp
if (material.has("normalTexture")) data.normal_texture = json_int(material.at("normalTexture"), "index", -1);
if (material.has("emissiveFactor")) data.emissive = glb_to_vec3(material.at("emissiveFactor"));
if (material.has("emissiveTexture")) data.emissive_texture = json_int(material.at("emissiveTexture"), "index", -1);
if (material.has("doubleSided")) data.double_sided = material.at("doubleSided").boolVal;
```

Inside `KHR_materials_volume` parsing:

```cpp
if (exts.has("KHR_materials_volume")) {
    const JsonValue& volume = exts.at("KHR_materials_volume");
    if (volume.has("attenuationColor")) data.attenuation_color = glb_to_vec3(volume.at("attenuationColor"));
    if (volume.has("attenuationDistance")) data.attenuation_distance = volume.at("attenuationDistance").numVal;
}
```

- [ ] **Step 3: Map emissive GLB materials**

In `add_loaded_material`, before transmission/metallic routing:

```cpp
if (data.emissive.length_squared() > 1e-12) {
    mat = std::make_unique<Emissive>(data.emissive);
} else if (data.transmission > 0.5 || data.alpha_blend) {
    mat = std::make_unique<Dielectric>(data.ior, make_loaded_texture(mesh, data));
}
```

If `normal_texture` is present and the material maps to PBR, set `pbr->normal` and `pbr->has_normal_map`.

- [ ] **Step 4: Add regression coverage**

Add this direct `LoadedMaterialData` unit-style test:

```cpp
void test_loaded_material_emissive_routes_to_emissive() {
    Scene scene;
    ObjMeshData mesh;
    LoadedMaterialData data;
    data.emissive = Color(2, 3, 4);
    Material* mat = add_loaded_material(mesh, data, scene);
    check(mat->is_emissive(), "GLB emissiveFactor should route to Emissive material");
}
```

Call it from `main()`.

- [ ] **Step 5: Verify Khronos scenes**

Run:

```bash
./build/raytracer --scene scenes/khronos_water_bottle.json --samples 16 --out /tmp/khronos_water_bottle_import.png
./build/raytracer --scene scenes/khronos_transmission_test.json --samples 16 --out /tmp/khronos_transmission_import.png
file /tmp/khronos_water_bottle_import.png /tmp/khronos_transmission_import.png
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: both PNGs are valid and tests pass.

- [ ] **Step 6: Update import docs and commit**

Update `README.md` OBJ/GLB support section with:

```markdown
- OBJ `.mtl`: supports `Kd` and `map_Kd`; `Ks`/`Ns` are parsed conservatively or ignored.
- GLB: supports base color, baseColorTexture, metallic/roughness factors, metallicRoughnessTexture, normalTexture, emissiveFactor, alpha blend, transmission, IOR, and volume attenuation metadata.
- Unsupported GLB extensions are ignored with fallback material behavior.
```

Run:

```bash
git add include/raytracer/scene/obj.h include/raytracer/scene/glb.h include/raytracer/scene/scene.h tests/regression_tests.cpp README.md
git commit -m "Extend GLB material texture and emission mapping"
```

Expected: commit succeeds.

---

### Task 10: Final Verification

**Files:**
- No code changes expected.

- [ ] **Step 1: Run full build/test verification**

Run:

```bash
./build.sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: build succeeds and all tests pass.

- [ ] **Step 2: Render representative scenes**

Run:

```bash
./raytracer --scene scenes/default.json --samples 8 --out /tmp/final_default.png
./raytracer --scene scenes/mirror_glass_water.json --samples 64 --out /tmp/final_mirror_glass_water.png
./raytracer --scene scenes/khronos_water_bottle.json --samples 16 --out /tmp/final_water_bottle.png
./raytracer --scene scenes/obj_mtl_test.json --samples 8 --out /tmp/final_obj_mtl.png
file /tmp/final_default.png /tmp/final_mirror_glass_water.png /tmp/final_water_bottle.png /tmp/final_obj_mtl.png
```

Expected: all outputs are valid PNG images.

- [ ] **Step 3: Run benchmark once**

Run:

```bash
./scripts/benchmark.sh
```

Expected: every benchmark scene prints stats and exits 0.

- [ ] **Step 4: Check git history and status**

Run:

```bash
git log --oneline -n 10
git status --short
```

Expected: commits are stage-scoped and working tree is clean.
