# Stage 8: Triangles & Triangle Meshes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add triangle and triangle-mesh intersection to the raytracer, with UV interpolation, while keeping existing sphere/OBJ scenes working.

**Architecture:** Header-only C++17. Extend `HitRecord` with UV fields, add `Vec2` type, add `AABB::surface_area`, extract a free `triangle_intersect()` function (shared by `Triangle` and the new `TriangleMesh`), and extend JSON scene parsing with `triangle`/`triangles` types. `TriangleMesh` uses SoA layout to align with the upcoming GPU refactor (Stage 12).

**Tech Stack:** C++17, CMake 3.10, header-only, zero external dependencies. Verification is end-to-end (compile + render scene + inspect output), no unit test framework.

**Spec:** `docs/superpowers/specs/2026-06-28-stage8-triangles-and-meshes-design.md`

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `include/raytracer/math/vec3.h` | Modify | Append `Vec2` type + operators |
| `include/raytracer/geometry/aabb.h` | Modify | Add `surface_area()` |
| `include/raytracer/geometry/hittable.h` | Modify | Add `u`/`v` fields to `HitRecord` |
| `include/raytracer/geometry/triangle.h` | Modify | Extract `triangle_intersect()` free fn; add UV fields, constructor, interpolation to `Triangle` |
| `include/raytracer/geometry/triangle_mesh.h` | Create | SoA `TriangleMesh` class |
| `include/raytracer/scene/scene.h` | Modify | Parse `triangle`/`triangles` JSON types |
| `scenes/triangle_test.json` | Create | Acceptance scene: quad (2 tris) + triangle + sphere |
| `src/main.cpp` | Modify (temp) | Add `RT_UV_DEBUG` branch in `ray_color` for UV verification; reverted in Task 9 |
| `scenes/triangle_uv_debug.json` | Create (temp) | UV debug scene; removed in Task 9 |
| `README.md` | Modify | Update "current implementation status" |
| `scripts/` | (exists) | `ppm_to_png.sh` already present for PNG conversion |

---

## Task 1: Add `Vec2` type

**Files:**
- Modify: `include/raytracer/math/vec3.h` (append at end, before `#endif`)

- [ ] **Step 1: Read current end of `vec3.h`**

Run: `tail -10 include/raytracer/math/vec3.h`
Expected: see `gamma2` function then `#endif`

- [ ] **Step 2: Append `Vec2` type**

Add this code immediately before the final `#endif`:

```cpp
// --- Vec2 (for UV coordinates) ---
class Vec2 {
public:
    double x, y;

    Vec2() : x(0), y(0) {}
    Vec2(double x, double y) : x(x), y(y) {}

    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator*=(double t) { x *= t; y *= t; return *this; }
};

inline Vec2 operator+(const Vec2& a, const Vec2& b) { return Vec2(a.x+b.x, a.y+b.y); }
inline Vec2 operator*(double t, const Vec2& a) { return Vec2(t*a.x, t*a.y); }
inline Vec2 operator*(const Vec2& a, double t) { return t * a; }
```

- [ ] **Step 3: Compile to verify no syntax errors**

Run: `cmake --build build 2>&1 | tail -5`
Expected: `Built target raytracer` (no errors; nothing uses Vec2 yet so build unchanged)

- [ ] **Step 4: Commit**

```bash
git add include/raytracer/math/vec3.h
git commit -m "Stage 8 task 1: add Vec2 type for UV coordinates"
```

---

## Task 2: Add `AABB::surface_area()`

**Files:**
- Modify: `include/raytracer/geometry/aabb.h` (add method to `AABB` class, after `longest_axis`)

- [ ] **Step 1: Add `surface_area()` method**

Insert this method into the `AABB` class public section, right after the `longest_axis()` method:

```cpp
    double surface_area() const {
        Vec3 d = maximum - minimum;
        return 2.0 * (d.x * d.y + d.y * d.z + d.z * d.x);
    }
```

- [ ] **Step 2: Compile**

Run: `cmake --build build 2>&1 | tail -5`
Expected: `Built target raytracer`

