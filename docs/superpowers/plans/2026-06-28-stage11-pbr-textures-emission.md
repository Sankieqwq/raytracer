# Stage 11: PBR Materials, Textures & Emission Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add PBR Cook-Torrance materials (GGX+Smith+Schlick with importance sampling), a tag-dispatch POD texture system (solid + image, supporting albedo/metallic/roughness/normal maps), and an emissive material for area lights. The `scatter` interface gains an `emission` output parameter; `ray_color` adds emission to the recursive radiance.

**Architecture:** Header-only C++17. New `Texture` POD (`render/texture.h`) with `texture_sample()` free function and `load_image_texture()` using stb_image. `HitRecord` gains `tangent`/`has_tangent` for normal mapping. `Material::scatter` signature changes to add `Color& emission`. New `PBR` and `Emissive` classes; existing Lambertian/Metal/Dielectric updated to set `emission = 0`. Sphere/triangle/mesh `hit()` compute UV + tangent. `scene.h` parses `pbr`/`emissive` materials with `_map` texture fields.

**Tech Stack:** C++17, CMake 3.10, stb_image (header-only third-party), no other new deps.

**Spec:** `docs/superpowers/specs/2026-06-28-stage11-pbr-textures-emission-design.md`

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `third_party/stb_image.h` | Create | stb_image single-header library |
| `include/raytracer/render/texture.h` | Create | `Texture` POD + `texture_sample` + `load_image_texture` |
| `include/raytracer/geometry/hittable.h` | Modify | `HitRecord` gains `tangent`/`has_tangent` |
| `include/raytracer/geometry/sphere.h` | Modify | `hit()` computes UV + tangent |
| `include/raytracer/geometry/triangle.h` | Modify | `hit()` computes tangent from UV |
| `include/raytracer/geometry/triangle_mesh.h` | Modify | `hit()` computes tangent from UV |
| `include/raytracer/material/material.h` | Modify | `scatter` adds `emission` param; new `PBR` + `Emissive`; existing materials set `emission=0` |
| `src/main.cpp` | Modify | `STB_IMAGE_IMPLEMENTATION`; `ray_color` adds emission |
| `include/raytracer/scene/scene.h` | Modify | `parse_material` supports `pbr`/`emissive` + texture loading helpers |
| `textures/checkerboard.png` | Create | Test texture (procedural) |
| `scenes/pbr_test.json` | Create | PBR acceptance scene |
| `scenes/texture_test.json` | Create | Texture mapping acceptance scene |
| `README.md` | Modify | Update status and add PBR/texture section |

---

## Task 1: Add stb_image to third_party

**Files:**
- Create: `third_party/stb_image.h`

- [ ] **Step 1: Copy the library**

```bash
cp /tmp/stb_image.h third_party/stb_image.h
wc -l third_party/stb_image.h
```
Expected: ~7988 lines

- [ ] **Step 2: Commit**

```bash
git add third_party/stb_image.h
git commit -m "Stage 11 task 1: add stb_image to third_party"
```

---

## Task 2: Create `texture.h`

**Files:**
- Create: `include/raytracer/render/texture.h`

- [ ] **Step 1: Write the file**

Create `include/raytracer/render/texture.h`:

