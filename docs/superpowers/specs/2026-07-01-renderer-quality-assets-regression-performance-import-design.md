# Renderer Quality, Assets, Regression, Performance, and Import Roadmap

## Context

The renderer is a C++17 CPU path tracer with JSON scene loading, OBJ/GLB mesh support, linear BVH traversal, PBR materials, textures, dielectric glass/water, emissive area lights, and regression tests. The current working tree contains an approved PBR/emissive consistency fix that has been verified locally but not yet committed. This roadmap keeps that fix as a baseline commit, then proceeds through render-quality, asset, image-regression, performance, and import improvements as independent stages.

## Goals

- Preserve a clean git history with one commit per logical stage.
- Improve display quality without destabilizing geometry or import behavior.
- Add official real-world test assets with clear license handling.
- Add deterministic golden image regression tests before deeper performance and importer changes.
- Optimize only after adding measurement and image-regression guardrails.
- Improve OBJ/GLB import compatibility for Blender, Khronos, and common asset sources.

## Non-Goals

- CUDA/GPU migration is not part of this roadmap.
- Blender plugin integration is deferred.
- Full glTF specification coverage is deferred; this roadmap targets the most useful material fields first.
- Large binary asset ingestion without license review is not allowed.

## Stage 1: Baseline Commit for PBR and Emissive Fixes

Commit the existing approved working-tree changes as the baseline before starting new work.

Scope:
- PBR exposes `f()` and `pdf()` and uses the non-delta BRDF path.
- Direct light uses material BRDF instead of base color only.
- Emissive mesh sampling includes only emissive triangles, grouped by emissive material.
- Emissive lights are selected by area, and light PDFs match the sampling strategy.
- `scenes/mirror_glass_water.json` is added as an acceptance scene.
- Regression tests cover PBR BRDF/PDF, mixed emissive mesh sampling, and the new scene.
- CTest runs from the source root so scene paths resolve consistently.

Verification:
- `./build.sh`
- `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`
- `./run.sh --scene scenes/mirror_glass_water.json --samples 512 --out /tmp/mirror_glass_water_hq.ppm`

Commit:
- `Fix PBR and emissive light sampling consistency`

## Stage 2: Render Quality and Display Output

Improve final image presentation while keeping the underlying path tracer architecture stable.

Scope:
- Add tone mapping with `none`, `reinhard`, and `aces` modes.
- Add exposure control through `--exposure <value>` and `image.exposure`.
- Preserve old PPM output behavior.
- Add native PNG output when the output path ends in `.png`, using `stb_image_write.h`.
- Refactor display conversion into a single output path: sample average, exposure, tone map, gamma correction, write image.
- Keep old scenes mostly unchanged; update documentation and selected showcase scene recommendations.

Testing:
- Unit/regression coverage for tone mapping and exposure behavior.
- Output dispatch coverage for `.ppm` and `.png`.
- Smoke render `mirror_glass_water` to PNG.

Commit:
- `Improve display output with tone mapping and PNG support`

## Stage 3: Official Complex Test Assets

Introduce a small, license-reviewed set of Khronos glTF Sample Assets to test realistic GLB import and material behavior.

Scope:
- Prefer Khronos glTF Sample Assets because each model provides source, description, and license metadata.
- First-party candidates:
  - `WaterBottle.glb` for metallic/roughness, UVs, and textures.
  - `TransmissionTest.glb` for `KHR_materials_transmission`.
  - One additional transmission or glass comparison asset only if license and size are acceptable.
- Store direct CC0 assets under `models/khronos/`.
- For CC BY or large assets, provide `scripts/fetch_khronos_assets.sh` and do not commit binaries unless explicitly approved.
- Add `models/khronos/LICENSES.md` with per-asset source URLs, credits, and license summaries.
- Add one scene per asset, using auto camera, ground, and `override_material: false`.

Testing:
- Asset scenes load without crashes.
- Manual smoke renders produce PNG output.
- CI/regression tests do not require network access.

Commit:
- `Add Khronos GLB acceptance assets and scenes`

## Stage 4: Golden Image Regression

Add deterministic image regression tests to catch unintended visual changes.

Scope:
- Add `--seed <n>` and optional `image.seed`.
- Golden tests use `--threads 1` for deterministic random ordering.
- Add small golden scenes at 128x72 or 160x90 with low sample counts.
- Store scripts and data under:

```text
tests/golden/
  scenes/
  images/
  compare_images.py
```

- Add `--update` behavior to regenerate golden images only when a visual change is accepted.
- Add CTest integration as a separate `golden_image_tests` target.

Comparison:
- Use mean absolute error and max absolute error thresholds.
- Start with tolerant thresholds, then tighten if deterministic output is stable.

Commit:
- `Add deterministic golden image regression tests`

## Stage 5: Performance Measurement and Optimization

Measure before optimizing, then make incremental CPU-side improvements that preserve visual output.

Scope:
- Add `--stats` output for load time, render time, total time, image size, samples, depth, threads, primitive count, and available BVH stats.
- Add `scripts/benchmark.sh`.
- Benchmark default, PBR, mirror/glass/water, OBJ, and Khronos scenes with one thread and hardware thread counts.
- Optimize per-thread RNG and seed handling.
- Consider tile-based scheduling after RNG determinism is under control.
- Precompute scene emissive total area instead of recomputing it at each shading point.
- Add BVH statistics such as node count, leaf count, and max depth before changing BVH construction.

Testing:
- Benchmarks produce stable, comparable output.
- Regression and golden tests remain green.
- Multi-thread render time is not worse than the current baseline on representative scenes.

Commits:
- `Add render benchmarking and stats output`
- `Optimize render scheduling and random sampling`

## Stage 6: OBJ and GLB Import Enhancements

Improve compatibility with Blender exports, Khronos assets, and common downloaded models.

OBJ Scope:
- Parse `.mtl` files for `Kd` and `map_Kd`.
- Map `Kd` to Lambertian or PBR base color.
- Map `map_Kd` to albedo texture with correct relative path resolution.
- Keep `override_material: true` as an explicit override.
- Treat `Ks` and `Ns` conservatively; do not over-map them into metalness unless validated.

GLB Scope:
- Add support for `metallicRoughnessTexture`.
- Add support for `normalTexture`.
- Add `emissiveFactor` and, if straightforward, `emissiveTexture`.
- Read `doubleSided` and define a consistent backface policy.
- Read `KHR_materials_volume` attenuation color and distance for future or immediate dielectric absorption.
- Warn and gracefully degrade unsupported extensions.

Testing:
- Existing scenes continue to load and render.
- A small OBJ+MTL fixture validates diffuse color and texture import.
- Khronos scenes validate GLB texture and transmission paths.
- Golden image tests guard against accidental import regressions.

Commits:
- `Add basic OBJ MTL material loading`
- `Extend GLB material texture and emission mapping`
- `Document asset import compatibility`

## Cross-Stage Rules

- Each stage gets its own verification before commit.
- Do not mix unrelated refactors into stage commits.
- Any change that affects rendered pixels must either add or update golden image evidence.
- New binary assets require source URL, license, and credits in the repo.
- Network download scripts are allowed, but automated tests must not depend on network access.
- README must remain accurate after each stage.

## Success Criteria

After this roadmap, the renderer can produce display-ready PNG output, run deterministic image regression tests, render official Khronos GLB acceptance scenes, provide baseline performance stats, and import more common OBJ/GLB material features while preserving the current CPU renderer architecture.