- [ ] **Step 3: Commit**

```bash
git add include/raytracer/geometry/aabb.h
git commit -m "Stage 8 task 2: add AABB::surface_area for SAH prep"
```

---

## Task 3: Add `u`/`v` fields to `HitRecord`

**Files:**
- Modify: `include/raytracer/geometry/hittable.h:10-21` (HitRecord struct)

- [ ] **Step 1: Edit `HitRecord` struct**

Replace the existing `HitRecord` struct body:

```cpp
struct HitRecord {
    Point3 p;
    Vec3 normal;
    double t = 0;
    double u = 0;
    double v = 0;
    bool front_face = true;
    Material* material = nullptr;

    void set_face_normal(const Ray& r, const Vec3& outward_normal) {
        front_face = dot(r.direction, outward_normal) < 0;
        normal = front_face ? outward_normal : -outward_normal;
    }
};
```

- [ ] **Step 2: Compile**

Run: `cmake --build build 2>&1 | tail -5`
Expected: `Built target raytracer` (default u=0/v=0 keeps existing sphere/OBJ behavior)

- [ ] **Step 3: Verify backward compatibility**

Run: `./build/raytracer --scene scenes/default.json --out /tmp/stage8_default.ppm`
Expected: writes `/tmp/stage8_default.ppm`, output identical to before refactor (visually: blue gradient sky, two diffuse spheres on grey ground)

- [ ] **Step 4: Commit**

```bash
git add include/raytracer/geometry/hittable.h
git commit -m "Stage 8 task 3: add u/v fields to HitRecord"
```

---

## Task 4: Extract `triangle_intersect()` free function

**Files:**
- Modify: `include/raytracer/geometry/triangle.h` (add free function; refactor `Triangle::hit` to call it)

- [ ] **Step 1: Add `triangle_intersect()` free function**

Insert this immediately after the `#include` lines and before the `Triangle` class definition:

```cpp
// Möller–Trumbore ray-triangle intersection.
// On hit, writes t and barycentric coords (b1, b2) to output params.
// Barycentric weights: b0 = 1 - b1 - b2 (at v0), b1 (at v1), b2 (at v2).
// Returns false for rays parallel to triangle or hits outside [t_min, t_max].
inline bool triangle_intersect(const Ray& r, double t_min, double t_max,
                               const Point3& v0, const Point3& v1, const Point3& v2,
                               double& t, double& b1, double& b2) {
    const double eps = 1e-8;
    Vec3 edge1 = v1 - v0;
    Vec3 edge2 = v2 - v0;
    Vec3 pvec = cross(r.direction, edge2);
    double det = dot(edge1, pvec);

    if (std::fabs(det) < eps) return false;
    double inv_det = 1.0 / det;

    Vec3 tvec = r.origin - v0;
    b1 = dot(tvec, pvec) * inv_det;
    if (b1 < 0.0 || b1 > 1.0) return false;

    Vec3 qvec = cross(tvec, edge1);
    b2 = dot(r.direction, qvec) * inv_det;
    if (b2 < 0.0 || (b1 + b2) > 1.0) return false;

    t = dot(edge2, qvec) * inv_det;
    if (t < t_min || t > t_max) return false;
    return true;
}
```

- [ ] **Step 2: Refactor `Triangle::hit` to call `triangle_intersect`**

Replace the entire `Triangle::hit` method body with:

```cpp
    bool hit(const Ray& r, double t_min, double t_max,
             HitRecord& rec) const override {
        double t, b1, b2;
        if (!triangle_intersect(r, t_min, t_max, v0, v1, v2, t, b1, b2))
            return false;

        double b0 = 1.0 - b1 - b2;
        rec.t = t;
        rec.p = r.at(t);
        Vec3 outward_normal;
        if (has_vertex_normals) {
            outward_normal = (b0 * n0 + b1 * n1 + b2 * n2).normalized();
        } else {
            outward_normal = cross(v1 - v0, v2 - v0).normalized();
        }
        rec.set_face_normal(r, outward_normal);
        if (has_uvs) {
            Vec2 uv = b0 * uv0 + b1 * uv1 + b2 * uv2;
            rec.u = uv.x;
            rec.v = uv.y;
        } else {
            rec.u = b1;
            rec.v = b2;
        }
        rec.material = material;
        return true;
    }
```