```cpp
// Module D: render -- texture (POD tag dispatch, GPU-friendly)
#ifndef RT_TEXTURE_H
#define RT_TEXTURE_H

#include "raytracer/math/vec3.h"
#include <vector>
#include <string>
#include <stdexcept>
#include <cmath>
#include "stb_image.h"

enum class TextureType { Solid, Image };

struct Texture {
    TextureType type = TextureType::Solid;
    Color solid_color;              // Solid
    std::vector<float> pixels;      // Image: RGB float row-major, [0,1]
    int width = 0;
    int height = 0;

    Texture() = default;
    explicit Texture(const Color& c) : type(TextureType::Solid), solid_color(c) {}
};

// Sample with repeat wrapping. Returns RGB Color in [0,1].
inline Color texture_sample(const Texture& tex, double u, double v) {
    if (tex.type == TextureType::Solid) return tex.solid_color;
    if (tex.width == 0 || tex.height == 0) return Color(0, 0, 0);
    double uu = u - std::floor(u);
    double vv = v - std::floor(v);
    int x = static_cast<int>(uu * tex.width);
    int y = static_cast<int>(vv * tex.height);
    if (x < 0) x = 0; else if (x >= tex.width) x = tex.width - 1;
    if (y < 0) y = 0; else if (y >= tex.height) y = tex.height - 1;
    int idx = (y * tex.width + x) * 3;
    return Color(tex.pixels[idx], tex.pixels[idx + 1], tex.pixels[idx + 2]);
}

// Load image as RGB float texture. stbi_load forces 3 channels.
inline Texture load_image_texture(const std::string& path) {
    Texture tex;
    tex.type = TextureType::Image;
    int w, h, ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 3);
    if (!data) throw std::runtime_error("Failed to load image: " + path);
    tex.width = w;
    tex.height = h;
    tex.pixels.resize(static_cast<size_t>(w) * h * 3);
    for (size_t i = 0; i < tex.pixels.size(); i++) {
        tex.pixels[i] = static_cast<float>(data[i]) / 255.0f;
    }
    stbi_image_free(data);
    return tex;
}

#endif
```

- [ ] **Step 2: Add STB_IMAGE_IMPLEMENTATION to main.cpp**

In `src/main.cpp`, after the `TINYOBJLOADER_IMPLEMENTATION` block, add:

```cpp
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
```

- [ ] **Step 3: Compile**

Run: `cmake --build build 2>&1 | tail -5`
Expected: `Built target raytracer` (nothing includes texture.h yet, but stb_image impl compiles)

- [ ] **Step 4: Commit**

```bash
git add include/raytracer/render/texture.h src/main.cpp
git commit -m "Stage 11 task 2: add Texture POD + stb_image implementation"
```

---

## Task 3: Extend `HitRecord` with tangent

**Files:**
- Modify: `include/raytracer/geometry/hittable.h`

- [ ] **Step 1: Add tangent fields**

Replace the `HitRecord` struct with:

```cpp
struct HitRecord {
    Point3 p;
    Vec3 normal;
    Vec3 tangent;
    double t = 0;
    double u = 0;
    double v = 0;
    bool front_face = true;
    bool has_tangent = false;
    Material* material = nullptr;

    void set_face_normal(const Ray& r, const Vec3& outward_normal) {
        front_face = dot(r.direction, outward_normal) < 0;
        normal = front_face ? outward_normal : -outward_normal;
    }
};
```

- [ ] **Step 2: Compile**

Run: `cmake --build build 2>&1 | tail -5`
Expected: `Built target raytracer` (existing materials unaffected; tangent defaults to 0, has_tangent=false)

- [ ] **Step 3: Verify backward compat**

Run: `./build/raytracer --scene scenes/default.json --out /tmp/s11_t3.ppm 2>&1 | tail -2`
Expected: renders correctly

- [ ] **Step 4: Commit**

```bash
git add include/raytracer/geometry/hittable.h
git commit -m "Stage 11 task 3: add tangent/has_tangent to HitRecord"
```

---

## Task 4: Compute UV + tangent in sphere

**Files:**
- Modify: `include/raytracer/geometry/sphere.h`

- [ ] **Step 1: Add UV + tangent computation in `hit()`**

In `Sphere::hit()`, after `rec.set_face_normal(r, outward);` and before `rec.material = material;`, add:

```cpp
        // UV: spherical coordinates
        double theta = std::acos(std::clamp(-oc.y / radius, -1.0, 1.0));
        double phi = std::atan2(-oc.z, oc.x) + pi;
        rec.u = phi / (2 * pi);
        rec.v = theta / pi;
        // Tangent: along longitude (dp/du direction)
        rec.tangent = Vec3(-oc.z, 0, oc.x).normalized();
        rec.has_tangent = true;
```

Also add `#include <cmath>` and `#include "raytracer/math/util.h"` at top of sphere.h if not present (for `pi`).

- [ ] **Step 2: Compile**

Run: `cmake --build build 2>&1 | tail -5`
Expected: `Built target raytracer`

- [ ] **Step 3: Verify**

Run: `./build/raytracer --scene scenes/default.json --out /tmp/s11_t4.ppm 2>&1 | tail -2`
Expected: renders correctly (sphere UV/tangent computed but unused yet)