- [ ] **Step 3: Add UV fields and constructor to `Triangle` class**

Replace the existing `Triangle` class field section and constructors with:

```cpp
class Triangle : public Hittable {
public:
    Point3 v0, v1, v2;
    Vec3 n0, n1, n2;
    Vec2 uv0, uv1, uv2;
    bool has_vertex_normals = false;
    bool has_uvs = false;
    Material* material;

    Triangle() : v0(), v1(), v2(), n0(), n1(), n2(), uv0(), uv1(), uv2(), material(nullptr) {}
    Triangle(const Point3& a, const Point3& b, const Point3& c, Material* material = nullptr)
        : v0(a), v1(b), v2(c), n0(), n1(), n2(), uv0(), uv1(), uv2(), material(material) {}
    Triangle(const Point3& a, const Point3& b, const Point3& c,
             const Vec3& na, const Vec3& nb, const Vec3& nc,
             Material* material = nullptr)
        : v0(a), v1(b), v2(c), n0(na), n1(nb), n2(nc),
          uv0(), uv1(), uv2(), has_vertex_normals(true), material(material) {}
    Triangle(const Point3& a, const Point3& b, const Point3& c,
             const Vec3& na, const Vec3& nb, const Vec3& nc,
             const Vec2& t0, const Vec2& t1, const Vec2& t2,
             Material* material = nullptr)
        : v0(a), v1(b), v2(c), n0(na), n1(nb), n2(nc),
          uv0(t0), uv1(t1), uv2(t2),
          has_vertex_normals(true), has_uvs(true), material(material) {}
```

(The `hit` and `bounding_box` methods stay; `hit` was already replaced in Step 2.)

- [ ] **Step 4: Compile**

Run: `cmake --build build 2>&1 | tail -10`
Expected: `Built target raytracer`, no warnings

- [ ] **Step 5: Verify backward compatibility (OBJ mesh still renders)**

Check if an OBJ scene exists. Run: `ls scenes/*.json`
If there's a scene using `"type": "mesh"`, render it. Otherwise render the default:

Run: `./build/raytracer --scene scenes/default.json --out /tmp/stage8_t4.ppm`
Expected: renders without crash, output visually correct

- [ ] **Step 6: Commit**

```bash
git add include/raytracer/geometry/triangle.h
git commit -m "Stage 8 task 4: extract triangle_intersect free fn, add UV support to Triangle"
```

---

## Task 5: Create `TriangleMesh` class

**Files:**
- Create: `include/raytracer/geometry/triangle_mesh.h`

- [ ] **Step 1: Create the new file**

Write `include/raytracer/geometry/triangle_mesh.h` with this exact content:

```cpp
// Module B: geometry -- triangle mesh (SoA layout for future GPU upload)
#ifndef RT_TRIANGLE_MESH_H
#define RT_TRIANGLE_MESH_H

#include "raytracer/geometry/hittable.h"
#include "raytracer/geometry/triangle.h"  // triangle_intersect
#include "raytracer/math/vec3.h"
#include <vector>

class TriangleMesh : public Hittable {
public:
    std::vector<Point3> vertices;
    std::vector<Vec3> normals;              // empty = no vertex normals
    std::vector<Vec2> uvs;                 // empty = no uvs
    std::vector<int> indices;              // size must be multiple of 3
    std::vector<Material*> material_per_tri;  // size == indices.size() / 3

    TriangleMesh() = default;

    // Brute-force traversal; BVH acceleration comes in Stage 10.
    bool hit(const Ray& r, double t_min, double t_max,
             HitRecord& rec) const override {
        bool hit_any = false;
        double closest = t_max;
        size_t tri_count = indices.size() / 3;
        for (size_t i = 0; i < tri_count; i++) {
            int i0 = indices[i*3 + 0];
            int i1 = indices[i*3 + 1];
            int i2 = indices[i*3 + 2];
            double t, b1, b2;
            if (!triangle_intersect(r, t_min, closest,
                                    vertices[i0], vertices[i1], vertices[i2],
                                    t, b1, b2)) continue;
            double b0 = 1.0 - b1 - b2;
            HitRecord tmp;
            tmp.t = t;
            tmp.p = r.at(t);
            Vec3 outward;
            if (!normals.empty()) {
                outward = (b0 * normals[i0] + b1 * normals[i1] + b2 * normals[i2]).normalized();
            } else {
                outward = cross(vertices[i1] - vertices[i0],
                                vertices[i2] - vertices[i0]).normalized();
            }
            tmp.set_face_normal(r, outward);
            if (!uvs.empty()) {
                Vec2 uv = b0 * uvs[i0] + b1 * uvs[i1] + b2 * uvs[i2];
                tmp.u = uv.x;
                tmp.v = uv.y;
            } else {
                tmp.u = b1;
                tmp.v = b2;
            }
            tmp.material = material_per_tri[i];
            rec = tmp;
            closest = t;
            hit_any = true;
        }
        return hit_any;
    }

    bool bounding_box(AABB& output_box) const override {
        if (!bbox_computed_) compute_bbox();
        output_box = bbox_;
        return true;
    }

    void compute_bbox() const {
        if (vertices.empty()) {
            bbox_ = AABB();
            bbox_computed_ = true;
            return;
        }
        Point3 mn = vertices[0], mx = vertices[0];
        for (const Point3& p : vertices) {
            if (p.x < mn.x) mn.x = p.x;
            if (p.y < mn.y) mn.y = p.y;
            if (p.z < mn.z) mn.z = p.z;
            if (p.x > mx.x) mx.x = p.x;
            if (p.y > mx.y) mx.y = p.y;
            if (p.z > mx.z) mx.z = p.z;
        }
        const double pad = 1e-4;
        bbox_ = AABB(mn - Vec3(pad, pad, pad), mx + Vec3(pad, pad, pad));
        bbox_computed_ = true;
    }

private:
    mutable AABB bbox_;
    mutable bool bbox_computed_ = false;
};

#endif
```

- [ ] **Step 2: Compile** (nothing includes it yet, but verify no syntax errors)

Run: `cmake --build build 2>&1 | tail -5`
Expected: `Built target raytracer` (unused header, but must parse cleanly)

- [ ] **Step 3: Commit**

```bash
git add include/raytracer/geometry/triangle_mesh.h
git commit -m "Stage 8 task 5: add TriangleMesh (SoA layout)"
```

---

## Task 6: Extend JSON scene parsing with `triangle` and `triangles` types

**Files:**
- Modify: `include/raytracer/scene/scene.h:126-158` (the object-type dispatch in `load_scene`)
- Modify: `include/raytracer/scene/scene.h:1-19` (add include of `triangle_mesh.h`)

- [ ] **Step 1: Add include of `triangle_mesh.h`**

In `include/raytracer/scene/scene.h`, find the block of `#include "raytracer/geometry/..."` lines (around line 11-13) and add after the `triangle.h` include:

```cpp
#include "raytracer/geometry/triangle_mesh.h"
```

- [ ] **Step 2: Add a `to_vec2` helper**

Right after the existing `to_vec3` helper (around line 40), add:

```cpp
inline Vec2 to_vec2(const JsonValue& arr) {
    return Vec2(arr.arrVal[0].numVal, arr.arrVal[1].numVal);
}
```

- [ ] **Step 3: Add parsing for `triangle` and `triangles` types**

In `load_scene`, inside the `if (root.has("objects"))` loop, after the existing `else if (type == "mesh") { ... }` block and before the final `else { throw ... }`, insert two new branches:

```cpp
            } else if (type == "triangle") {
                const JsonValue& verts = obj.at("vertices");
                Point3 a = to_vec3(verts.arrVal[0]);
                Point3 b = to_vec3(verts.arrVal[1]);
                Point3 c = to_vec3(verts.arrVal[2]);
                Material* mat = ensure_material(obj, scene);
                std::unique_ptr<Triangle> tri;
                if (obj.has("normals") && obj.has("uvs")) {
                    const JsonValue& norms = obj.at("normals");
                    const JsonValue& uvs = obj.at("uvs");
                    tri = std::make_unique<Triangle>(
                        a, b, c,
                        to_vec3(norms.arrVal[0]), to_vec3(norms.arrVal[1]), to_vec3(norms.arrVal[2]),
                        to_vec2(uvs.arrVal[0]), to_vec2(uvs.arrVal[1]), to_vec2(uvs.arrVal[2]),
                        mat);
                } else if (obj.has("normals")) {
                    const JsonValue& norms = obj.at("normals");
                    tri = std::make_unique<Triangle>(
                        a, b, c,
                        to_vec3(norms.arrVal[0]), to_vec3(norms.arrVal[1]), to_vec3(norms.arrVal[2]),
                        mat);
                } else {
                    tri = std::make_unique<Triangle>(a, b, c, mat);
                }
                scene.primitives.add(tri.get());
                scene.objects.push_back(std::move(tri));
            } else if (type == "triangles") {
                auto mesh = std::make_unique<TriangleMesh>();
                const JsonValue& verts = obj.at("vertices");
                for (const JsonValue& v : verts.arrVal)
                    mesh->vertices.push_back(to_vec3(v));
                const JsonValue& idxs = obj.at("indices");
                for (const JsonValue& i : idxs.arrVal)
                    mesh->indices.push_back((int)i.numVal);
                if (obj.has("normals")) {
                    for (const JsonValue& n : obj.at("normals").arrVal)
                        mesh->normals.push_back(to_vec3(n));
                }
                if (obj.has("uvs")) {
                    for (const JsonValue& uv : obj.at("uvs").arrVal)
                        mesh->uvs.push_back(to_vec2(uv));
                }
                Material* mat = ensure_material(obj, scene);
                size_t tri_count = mesh->indices.size() / 3;
                mesh->material_per_tri.assign(tri_count, mat);
                scene.primitives.add(mesh.get());
                scene.objects.push_back(std::move(mesh));
            }
```

- [ ] **Step 4: Compile**

Run: `cmake --build build 2>&1 | tail -10`
Expected: `Built target raytracer`, no warnings

- [ ] **Step 5: Verify backward compatibility**

Run: `./build/raytracer --scene scenes/default.json --out /tmp/stage8_t6.ppm`
Expected: renders correctly (no crash, output identical to pre-refactor default scene)

- [ ] **Step 6: Commit**

```bash
git add include/raytracer/scene/scene.h
git commit -m "Stage 8 task 6: parse triangle/triangles JSON types"
```

---

## Task 7: Create acceptance scene `triangle_test.json`

**Files:**
- Create: `scenes/triangle_test.json`

- [ ] **Step 1: Create the scene file**

Write `scenes/triangle_test.json` with this exact content:

```json
{
    "image": {
        "width": 800,
        "height": 400,
        "samples": 32,
        "max_depth": 32,
        "output": "triangle_test.ppm"
    },
    "camera": {
        "lookfrom": [0, 0, 0],
        "lookat": [0, 0, -1],
        "vup": [0, 1, 0],
        "vfov": 60,
        "aperture": 0.0,
        "focus_dist": 1.0
    },
    "objects": [
        {
            "type": "triangles",
            "vertices": [
                [-2, -1, -2],
                [2, -1, -2],
                [2, 1, -2],
                [-2, 1, -2]
            ],
            "indices": [0, 1, 2, 0, 2, 3],
            "uvs": [
                [0, 0],
                [1, 0],
                [1, 1],
                [0, 1]
            ],
            "material": {
                "type": "lambertian",
                "albedo": [0.9, 0.9, 0.9]
            }
        },
        {
            "type": "triangle",
            "vertices": [
                [-0.5, -0.5, -1.5],
                [0.5, -0.5, -1.5],
                [0, 0.5, -1.5]
            ],
            "material": {
                "type": "lambertian",
                "albedo": [0.8, 0.2, 0.2]
            }
        },
        {
            "type": "sphere",
            "center": [0, 0, -1],
            "radius": 0.3,
            "material": {
                "type": "lambertian",
                "albedo": [0.1, 0.2, 0.5]
            }
        }
    ]
}
```