- [ ] **Step 4: Commit**

```bash
git add include/raytracer/geometry/sphere.h
git commit -m "Stage 11 task 4: sphere hit computes UV + tangent"
```

---

## Task 5: Compute tangent in triangle and mesh

**Files:**
- Modify: `include/raytracer/geometry/triangle.h`
- Modify: `include/raytracer/geometry/triangle_mesh.h`

- [ ] **Step 1: Add tangent to `Triangle::hit`**

In `include/raytracer/geometry/triangle.h`, in the `Triangle::hit()` method, after the UV interpolation block and before `rec.material = material;`, add:

```cpp
        // Tangent from UV (standard derivation)
        if (has_uvs) {
            double du1 = uv1.x - uv0.x;
            double dv1 = uv1.y - uv0.y;
            double du2 = uv2.x - uv0.x;
            double dv2 = uv2.y - uv0.y;
            double det = du1 * dv2 - du2 * dv1;
            Vec3 edge1 = v1 - v0;
            Vec3 edge2 = v2 - v0;
            if (std::fabs(det) > 1e-10) {
                double inv = 1.0 / det;
                rec.tangent = (inv * (dv2 * edge1 - dv1 * edge2)).normalized();
            } else {
                rec.tangent = cross(Vec3(0, 1, 0), outward_normal).normalized();
                if (rec.tangent.length_squared() < 1e-10)
                    rec.tangent = cross(Vec3(1, 0, 0), outward_normal).normalized();
            }
            rec.has_tangent = true;
        }
```

Note: `outward_normal` is the variable name used in `Triangle::hit` for the geometric normal before `set_face_normal`.

- [ ] **Step 2: Add tangent to `TriangleMesh::hit`**

In `include/raytracer/geometry/triangle_mesh.h`, in the `TriangleMesh::hit()` inner loop, after the UV block and before `tmp.material = material_per_tri[i];`, add:

```cpp
            if (!uvs.empty()) {
                Vec2 uv0_ = uvs[i0], uv1_ = uvs[i1], uv2_ = uvs[i2];
                double du1 = uv1_.x - uv0_.x;
                double dv1 = uv1_.y - uv0_.y;
                double du2 = uv2_.x - uv0_.x;
                double dv2 = uv2_.y - uv0_.y;
                double det = du1 * dv2 - du2 * dv1;
                Vec3 edge1 = vertices[i1] - vertices[i0];
                Vec3 edge2 = vertices[i2] - vertices[i0];
                if (std::fabs(det) > 1e-10) {
                    double inv = 1.0 / det;
                    tmp.tangent = (inv * (dv2 * edge1 - dv1 * edge2)).normalized();
                } else {
                    tmp.tangent = cross(Vec3(0, 1, 0), outward).normalized();
                    if (tmp.tangent.length_squared() < 1e-10)
                        tmp.tangent = cross(Vec3(1, 0, 0), outward).normalized();
                }
                tmp.has_tangent = true;
            }
```

Note: `outward` is the variable name used in `TriangleMesh::hit` for the geometric normal.

- [ ] **Step 3: Compile**

Run: `cmake --build build 2>&1 | tail -5`
Expected: `Built target raytracer`

- [ ] **Step 4: Verify**

Run: `./build/raytracer --scene scenes/triangle_test.json --out /tmp/s11_t5.ppm 2>&1 | tail -2`
Expected: renders correctly

- [ ] **Step 5: Commit**

```bash
git add include/raytracer/geometry/triangle.h include/raytracer/geometry/triangle_mesh.h
git commit -m "Stage 11 task 5: triangle/mesh compute tangent from UV"
```

---

## Task 6: Update `Material::scatter` signature with emission

**Files:**
- Modify: `include/raytracer/material/material.h`

- [ ] **Step 1: Update base class and existing materials**

In `include/raytracer/material/material.h`, change the `Material` base class and all three existing materials (`Lambertian`, `Metal`, `Dielectric`) to add the `emission` output parameter and set it to 0.

For `Material`:
```cpp
class Material {
public:
    virtual ~Material() = default;
    virtual bool scatter(const Ray& r_in, const HitRecord& rec,
                         Color& attenuation, Ray& scattered,
                         Color& emission) const = 0;
};
```

For `Lambertian::scatter`, change signature and add `emission = Color(0, 0, 0);` at the start:
```cpp
    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered,
                 Color& emission) const override {
        emission = Color(0, 0, 0);
        (void)r_in;
        Vec3 dir = rec.normal + random_unit_vector();
        if (dir.length_squared() < 1e-8) dir = rec.normal;
        scattered = Ray(rec.p, dir);
        attenuation = albedo;
        return true;
    }
```

For `Metal::scatter`:
```cpp
    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered,
                 Color& emission) const override {
        emission = Color(0, 0, 0);
        Vec3 reflected = reflect(r_in.direction.normalized(), rec.normal);
        scattered = Ray(rec.p, reflected + fuzz * random_in_unit_sphere());
        attenuation = albedo;
        return dot(scattered.direction, rec.normal) > 0;
    }
```

For `Dielectric::scatter`:
```cpp
    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered,
                 Color& emission) const override {
        emission = Color(0, 0, 0);
        attenuation = Color(1.0, 1.0, 1.0);
        // ... rest unchanged
```

- [ ] **Step 2: Add PBR class**

Append before the final `#endif` in `material.h`:

```cpp
// PBR metal-roughness with Cook-Torrance BRDF (GGX + Smith + Schlick)
class PBR : public Material {
public:
    Texture albedo;
    Texture metallic;
    Texture roughness;
    Texture normal;
    bool has_normal_map = false;

    PBR(const Texture& albedo_tex, double metallic_val, double roughness_val)
        : albedo(albedo_tex),
          metallic(Texture{TextureType::Solid, Color(metallic_val, 0, 0), {}, 0, 0}),
          roughness(Texture{TextureType::Solid, Color(roughness_val, 0, 0), {}, 0, 0}),
          normal(Texture{TextureType::Solid, Color(0.5, 0.5, 1.0), {}, 0, 0}) {}

    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered,
                 Color& emission) const override {
        emission = Color(0, 0, 0);

        Color base = texture_sample(albedo, rec.u, rec.v);
        double met = texture_sample(metallic, rec.u, rec.v).x;
        double rough = std::max(0.001, texture_sample(roughness, rec.u, rec.v).x);

        Vec3 n = rec.normal;
        if (has_normal_map && rec.has_tangent) {
            Color ns = texture_sample(normal, rec.u, rec.v);
            Vec3 tn = Vec3(2*ns.x - 1, 2*ns.y - 1, 2*ns.z - 1).normalized();
            Vec3 T = rec.tangent.normalized();
            Vec3 B = cross(n, T).normalized();
            n = (T * tn.x + B * tn.y + n * tn.z).normalized();
            if (dot(n, rec.normal) < 0) n = -n;
        }

        Vec3 v = (-r_in.direction).normalized();
        double n_dot_v = std::max(0.0, dot(n, v));

        Vec3 up = std::fabs(n.x) > 0.9 ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
        Vec3 Tbn_T = cross(up, n).normalized();
        Vec3 Tbn_B = cross(n, Tbn_T);

        double r1 = random_double();
        double r2 = random_double();
        double alpha = rough * rough;
        double phi = 2 * pi * r1;
        double cos_theta = std::sqrt((1 - r2) / (1 + (alpha * alpha - 1) * r2));
        double sin_theta = std::sqrt(1 - cos_theta * cos_theta);
        Vec3 h_local(sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta);
        Vec3 h = (Tbn_T * h_local.x + Tbn_B * h_local.y + n * h_local.z).normalized();

        Vec3 scattered_dir = (2 * dot(v, h) * h - v).normalized();
        double n_dot_l = std::max(0.0, dot(n, scattered_dir));
        double n_dot_h = std::max(0.0, dot(n, h));
        double v_dot_h = std::max(0.0, dot(v, h));

        if (n_dot_l <= 0 || n_dot_v <= 0) return false;

        double a2 = alpha * alpha;
        double denom_d = n_dot_h * n_dot_h * (a2 - 1) + 1;
        double D = a2 / (pi * denom_d * denom_d);

        auto G1 = [&](double ndx) {
            double sq = std::sqrt(a2 + (1 - a2) * ndx * ndx);
            return 2 * ndx / (ndx + sq);
        };
        double G = G1(n_dot_l) * G1(n_dot_v);

        Color F0 = (1 - met) * Color(0.04, 0.04, 0.04) + met * base;
        Color F = F0 + (Color(1, 1, 1) - F0) * std::pow(1 - v_dot_h, 5);

        Color kD = (Color(1, 1, 1) - F) * (1 - met);
        Color diffuse = kD * base / pi;
        Color specular = F * (D * G) / (4 * n_dot_l * n_dot_v);
        Color brdf = diffuse + specular;

        double pdf = (D * n_dot_h) / (4 * v_dot_h);
        if (pdf <= 0) return false;

        attenuation = brdf * n_dot_l / pdf;
        scattered = Ray(rec.p, scattered_dir);
        return true;
    }
};

// Emissive material (area light)
class Emissive : public Material {
public:
    Color emission;
    Emissive(const Color& e) : emission(e) {}

    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered,
                 Color& emission_out) const override {
        (void)r_in; (void)rec; (void)scattered; (void)attenuation;
        emission_out = emission;
        return false;
    }
};
```