- [ ] **Step 2: Render the scene**

Run: `./build/raytracer --scene scenes/triangle_test.json`
Expected output:
```
Scene: scenes/triangle_test.json
Image: 800x400, samples=32, depth=32
Primitives: 3
Progress: 100%
Wrote triangle_test.ppm
```

- [ ] **Step 3: Convert to PNG for inspection**

Run: `sips -s format png triangle_test.ppm --out triangle_test.png 2>&1 | tail -2`
Expected: creates `triangle_test.png`

- [ ] **Step 4: Inspect the PNG**

Open `triangle_test.png` and verify:
- White quad (ground/wall) filling background
- Red triangle in center
- Blue sphere overlapping the triangle
- Blue-to-white gradient sky at top

- [ ] **Step 5: Commit**

```bash
git add scenes/triangle_test.json
git commit -m "Stage 8 task 7: add triangle acceptance scene"
```

---

## Task 8: UV interpolation verification (temporary debug path)

**Files:**
- Modify (temp): `src/main.cpp` (add `#ifdef RT_UV_DEBUG` branch in `ray_color`)
- Create (temp): `scenes/triangle_uv_debug.json`

> **Why not a debug material?** The current `ray_color` returns `attenuation * ray_color(scattered, depth-1)` on scatter, and returns black when `scatter==false`. Either path corrupts UV-as-color output (black or sky tint). So we gate a debug branch in `ray_color` itself via a compile macro, keeping changes localized and easy to remove.

- [ ] **Step 1: Add `RT_UV_DEBUG` branch to `ray_color` in `src/main.cpp`**

Replace the body of `ray_color` (lines 12-27) with:

```cpp
Color ray_color(const Ray& r, const Hittable& world, int depth) {
    if (depth <= 0) return Color(0, 0, 0);

    HitRecord rec;
    if (world.hit(r, 0.001, infinity, rec)) {
#ifdef RT_UV_DEBUG
        return Color(rec.u, rec.v, 0);
#else
        Ray scattered;
        Color attenuation;
        if (rec.material && rec.material->scatter(r, rec, attenuation, scattered))
            return attenuation * ray_color(scattered, world, depth - 1);
        return Color(0, 0, 0);
#endif
    }

    Vec3 unit_dir = r.direction.normalized();
    double t = 0.5 * (unit_dir.y + 1.0);
    return (1 - t) * Color(1.0, 1.0, 1.0) + t * Color(0.5, 0.7, 1.0);
}
```

- [ ] **Step 2: Create the UV debug scene**

Write `scenes/triangle_uv_debug.json`:

```json
{
    "image": {
        "width": 400,
        "height": 400,
        "samples": 1,
        "max_depth": 1,
        "output": "triangle_uv_debug.ppm"
    },
    "camera": {
        "lookfrom": [0, 0, 0],
        "lookat": [0, 0, -1],
        "vup": [0, 1, 0],
        "vfov": 60,
        "aperture": 0.0,
        "focus_dist": 1.0
    },
    "objects": [
        {
            "type": "triangles",
            "vertices": [
                [-1, -1, -1],
                [1, -1, -1],
                [1, 1, -1],
                [-1, 1, -1]
            ],
            "indices": [0, 1, 2, 0, 2, 3],
            "uvs": [
                [0, 0],
                [1, 0],
                [1, 1],
                [0, 1]
            ],
            "material": { "type": "lambertian", "albedo": [1, 1, 1] }
        }
    ]
}
```

- [ ] **Step 3: Build with `RT_UV_DEBUG` defined**