Also add `#include "raytracer/render/texture.h"` and `#include "raytracer/math/util.h"` to the includes at the top of `material.h`.

- [ ] **Step 3: Update `ray_color` in main.cpp**

In `src/main.cpp`, replace the `ray_color` function with:

```cpp
Color ray_color(const Ray& r, const Hittable& world, int depth) {
    if (depth <= 0) return Color(0, 0, 0);

    HitRecord rec;
    if (world.hit(r, 0.001, infinity, rec)) {
        Ray scattered;
        Color attenuation, emission;
        if (rec.material && rec.material->scatter(r, rec, attenuation, scattered, emission)) {
            return emission + attenuation * ray_color(scattered, world, depth - 1);
        }
        return emission;
    }

    Vec3 unit_dir = r.direction.normalized();
    double t = 0.5 * (unit_dir.y + 1.0);
    return (1 - t) * Color(1.0, 1.0, 1.0) + t * Color(0.5, 0.7, 1.0);
}
```

- [ ] **Step 4: Compile**

Run: `cmake --build build 2>&1 | tail -10`
Expected: `Built target raytracer`. Any errors about `scatter` signature mismatches indicate a place that wasn't updated — fix it.

- [ ] **Step 5: Verify backward compat (six scenes)**

Run:
```bash
./build/raytracer --scene scenes/default.json --out /tmp/s11_t6_default.ppm 2>&1 | tail -2
./build/raytracer --scene scenes/three_balls.json --out /tmp/s11_t6_three.ppm 2>&1 | tail -2
./build/raytracer --scene scenes/triangle_test.json --out /tmp/s11_t6_tri.ppm 2>&1 | tail -2
```
Expected: all three render successfully

- [ ] **Step 6: Commit**

```bash
git add include/raytracer/material/material.h src/main.cpp
git commit -m "Stage 11 task 6: scatter+emission signature, PBR + Emissive, ray_color emission"
```

---

## Task 7: Extend `scene.h` parse_material with pbr/emissive + textures

**Files:**
- Modify: `include/raytracer/scene/scene.h`

- [ ] **Step 1: Add include and texture helpers**

Add `#include "raytracer/render/texture.h"` to the includes at the top.

After the `to_vec2` helper, add:

```cpp
inline Texture parse_texture_color(const JsonValue& m, const std::string& base_key,
                                    const Color& default_color,
                                    const std::filesystem::path& scene_dir) {
    std::string map_key = base_key + "_map";
    if (m.has(map_key)) {
        std::string path = resolve_asset_path(scene_dir, m.at(map_key).strVal);
        return load_image_texture(path);
    }
    if (m.has(base_key)) {
        return Texture(to_vec3(m.at(base_key)));
    }
    return Texture(default_color);
}

inline Texture parse_texture_scalar(const JsonValue& m, const std::string& base_key,
                                     double default_val,
                                     const std::filesystem::path& scene_dir) {
    std::string map_key = base_key + "_map";
    if (m.has(map_key)) {
        std::string path = resolve_asset_path(scene_dir, m.at(map_key).strVal);
        return load_image_texture(path);
    }
    double v = m.has(base_key) ? m.at(base_key).numVal : default_val;
    return Texture(Color(v, 0, 0));
}
```