Run:
```bash
cmake --build build --target clean 2>/dev/null
cmake -S . -B build -DCMAKE_CXX_FLAGS="-DRT_UV_DEBUG"
cmake --build build 2>&1 | tail -5
```
Expected: `Built target raytracer`

- [ ] **Step 4: Render the UV debug image**

Run:
```bash
./build/raytracer --scene scenes/triangle_uv_debug.json
sips -s format png triangle_uv_debug.ppm --out triangle_uv_debug.png 2>&1 | tail -1
```

- [ ] **Step 5: Inspect the debug PNG**

Open `triangle_uv_debug.png`. Expected gradient on the quad:
- Bottom-left corner: black (UV 0,0 → RGB 0,0,0)
- Bottom-right corner: red (UV 1,0 → RGB 1,0,0)
- Top-right corner: yellow (UV 1,1 → RGB 1,1,0)
- Top-left corner: green (UV 0,1 → RGB 0,1,0)
- Smooth gradient across the quad, **no visible seam** between the two triangles (both tris share the same UV mapping)

If gradient is wrong (uniform color, wrong corner colors, or a visible color discontinuity at the diagonal seam), UV interpolation is broken — revisit Task 4 (Triangle UV) or Task 5 (TriangleMesh UV).

- [ ] **Step 6: (No commit yet — debug artifacts removed in Task 9)**

Do not commit. Task 9 reverts the macro and removes temp files.

---

## Task 9: Remove UV debug artifacts and final verification

**Files:**
- Modify (revert): `src/main.cpp` (remove `RT_UV_DEBUG` branch, restore original `ray_color`)
- Delete: `scenes/triangle_uv_debug.json`
- Delete: `triangle_uv_debug.ppm`, `triangle_uv_debug.png` (render outputs, gitignored anyway)

- [ ] **Step 1: Revert `ray_color` in `src/main.cpp`**

Replace the `ray_color` body (with the `#ifdef RT_UV_DEBUG` branch) back to the original:

```cpp
Color ray_color(const Ray& r, const Hittable& world, int depth) {
    if (depth <= 0) return Color(0, 0, 0);

    HitRecord rec;
    if (world.hit(r, 0.001, infinity, rec)) {
        Ray scattered;
        Color attenuation;
        if (rec.material && rec.material->scatter(r, rec, attenuation, scattered))
            return attenuation * ray_color(scattered, world, depth - 1);
        return Color(0, 0, 0);
    }

    Vec3 unit_dir = r.direction.normalized();
    double t = 0.5 * (unit_dir.y + 1.0);
    return (1 - t) * Color(1.0, 1.0, 1.0) + t * Color(0.5, 0.7, 1.0);
}
```

- [ ] **Step 2: Delete debug scene and outputs**

Run:
```bash
rm -f scenes/triangle_uv_debug.json
rm -f triangle_uv_debug.ppm triangle_uv_debug.png
```

- [ ] **Step 3: Clean rebuild WITHOUT the macro** (to clear cached `-DRT_UV_DEBUG`)

Run:
```bash
rm -rf build
cmake -S . -B build
cmake --build build 2>&1 | tail -5
```
Expected: `Built target raytracer`, no warnings, no `RT_UV_DEBUG` in compile flags

- [ ] **Step 4: Final backward-compatibility check**

Render all three scenes:

```bash
./build/raytracer --scene scenes/default.json --out /tmp/stage8_final_default.ppm
./build/raytracer --scene scenes/three_balls.json --out /tmp/stage8_final_three_balls.ppm
./build/raytracer --scene scenes/triangle_test.json --out /tmp/stage8_final_triangle.ppm
```

Expected: all three render successfully; `triangle_test` shows quad + triangle + sphere.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git rm -f scenes/triangle_uv_debug.json 2>/dev/null || true
git commit -m "Stage 8 task 9: remove UV debug branch, final verification"
```

---

## Task 10: Update README

**Files:**
- Modify: `README.md` (update "当前实现状态" section)

- [ ] **Step 1: Read current README status section**

Run: `grep -n "当前实现状态" -A 12 README.md`
Expected: see the bullet list of stages 0-7 + "后续：BVH 加速、三角形网格、多线程"

- [ ] **Step 2: Update the status list**

Replace the "⬜ 后续：BVH 加速、三角形网格、多线程" line and add a Stage 8 entry. The list should now read (in Chinese, matching existing style):

```markdown
- ✅ 阶段 0：骨架 + PPM 渐变图
- ✅ 阶段 1：射线-球求交
- ✅ 阶段 2：法线 / 天空背景
- ✅ 阶段 3：多物体（球 + 地面）
- ✅ 阶段 4：Lambert 漫反射 + 柔和阴影
- ✅ 阶段 5：抗锯齿（多次采样平均）
- ✅ 阶段 6：材质系统（Lambert / Metal / Dielectric 全部完成）
- ✅ 阶段 7：场景文件化（JSON 场景 + 命令行参数）
- ✅ 阶段 8：三角形与三角网格求交（Möller–Trumbore + UV 插值 + SoA 网格）
- ⬜ 后续：BVH 加速、PBR 材质、CUDA 移植
```

- [ ] **Step 3: Add a brief note about the new scene types**

Find the "示例场景" table in README and append a row:

```markdown
| `scenes/triangle_test.json` | 三角形 + 三角网格（验证 UV 插值） |
```

- [ ] **Step 4: Commit**

```bash
git add README.md
git commit -m "Stage 8 task 10: update README status and scene list"
```

---

## Final Verification (after all tasks)

- [ ] **Full clean build from scratch** (ensures no `RT_UV_DEBUG` macro residue)

```bash
rm -rf build
cmake -S . -B build
cmake --build build
```
Expected: clean build, no warnings, no `RT_UV_DEBUG` defined.

- [ ] **Render all scenes**

```bash
./build/raytracer --scene scenes/default.json
./build/raytracer --scene scenes/three_balls.json
./build/raytracer --scene scenes/triangle_test.json
```
Expected: all three render successfully.

- [ ] **Verify PNG outputs**

```bash
sips -s format png out.ppm --out final_default.png
sips -s format png three_balls.ppm --out final_three_balls.png
sips -s format png triangle_test.ppm --out final_triangle.png
```

Open each PNG and confirm:
- `final_default.png`: two diffuse spheres on grey ground, blue sky (unchanged from pre-Stage-8)
- `final_three_balls.png`: glass + metal + diffuse spheres (unchanged)
- `final_triangle.png`: white quad background, red triangle, blue sphere

- [ ] **Verify debug artifacts gone**

Run: `ls scenes/triangle_uv_debug.json 2>&1; grep -n RT_UV_DEBUG src/main.cpp 2>&1`
Expected: both report "No such file" / no matches.

- [ ] **Git log check**

Run: `git log --oneline -10`
Expected: see Stage 8 commits (tasks 1-10) on top.

---

## Self-Review Notes

- **Spec coverage**: All 7 spec sections (3.1 decisions, 4.1-4.8 file changes, 5 out-of-scope, 6 risks, 7 verification, 8 future hooks) mapped to tasks 1-10.
- **Type consistency**: `triangle_intersect` signature (Task 4) matches usage in `Triangle::hit` (Task 4) and `TriangleMesh::hit` (Task 5). `Vec2` operators (Task 1) match usage in Tasks 4/5. `HitRecord.u/v` (Task 3) used consistently.
- **No placeholders**: All code blocks complete, all commands exact.
- **Backward compat**: Tasks 3, 4, 6, 9 include explicit "render default scene" verification steps.
- **Temp artifacts**: Task 8 creates debug branch in `main.cpp` + debug scene; Task 9 reverts `main.cpp`, deletes scene, requires clean rebuild to clear macro — clean separation.
- **Debug path correctness**: `RT_UV_DEBUG` branch returns `Color(rec.u, rec.v, 0)` directly from `ray_color` on hit, avoiding the `attenuation * recursive_call` path that would black-out or sky-tint the UV colors.