- [ ] **Step 2: Add pbr/emissive branches to parse_material**

In `parse_material`, before the `throw std::runtime_error("Unknown material type: " + type);` line, add:

```cpp
    } else if (type == "pbr") {
        // Note: parse_material doesn't have scene_dir; use a fallback
        // We need scene_dir passed in. For now, use relative path resolution.
        // Workaround: pass scene_dir via a global or change signature.
        // Actually, parse_material is called from load_scene which has scene_dir.
        // Let's add scene_dir parameter.
        throw std::runtime_error("pbr material requires scene_dir context (see step 3)");
    } else if (type == "emissive") {
        mat = std::make_unique<Emissive>(to_vec3(m.at("emission")));
    }
```

- [ ] **Step 3: Pass scene_dir to parse_material**

The `parse_material` function needs `scene_dir` for texture loading. Change its signature and the `ensure_material` helper:

Find `inline Material* parse_material(const JsonValue& m, Scene& scene)` and change to:
```cpp
inline Material* parse_material(const JsonValue& m, Scene& scene,
                                const std::filesystem::path& scene_dir) {
```

Find `inline Material* ensure_material(const JsonValue& obj, Scene& scene)` and change to:
```cpp
inline Material* ensure_material(const JsonValue& obj, Scene& scene,
                                 const std::filesystem::path& scene_dir) {
    if (obj.has("material")) return parse_material(obj.at("material"), scene, scene_dir);
    // ... fallback unchanged (but call parse_material with scene_dir)
    return parse_material(fallback, scene, scene_dir);
}
```

In `load_scene`, update all calls to `ensure_material` and `parse_material` to pass `scene_dir`:
- `Material* mat = ensure_material(obj, scene);` → `Material* mat = ensure_material(obj, scene, scene_dir);`
- There are multiple such calls in the sphere/mesh/triangle/triangles branches.

- [ ] **Step 4: Complete the pbr branch**

Replace the placeholder pbr branch with:

```cpp
    } else if (type == "pbr") {
        Texture albedo_tex = parse_texture_color(m, "albedo", Color(0.8, 0.8, 0.8), scene_dir);
        Texture metallic_tex = parse_texture_scalar(m, "metallic", 0.0, scene_dir);
        Texture roughness_tex = parse_texture_scalar(m, "roughness", 0.5, scene_dir);
        double met_def = m.has("metallic") ? m.at("metallic").numVal : 0.0;
        double rough_def = m.has("roughness") ? m.at("roughness").numVal : 0.5;
        auto pbr = std::make_unique<PBR>(albedo_tex, met_def, rough_def);
        pbr->albedo = albedo_tex;
        pbr->metallic = metallic_tex;
        pbr->roughness = roughness_tex;
        if (m.has("normal_map")) {
            std::string path = resolve_asset_path(scene_dir, m.at("normal_map").strVal);
            pbr->normal = load_image_texture(path);
            pbr->has_normal_map = true;
        }
        mat = std::move(pbr);
    }
```

- [ ] **Step 5: Compile**

Run: `cmake --build build 2>&1 | tail -15`
Expected: `Built target raytracer`

- [ ] **Step 6: Verify backward compat**

Run:
```bash
./build/raytracer --scene scenes/default.json --out /tmp/s11_t7.ppm 2>&1 | tail -2
./build/raytracer --scene scenes/mark.json --out /tmp/s11_t7_mark.ppm 2>&1 | tail -2
```
Expected: both render successfully

- [ ] **Step 7: Commit**

```bash
git add include/raytracer/scene/scene.h
git commit -m "Stage 11 task 7: parse pbr/emissive materials + texture loading"
```

---

## Task 8: Generate test texture and create acceptance scenes

**Files:**
- Create: `textures/checkerboard.png`
- Create: `scenes/pbr_test.json`
- Create: `scenes/texture_test.json`

- [ ] **Step 1: Generate checkerboard PNG**

Use Python to generate a 128x128 checkerboard PPM, then convert to PNG with sips:

```bash
mkdir -p textures
python3 -c "
w, h = 128, 128
cell = 16
lines = ['P3', f'{w} {h}', '255']
for y in range(h):
    for x in range(w):
        c = 255 if ((x // cell) + (y // cell)) % 2 == 0 else 0
        lines.append(f'{c} {c} {c}')
with open('textures/checkerboard.ppm', 'w') as f:
    f.write('\n'.join(lines) + '\n')
"
sips -s format png textures/checkerboard.ppm --out textures/checkerboard.png 2>&1 | tail -1
rm textures/checkerboard.ppm
```

- [ ] **Step 2: Create pbr_test.json**

```json
{
    "image": {
        "width": 600, "height": 400, "samples": 32, "max_depth": 32,
        "output": "pbr_test.ppm"
    },
    "camera": {
        "lookfrom": [0, 0, 5], "lookat": [0, 0, 0], "vup": [0, 1, 0],
        "vfov": 40, "aperture": 0.0, "focus_dist": 5.0
    },
    "objects": [
        {
            "type": "sphere", "center": [-1.5, 0, 0], "radius": 1,
            "material": { "type": "pbr", "albedo": [0.8, 0.2, 0.2], "metallic": 1.0, "roughness": 0.05 }
        },
        {
            "type": "sphere", "center": [0, 0, 0], "radius": 1,
            "material": { "type": "pbr", "albedo": [0.7, 0.7, 0.7], "metallic": 0.5, "roughness": 0.4 }
        },
        {
            "type": "sphere", "center": [1.5, 0, 0], "radius": 1,
            "material": { "type": "pbr", "albedo": [0.2, 0.8, 0.2], "metallic": 0.0, "roughness": 0.8 }
        },
        {
            "type": "sphere", "center": [0, 3, 0], "radius": 1,
            "material": { "type": "emissive", "emission": [8, 8, 8] }
        },
        {
            "type": "sphere", "center": [0, -100.5, 0], "radius": 100,
            "material": { "type": "lambertian", "albedo": [0.3, 0.3, 0.3] }
        }
    ]
}
```

- [ ] **Step 3: Create texture_test.json**

```json
{
    "image": {
        "width": 400, "height": 400, "samples": 16, "max_depth": 16,
        "output": "texture_test.ppm"
    },
    "camera": {
        "lookfrom": [0, 0, 3], "lookat": [0, 0, 0], "vup": [0, 1, 0],
        "vfov": 40, "aperture": 0.0, "focus_dist": 3.0
    },
    "objects": [
        {
            "type": "sphere", "center": [0, 0, 0], "radius": 1,
            "material": {
                "type": "pbr",
                "albedo_map": "../textures/checkerboard.png",
                "metallic": 0.0, "roughness": 0.8
            }
        }
    ]
}
```

- [ ] **Step 4: Render pbr_test**

Run: `./build/raytracer --scene scenes/pbr_test.json > /tmp/pbr.log 2>&1; tail -3 /tmp/pbr.log`
Expected: renders successfully, `Wrote pbr_test.ppm`

- [ ] **Step 5: Render texture_test**

Run: `./build/raytracer --scene scenes/texture_test.json > /tmp/tex.log 2>&1; tail -3 /tmp/tex.log`
Expected: renders successfully, `Wrote texture_test.ppm`

- [ ] **Step 6: Verify outputs**

```bash
python3 << 'EOF'
import os
for name, path in [('pbr', 'pbr_test.ppm'), ('tex', 'texture_test.ppm')]:
    if not os.path.exists(path):
        print(f'{name}: MISSING'); continue
    with open(path) as f:
        parts = f.read().split('\n', 3)
    w, h = map(int, parts[1].split())
    px = list(map(int, parts[3].split()))
    non_black = sum(1 for v in px if v > 10)
    print(f'{name}: {w}x{h}, non-black={100*non_black/len(px):.1f}%')
EOF
```
Expected: both have meaningful non-black content (>50%)

- [ ] **Step 7: Commit**

```bash
git add textures/checkerboard.png scenes/pbr_test.json scenes/texture_test.json
git commit -m "Stage 11 task 8: add test texture + pbr/texture acceptance scenes"
```

---

## Task 9: Update README

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update status section**

Find the "当前实现状态" list and update:

```markdown
- ✅ 阶段 10：BVH 线性化加速（扁平节点 + 栈遍历，GPU 友好）
- ✅ 阶段 11：PBR 材质 + 纹理 + 自发光（Cook-Torrance + GGX 重要性采样）
- ⬜ 后续：CUDA 移植
```

- [ ] **Step 2: Add PBR/texture section**

After the "## BVH 加速结构" section, add:

```markdown

## PBR 材质与纹理（阶段 11）

支持物理合理的金属-粗糙度着色模型。

### 材质类型

| 类型 | 字段 | 说明 |
|------|------|------|
| `lambertian` | `albedo` | 漫反射（保留） |
| `metal` | `albedo`, `fuzz` | 金属反射（保留） |
| `dielectric` | `ior` | 玻璃/水（保留） |
| `pbr` | `albedo`/`metallic`/`roughness` + 各 `_map` + `normal_map` | Cook-Torrance BRDF |
| `emissive` | `emission` | 自发光（面光源） |

### PBR 示例

```json
{
    "type": "pbr",
    "albedo": [0.8, 0.2, 0.2],
    "albedo_map": "../textures/checkerboard.png",
    "metallic": 1.0,
    "roughness": 0.05,
    "normal_map": "../textures/normal.png"
}
```

- 标量值（`albedo`/`metallic`/`roughness`）为默认，`_map` 字段加载贴图覆盖
- `normal_map` 通过切线空间（TBN）扰动法线
- BRDF：Cook-Torrance（GGX 法线分布 + Smith 几何项 + Fresnel-Schlick）
- 采样：GGX 重要性采样，提高收敛速度

### 自发光

```json
{ "type": "emissive", "emission": [8, 8, 8] }
```

`emission` 值可 >1，作为面光源参与全局光照。
```

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "Stage 11 task 9: update README with PBR/texture/emission docs"
```

---

## Final Verification

- [ ] **Full clean build**

```bash
rm -rf build
cmake -S . -B build
cmake --build build 2>&1 | tail -3
```
Expected: `Built target raytracer`

- [ ] **Render all scenes**

```bash
./build/raytracer --scene scenes/default.json --out /tmp/f1.ppm > /dev/null 2>&1
./build/raytracer --scene scenes/three_balls.json --out /tmp/f2.ppm > /dev/null 2>&1
./build/raytracer --scene scenes/triangle_test.json --out /tmp/f3.ppm > /dev/null 2>&1
./build/raytracer --scene scenes/mark.json --out /tmp/f4.ppm > /dev/null 2>&1
./build/raytracer --scene scenes/bunny_test.json --out /tmp/f5.ppm > /dev/null 2>&1
./build/raytracer --scene scenes/no_normal_obj.json --out /tmp/f6.ppm > /dev/null 2>&1
./build/raytracer --scene scenes/pbr_test.json > /dev/null 2>&1
./build/raytracer --scene scenes/texture_test.json > /dev/null 2>&1
```
Expected: all render successfully

- [ ] **Git log check**

Run: `git log --oneline -12`
Expected: see Stage 11 commits on top.

---

## Self-Review Notes

- **Spec coverage**: All 9 decisions mapped. Decision 1 (stb_image) → Task 1. Decision 2 (POD texture) → Task 2. Decision 3 (Cook-Torrance) → Task 6. Decision 4 (Emissive) → Task 6. Decision 5 (scatter+emission) → Task 6. Decision 6 (normal map) → Tasks 3-6 (tangent + TBN). Decision 7 (JSON format) → Task 7. Decision 8 (ray_color) → Task 6. Decision 9 (two scenes) → Task 8.
- **Type consistency**: `scatter` signature consistent across base + all 5 materials (Task 6). `Texture` struct (Task 2) used in `PBR` (Task 6) and `parse_texture_*` (Task 7). `texture_sample` defined in Task 2, used in Task 6 PBR.
- **Backward compat**: Tasks 3, 4, 5, 6, 7 each verify existing scenes still render.
- **No placeholders**: All code complete, all commands exact.
- **Risks**: PBR BRDF correctness (Task 6) — if test renders too dark/bright, check `attenuation = brdf * n_dot_l / pdf`. Normal map TBN consistency (Task 5) — fallback when UV degenerate. stb_image channels forced to 3 (Task 2).
