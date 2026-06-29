# Stage 9: OBJ Loading & Transforms Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the hand-written OBJ parser with tinyobjloader, add 4x4 matrix transforms (translate/rotate/scale + axis-angle), bake transforms to world space at load time, compute smooth normals for OBJ files lacking them, and keep the legacy parser as fallback.

**Architecture:** Header-only C++17. New `Mat4` type (row-major) in `math/`. Rewrite `obj.h` to wrap tinyobjloader, filling a `TriangleMesh` (SoA) with transform-baked vertices/normals. `scene.h` gains `parse_transform` and dispatches: try tinyobjloader → on failure, fall back to legacy `load_obj_triangles`. `main.cpp` hosts the `TINYOBJLOADER_IMPLEMENTATION` macro.

**Tech Stack:** C++17, CMake 3.10, tinyobjloader (header-only third-party), zero other deps.

**Spec:** `docs/superpowers/specs/2026-06-28-stage9-obj-loading-and-transforms-design.md`

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `third_party/tiny_obj_loader.h` | Create | tinyobjloader single-header library |
| `include/raytracer/math/mat4.h` | Create | 4x4 row-major matrix with transforms |
| `include/raytracer/scene/obj.h` | Rewrite | Keep legacy `load_obj_triangles` + add `load_obj_mesh` |
| `include/raytracer/scene/scene.h` | Modify | Add `parse_transform`, new mesh dispatch with fallback |
| `src/main.cpp` | Modify | Add `TINYOBJLOADER_IMPLEMENTATION` + include |
| `CMakeLists.txt` | Modify | Add `third_party` to include dirs |
| `models/cube_no_normal.obj` | Create | Test OBJ without normals |
| `scenes/bunny_test.json` | Create | Acceptance scene: mark.obj + transform |
| `scenes/no_normal_obj.json` | Create | Acceptance scene: cube without normals |
| `README.md` | Modify | Update status and docs |

---

## Task 1: Add tinyobjloader to third_party

**Files:**
- Create: `third_party/tiny_obj_loader.h`

- [ ] **Step 1: Create directory and copy the library**

```bash
mkdir -p third_party
cp /tmp/tiny_obj_loader.h third_party/tiny_obj_loader.h
```

- [ ] **Step 2: Verify the file**

Run: `wc -l third_party/tiny_obj_loader.h && head -3 third_party/tiny_obj_loader.h`
Expected: ~13865 lines, starts with MIT license comment

- [ ] **Step 3: Commit**

```bash
git add third_party/tiny_obj_loader.h
git commit -m "Stage 9 task 1: add tinyobjloader to third_party"
```

---

## Task 2: Create `Mat4` type

**Files:**
- Create: `include/raytracer/math/mat4.h`

- [ ] **Step 1: Write the file**

Create `include/raytracer/math/mat4.h` with this exact content:

```cpp
// Module A: math -- 4x4 row-major matrix for transforms
#ifndef RT_MAT4_H
#define RT_MAT4_H

#include "raytracer/math/vec3.h"
#include "raytracer/math/util.h"
#include <cmath>

class Mat4 {
public:
    double m[4][4];

    Mat4() {
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                m[i][j] = (i == j) ? 1.0 : 0.0;
    }

    static Mat4 identity() { return Mat4(); }

    static Mat4 translate(double tx, double ty, double tz) {
        Mat4 r;
        r.m[0][3] = tx; r.m[1][3] = ty; r.m[2][3] = tz;
        return r;
    }

    static Mat4 scale(double sx, double sy, double sz) {
        Mat4 r;
        r.m[0][0] = sx; r.m[1][1] = sy; r.m[2][2] = sz;
        return r;
    }

    static Mat4 rotate_x(double deg) {
        double c = std::cos(degrees_to_radians(deg));
        double s = std::sin(degrees_to_radians(deg));
        Mat4 r;
        r.m[1][1] = c;  r.m[1][2] = -s;
        r.m[2][1] = s;  r.m[2][2] = c;
        return r;
    }

    static Mat4 rotate_y(double deg) {
        double c = std::cos(degrees_to_radians(deg));
        double s = std::sin(degrees_to_radians(deg));
        Mat4 r;
        r.m[0][0] = c;  r.m[0][2] = s;
        r.m[2][0] = -s; r.m[2][2] = c;
        return r;
    }

    static Mat4 rotate_z(double deg) {
        double c = std::cos(degrees_to_radians(deg));
        double s = std::sin(degrees_to_radians(deg));
        Mat4 r;
        r.m[0][0] = c;  r.m[0][1] = -s;
        r.m[1][0] = s;  r.m[1][1] = c;
        return r;
    }

    // Axis-angle rotation (Rodrigues). axis is normalized internally.
    static Mat4 rotate_axis(const Vec3& axis, double deg) {
        Vec3 n = axis.normalized();
        double c = std::cos(degrees_to_radians(deg));
        double s = std::sin(degrees_to_radians(deg));
        double t = 1.0 - c;
        double x = n.x, y = n.y, z = n.z;
        Mat4 r;
        r.m[0][0] = t*x*x + c;     r.m[0][1] = t*x*y - s*z;  r.m[0][2] = t*x*z + s*y;
        r.m[1][0] = t*x*y + s*z;   r.m[1][1] = t*y*y + c;     r.m[1][2] = t*y*z - s*x;
        r.m[2][0] = t*x*z - s*y;   r.m[2][1] = t*y*z + s*x;   r.m[2][2] = t*z*z + c;
        return r;
    }

    // this * other  (apply other first, then this)
    Mat4 operator*(const Mat4& other) const {
        Mat4 r;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                double sum = 0;
                for (int k = 0; k < 4; k++)
                    sum += m[i][k] * other.m[k][j];
                r.m[i][j] = sum;
            }
        return r;
    }

    // Transform point (w=1)
    Vec3 transform_point(const Vec3& p) const {
        double x = m[0][0]*p.x + m[0][1]*p.y + m[0][2]*p.z + m[0][3];
        double y = m[1][0]*p.x + m[1][1]*p.y + m[1][2]*p.z + m[1][3];
        double z = m[2][0]*p.x + m[2][1]*p.y + m[2][2]*p.z + m[2][3];
        double w = m[3][0]*p.x + m[3][1]*p.y + m[3][2]*p.z + m[3][3];
        if (w != 0 && w != 1) { x /= w; y /= w; z /= w; }
        return Vec3(x, y, z);
    }

    // Transform direction (w=0, no translation)
    Vec3 transform_direction(const Vec3& d) const {
        double x = m[0][0]*d.x + m[0][1]*d.y + m[0][2]*d.z;
        double y = m[1][0]*d.x + m[1][1]*d.y + m[1][2]*d.z;
        double z = m[2][0]*d.x + m[2][1]*d.y + m[2][2]*d.z;
        return Vec3(x, y, z);
    }

    // Transform normal via inverse-transpose (3x3 adjugate, det scaling irrelevant)
    Vec3 transform_normal(const Vec3& n) const {
        double a = m[0][0], b = m[0][1], c = m[0][2];
        double d = m[1][0], e = m[1][1], f = m[1][2];
        double g = m[2][0], h = m[2][1], i = m[2][2];
        double A =  (e*i - f*h);
        double B = -(d*i - f*g);
        double C =  (d*h - e*g);
        double D = -(b*i - c*h);
        double E =  (a*i - c*g);
        double F = -(a*h - b*g);
        double G =  (b*f - c*e);
        double H = -(a*f - c*d);
        double I =  (a*e - b*d);
        return Vec3(A*n.x + D*n.y + G*n.z,
                    B*n.x + E*n.y + H*n.z,
                    C*n.x + F*n.y + I*n.z);
    }
};

#endif
```

- [ ] **Step 2: Compile to verify syntax**

Run: `cmake --build build 2>&1 | tail -5`
Expected: `Built target raytracer` (nothing includes it yet, but must parse)

- [ ] **Step 3: Commit**

```bash
git add include/raytracer/math/mat4.h
git commit -m "Stage 9 task 2: add Mat4 type (row-major, transforms)"
```

---

## Task 3: Rewrite `obj.h` with tinyobjloader + legacy fallback

**Files:**
- Rewrite: `include/raytracer/scene/obj.h`

- [ ] **Step 1: Write the new file**

Create `include/raytracer/scene/obj.h` with this exact content:

```cpp
// Module D: scene -- OBJ loader (tinyobjloader + legacy fallback)
#ifndef RT_OBJ_H
#define RT_OBJ_H

#include "raytracer/geometry/triangle_mesh.h"
#include "raytracer/math/mat4.h"
#include "raytracer/math/vec3.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// ============================================================
// Legacy hand-written parser (fallback when tinyobjloader fails)
// ============================================================

struct ObjTriangleData {
    Point3 v0, v1, v2;
    Vec3 n0, n1, n2;
    bool has_normals = false;
};

struct ObjFaceIndex {
    int v = 0;
    int vt = 0;
    int vn = 0;
};

inline int resolve_obj_index(int index, size_t count) {
    if (index > 0) return index - 1;
    if (index < 0) return static_cast<int>(count) + index;
    throw std::runtime_error("OBJ index cannot be zero");
}

inline ObjFaceIndex parse_obj_face_index(const std::string& token) {
    ObjFaceIndex idx;
    size_t first = token.find('/');
    if (first == std::string::npos) {
        idx.v = std::stoi(token);
        return idx;
    }
    size_t second = token.find('/', first + 1);
    idx.v = std::stoi(token.substr(0, first));
    if (second == std::string::npos) {
        std::string vt = token.substr(first + 1);
        if (!vt.empty()) idx.vt = std::stoi(vt);
        return idx;
    }
    std::string vt = token.substr(first + 1, second - first - 1);
    std::string vn = token.substr(second + 1);
    if (!vt.empty()) idx.vt = std::stoi(vt);
    if (!vn.empty()) idx.vn = std::stoi(vn);
    return idx;
}

inline std::vector<ObjTriangleData> load_obj_triangles(const std::string& path,
                                                       double scale = 1.0,
                                                       const Vec3& translate = Vec3(0, 0, 0)) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot open OBJ file: " + path);

    std::vector<Point3> positions;
    std::vector<Vec3> normals;
    std::vector<ObjTriangleData> triangles;
    std::string line;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string tag;
        iss >> tag;
        if (tag == "v") {
            double x, y, z;
            iss >> x >> y >> z;
            positions.push_back(scale * Point3(x, y, z) + translate);
        } else if (tag == "vn") {
            double x, y, z;
            iss >> x >> y >> z;
            normals.push_back(Vec3(x, y, z).normalized());
        } else if (tag == "f") {
            std::vector<ObjFaceIndex> face;
            std::string token;
            while (iss >> token) face.push_back(parse_obj_face_index(token));
            if (face.size() < 3) continue;
            for (size_t i = 1; i + 1 < face.size(); i++) {
                ObjTriangleData tri;
                const ObjFaceIndex idx0 = face[0];
                const ObjFaceIndex idx1 = face[i];
                const ObjFaceIndex idx2 = face[i + 1];
                tri.v0 = positions.at(resolve_obj_index(idx0.v, positions.size()));
                tri.v1 = positions.at(resolve_obj_index(idx1.v, positions.size()));
                tri.v2 = positions.at(resolve_obj_index(idx2.v, positions.size()));
                if (idx0.vn != 0 && idx1.vn != 0 && idx2.vn != 0 && !normals.empty()) {
                    tri.n0 = normals.at(resolve_obj_index(idx0.vn, normals.size()));
                    tri.n1 = normals.at(resolve_obj_index(idx1.vn, normals.size()));
                    tri.n2 = normals.at(resolve_obj_index(idx2.vn, normals.size()));
                    tri.has_normals = true;
                }
                triangles.push_back(tri);
            }
        }
    }
    if (triangles.empty()) throw std::runtime_error("OBJ contains no faces: " + path);
    return triangles;
}

// ============================================================
// New: tinyobjloader-based loader → TriangleMesh (SoA, baked)
// ============================================================

#include "tiny_obj_loader.h"

// Load OBJ via tinyobjloader, bake `transform` into vertices/normals.
// Computes area-weighted smooth normals when OBJ lacks vn.
// Throws std::runtime_error on failure.
inline TriangleMesh load_obj_mesh(const std::string& path, const Mat4& transform) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str())) {
        throw std::runtime_error("tinyobjloader failed: " + err + " " + warn);
    }

    const bool has_vn = !attrib.normals.empty();
    const bool has_vt = !attrib.texcoords.empty();

    // Smooth normals accumulator (only if missing vn)
    std::vector<Vec3> smooth_normals;
    if (!has_vn) {
        smooth_normals.assign(attrib.vertices.size() / 3, Vec3(0, 0, 0));
        // First pass: accumulate area-weighted face normals per vertex
        for (const auto& shape : shapes) {
            size_t idx_offset = 0;
            for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
                int fv = shape.mesh.num_face_vertices[f];
                if (fv < 3) { idx_offset += fv; continue; }
                tinyobj::index_t i0 = shape.mesh.indices[idx_offset + 0];
                tinyobj::index_t i1 = shape.mesh.indices[idx_offset + 1];
                tinyobj::index_t i2 = shape.mesh.indices[idx_offset + 2];
                Vec3 p0(attrib.vertices[3*i0.vertex_index + 0],
                        attrib.vertices[3*i0.vertex_index + 1],
                        attrib.vertices[3*i0.vertex_index + 2]);
                Vec3 p1(attrib.vertices[3*i1.vertex_index + 0],
                        attrib.vertices[3*i1.vertex_index + 1],
                        attrib.vertices[3*i1.vertex_index + 2]);
                Vec3 p2(attrib.vertices[3*i2.vertex_index + 0],
                        attrib.vertices[3*i2.vertex_index + 1],
                        attrib.vertices[3*i2.vertex_index + 2]);
                Vec3 face_n = cross(p1 - p0, p2 - p0);
                double area = 0.5 * face_n.length();
                face_n = face_n.normalized();
                smooth_normals[i0.vertex_index] += area * face_n;
                smooth_normals[i1.vertex_index] += area * face_n;
                smooth_normals[i2.vertex_index] += area * face_n;
                idx_offset += fv;
            }
        }
        for (auto& n : smooth_normals) {
            if (n.length_squared() < 1e-12) n = Vec3(0, 1, 0);
            else n = n.normalized();
        }
    }

    TriangleMesh mesh;

    // Second pass: fan-triangulate, bake transform, push into SoA
    auto get_pos = [&](const tinyobj::index_t& idx) {
        return Vec3(attrib.vertices[3*idx.vertex_index + 0],
                    attrib.vertices[3*idx.vertex_index + 1],
                    attrib.vertices[3*idx.vertex_index + 2]);
    };
    auto get_normal = [&](const tinyobj::index_t& idx) {
        return Vec3(attrib.normals[3*idx.normal_index + 0],
                    attrib.normals[3*idx.normal_index + 1],
                    attrib.normals[3*idx.normal_index + 2]);
    };
    auto get_uv = [&](const tinyobj::index_t& idx) {
        return Vec2(attrib.texcoords[2*idx.texcoord_index + 0],
                    attrib.texcoords[2*idx.texcoord_index + 1]);
    };

    for (const auto& shape : shapes) {
        size_t idx_offset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            int fv = shape.mesh.num_face_vertices[f];
            for (int k = 1; k + 1 < fv; k++) {
                tinyobj::index_t idx0 = shape.mesh.indices[idx_offset + 0];
                tinyobj::index_t idxk = shape.mesh.indices[idx_offset + k];
                tinyobj::index_t idxk1 = shape.mesh.indices[idx_offset + k + 1];

                mesh.vertices.push_back(transform.transform_point(get_pos(idx0)));
                mesh.vertices.push_back(transform.transform_point(get_pos(idxk)));
                mesh.vertices.push_back(transform.transform_point(get_pos(idxk1)));
                mesh.indices.push_back(static_cast<int>(mesh.vertices.size()) - 3);
                mesh.indices.push_back(static_cast<int>(mesh.vertices.size()) - 2);
                mesh.indices.push_back(static_cast<int>(mesh.vertices.size()) - 1);

                if (has_vn) {
                    mesh.normals.push_back(transform.transform_normal(get_normal(idx0)).normalized());
                    mesh.normals.push_back(transform.transform_normal(get_normal(idxk)).normalized());
                    mesh.normals.push_back(transform.transform_normal(get_normal(idxk1)).normalized());
                } else {
                    mesh.normals.push_back(transform.transform_normal(smooth_normals[idx0.vertex_index]).normalized());
                    mesh.normals.push_back(transform.transform_normal(smooth_normals[idxk.vertex_index]).normalized());
                    mesh.normals.push_back(transform.transform_normal(smooth_normals[idxk1.vertex_index]).normalized());
                }

                if (has_vt) {
                    mesh.uvs.push_back(get_uv(idx0));
                    mesh.uvs.push_back(get_uv(idxk));
                    mesh.uvs.push_back(get_uv(idxk1));
                }
            }
            idx_offset += fv;
        }
    }

    if (mesh.vertices.empty()) {
        throw std::runtime_error("OBJ contains no triangles: " + path);
    }
    return mesh;
}

#endif
```

- [ ] **Step 2: Add TINYOBJLOADER_IMPLEMENTATION to main.cpp**

In `src/main.cpp`, at the very top (before any other includes), add:

```cpp
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
```

- [ ] **Step 3: Update CMakeLists.txt**

Replace the `target_include_directories` line in `CMakeLists.txt` with:

```cmake
target_include_directories(raytracer PRIVATE include third_party)
```

- [ ] **Step 4: Compile**

Run:
```bash
rm -rf build && cmake -S . -B build && cmake --build build 2>&1 | tail -20
```
Expected: `Built target raytracer`. If tinyobjloader produces warnings, note them but proceed (we can suppress later if excessive).

- [ ] **Step 5: Verify backward compat (no scene uses mesh yet, just confirm build works)**

Run: `./build/raytracer --scene scenes/default.json --out /tmp/stage9_t3.ppm`
Expected: renders correctly (no crash)

- [ ] **Step 6: Commit**

```bash
git add include/raytracer/scene/obj.h src/main.cpp CMakeLists.txt
git commit -m "Stage 9 task 3: rewrite obj.h with tinyobjloader + Mat4 fallback"
```

---

## Task 4: Add `parse_transform` and new mesh dispatch to `scene.h`

**Files:**
- Modify: `include/raytracer/scene/scene.h`

- [ ] **Step 1: Add include of `mat4.h`**

After the existing `#include "raytracer/math/vec3.h"` line (around line 7), add:

```cpp
#include "raytracer/math/mat4.h"
```

- [ ] **Step 2: Add `parse_transform` helper**

After the `to_vec2` helper (around line 42), add:

```cpp
inline Mat4 parse_transform(const JsonValue& obj) {
    Mat4 M = Mat4::identity();
    if (obj.has("transform")) {
        const JsonValue& t = obj.at("transform");
        if (t.has("scale")) {
            const JsonValue& s = t.at("scale");
            if (s.isArray() && s.arrVal.size() == 3)
                M = M * Mat4::scale(s.arrVal[0].numVal, s.arrVal[1].numVal, s.arrVal[2].numVal);
            else
                M = M * Mat4::scale(s.numVal, s.numVal, s.numVal);
        }
        if (t.has("rotate_axis")) {
            const JsonValue& ra = t.at("rotate_axis");
            Vec3 axis = to_vec3(ra.at("axis"));
            double angle = ra.at("angle").numVal;
            M = M * Mat4::rotate_axis(axis, angle);
        }
        if (t.has("rotate")) {
            Vec3 r = to_vec3(t.at("rotate"));
            // XYZ order: apply X first, then Y, then Z  =>  M = M * Rz * Ry * Rx
            M = M * Mat4::rotate_z(r.z) * Mat4::rotate_y(r.y) * Mat4::rotate_x(r.x);
        }
        if (t.has("translate")) {
            Vec3 tr = to_vec3(t.at("translate"));
            M = M * Mat4::translate(tr.x, tr.y, tr.z);
        }
    } else {
        double s = obj.has("scale") ? obj.at("scale").numVal : 1.0;
        Vec3 tr = obj.has("translate") ? to_vec3(obj.at("translate")) : Vec3(0, 0, 0);
        M = Mat4::translate(tr.x, tr.y, tr.z) * Mat4::scale(s, s, s);
    }
    return M;
}
```

- [ ] **Step 3: Replace the `mesh` branch**

Find the existing `else if (type == "mesh") { ... }` block (currently around lines 141-158) and replace the entire block with:

```cpp
            } else if (type == "mesh") {
                std::string obj_path;
                if (obj.has("file")) obj_path = obj.at("file").strVal;
                else if (obj.has("obj")) obj_path = obj.at("obj").strVal;
                else if (obj.has("path")) obj_path = obj.at("path").strVal;
                else throw std::runtime_error("mesh requires 'file'/'obj'/'path'");
                obj_path = resolve_asset_path(scene_dir, obj_path);

                Mat4 transform = parse_transform(obj);
                Material* mat = ensure_material(obj, scene);

                bool loaded = false;
                try {
                    TriangleMesh mesh = load_obj_mesh(obj_path, transform);
                    size_t tri_count = mesh.indices.size() / 3;
                    mesh.material_per_tri.assign(tri_count, mat);
                    auto mesh_ptr = std::make_unique<TriangleMesh>(std::move(mesh));
                    scene.primitives.add(mesh_ptr.get());
                    scene.objects.push_back(std::move(mesh_ptr));
                    loaded = true;
                } catch (const std::exception& e) {
                    std::cerr << "tinyobjloader failed, fallback to legacy parser: "
                              << e.what() << "\n";
                }
                if (!loaded) {
                    double scale = obj.has("scale") ? obj.at("scale").numVal : 1.0;
                    Vec3 translate = obj.has("translate") ? to_vec3(obj.at("translate")) : Vec3(0, 0, 0);
                    std::vector<ObjTriangleData> tris = load_obj_triangles(obj_path, scale, translate);
                    for (const ObjTriangleData& tri : tris) {
                        std::unique_ptr<Triangle> mesh_tri;
                        if (tri.has_normals) {
                            mesh_tri = std::make_unique<Triangle>(
                                tri.v0, tri.v1, tri.v2, tri.n0, tri.n1, tri.n2, mat);
                        } else {
                            mesh_tri = std::make_unique<Triangle>(tri.v0, tri.v1, tri.v2, mat);
                        }
                        scene.primitives.add(mesh_tri.get());
                        scene.objects.push_back(std::move(mesh_tri));
                    }
                }
            }
```

- [ ] **Step 4: Compile**

Run: `cmake --build build 2>&1 | tail -15`
Expected: `Built target raytracer`, no errors

- [ ] **Step 5: Verify backward compat (three existing scenes)**

Run:
```bash
./build/raytracer --scene scenes/default.json --out /tmp/stage9_t4_default.ppm
./build/raytracer --scene scenes/three_balls.json --out /tmp/stage9_t4_three.ppm
./build/raytracer --scene scenes/triangle_test.json --out /tmp/stage9_t4_tri.ppm
```
Expected: all three render successfully (no mesh objects in these, but ensure nothing broke)

- [ ] **Step 6: Commit**

```bash
git add include/raytracer/scene/scene.h
git commit -m "Stage 9 task 4: parse_transform + mesh dispatch with fallback"
```

---

## Task 5: Create test OBJ without normals

**Files:**
- Create: `models/cube_no_normal.obj`

- [ ] **Step 1: Write the cube OBJ**

Create `models/cube_no_normal.obj`:

```
# Unit cube without normals (tests smooth normal computation)
v -1 -1 -1
v  1 -1 -1
v  1  1 -1
v -1  1 -1
v -1 -1  1
v  1 -1  1
v  1  1  1
v -1  1  1
f 1 2 3 4
f 5 8 7 6
f 1 5 6 2
f 2 6 7 3
f 3 7 8 4
f 4 8 5 1
```

- [ ] **Step 2: Commit (no rendering yet, scene comes in Task 7)**

```bash
git add models/cube_no_normal.obj
git commit -m "Stage 9 task 5: add cube OBJ without normals for smooth-normal test"
```

---

## Task 6: Create acceptance scene `bunny_test.json`

**Files:**
- Create: `scenes/bunny_test.json`

- [ ] **Step 1: Write the scene**

Create `scenes/bunny_test.json`:

```json
{
    "image": {
        "width": 400,
        "height": 400,
        "samples": 16,
        "max_depth": 32,
        "output": "bunny_test.ppm"
    },
    "camera": {
        "lookfrom": [0, 0, 5],
        "lookat": [0, 0, 0],
        "vup": [0, 1, 0],
        "vfov": 30,
        "aperture": 0.0,
        "focus_dist": 5.0
    },
    "objects": [
        {
            "type": "mesh",
            "file": "models/mark.obj",
            "transform": {
                "translate": [-13.5, -1, -3],
                "rotate": [0, 180, 0],
                "scale": 0.2
            },
            "material": {
                "type": "lambertian",
                "albedo": [0.8, 0.6, 0.2]
            }
        }
    ]
}
```

- [ ] **Step 2: Render**

Run: `./build/raytracer --scene scenes/bunny_test.json 2>&1 | tail -6`
Expected output:
```
Scene: scenes/bunny_test.json
Image: 400x400, samples=16, depth=32
Primitives: 1
Progress: 100%
Wrote bunny_test.ppm
```
(Primitives: 1 because the whole OBJ is one TriangleMesh)

- [ ] **Step 3: Convert to PNG and inspect**

Run: `sips -s format png bunny_test.ppm --out bunny_test.png 2>&1 | tail -1`

Open `bunny_test.png` and verify:
- Model is visible (not empty/black)
- Model is roughly centered (transform.translate moves it near origin)
- Model is rotated 180° around Y (facing camera)
- Smooth shading (vertex normals transformed correctly)

- [ ] **Step 4: Sanity-check pixel content**

Run:
```bash
python3 -c "
with open('bunny_test.ppm') as f:
    parts = f.read().split('\n', 3)
w, h = map(int, parts[1].split())
px = list(map(int, parts[3].split()))
non_black = sum(1 for v in px if v > 10)
print(f'Image: {w}x{h}, non-black pixel values: {non_black}/{len(px)} ({100*non_black/len(px):.1f}%)')
"
```
Expected: 30-70% non-black pixels (the model occupies a meaningful portion of the frame). If <5%, the model is missing or tiny; if >95%, something's wrong (e.g., it filled the screen).

- [ ] **Step 5: Commit**

```bash
git add scenes/bunny_test.json
git commit -m "Stage 9 task 6: add bunny_test.json acceptance scene"
```

---

## Task 7: Create no-normal OBJ acceptance scene

**Files:**
- Create: `scenes/no_normal_obj.json`

- [ ] **Step 1: Write the scene**

Create `scenes/no_normal_obj.json`:

```json
{
    "image": {
        "width": 400,
        "height": 400,
        "samples": 8,
        "max_depth": 32,
        "output": "no_normal_obj.ppm"
    },
    "camera": {
        "lookfrom": [0, 0, 5],
        "lookat": [0, 0, 0],
        "vup": [0, 1, 0],
        "vfov": 40,
        "aperture": 0.0,
        "focus_dist": 5.0
    },
    "objects": [
        {
            "type": "mesh",
            "file": "models/cube_no_normal.obj",
            "transform": {
                "rotate": [20, 30, 0],
                "scale": 1.0
            },
            "material": {
                "type": "lambertian",
                "albedo": [0.8, 0.4, 0.2]
            }
        }
    ]
}
```

- [ ] **Step 2: Render**

Run: `./build/raytracer --scene scenes/no_normal_obj.json 2>&1 | tail -6`
Expected: renders, `Primitives: 1`, no crash

- [ ] **Step 3: Convert and inspect**

Run: `sips -s format png no_normal_obj.ppm --out no_normal_obj.png 2>&1 | tail -1`

Open `no_normal_obj.png`. Verify:
- Cube is visible, rotated 20° X + 30° Y
- Faces have smooth shading (area-weighted normals blend at edges)
- No black/empty regions (smooth normals didn't degenerate)
- No obvious facets where normals flipped

- [ ] **Step 4: Commit**

```bash
git add scenes/no_normal_obj.json
git commit -m "Stage 9 task 7: add no-normal OBJ acceptance scene"
```

---

## Task 8: Verify backward compat with `mark.json` (if exists)

**Files:**
- (none, verification only)

- [ ] **Step 1: Check if mark.json exists and uses legacy fields**

Run: `ls scenes/mark.json 2>/dev/null && cat scenes/mark.json | head -20`

- [ ] **Step 2: If mark.json exists, render it and confirm fallback works**

If `scenes/mark.json` exists and uses `obj` + `scale` + `translate` (legacy fields):

Run: `./build/raytracer --scene scenes/mark.json --out /tmp/stage9_mark.ppm 2>&1 | tail -6`
Expected: renders successfully. If tinyobjloader succeeds, no fallback message; if it fails, see `fallback to legacy parser` message but still renders.

If `scenes/mark.json` does not exist, skip this task — it's optional. Note in your report which case applied.

- [ ] **Step 3: (No commit unless mark.json was missing and you created one — only if needed)**

---

## Task 9: Update README

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update status section**

Find the "当前实现状态" list and update the last two lines:

```markdown
- ✅ 阶段 8：三角形与三角网格求交（Möller–Trumbore + UV 插值 + SoA 网格）
- ✅ 阶段 9：OBJ 模型加载与变换（tinyobjloader + 4x4 矩阵 + 平滑法线）
- ⬜ 后续：BVH 线性化、PBR 材质、CUDA 移植
```

- [ ] **Step 2: Add transform docs to the OBJ section**

Find the "## OBJ 网格支持" section. After the existing example JSON, add a subsection:

```markdown

### 变换（阶段 9）

`mesh` 类型支持 `transform` 块，包含可选的 `translate`/`rotate`/`rotate_axis`/`scale`，变换按 SRT 顺序（先缩放，再旋转，最后平移）烘焙到顶点：

```json
{
    "type": "mesh",
    "file": "models/mark.obj",
    "transform": {
        "translate": [-13.5, -1, -3],
        "rotate": [0, 180, 0],
        "rotate_axis": { "axis": [0, 1, 0], "angle": 45 },
        "scale": 0.2
    },
    "material": { "type": "lambertian", "albedo": [0.8, 0.6, 0.2] }
}
```

- `translate`: `[x, y, z]` 平移
- `rotate`: `[rx, ry, rz]` 欧拉角（度），XYZ 顺序
- `rotate_axis`: `{axis: [x,y,z], angle: 度数}` 轴角旋转（`rotate` 之前应用）
- `scale`: 数值（统一缩放）或 `[sx, sy, sz]`（非统一缩放）

OBJ 缺少法线时自动按面积加权计算平滑法线。加载失败时降级到内置手写解析器（仅支持 scale/translate）。
```

- [ ] **Step 3: Update example scenes table**

Add rows for the two new scenes:

```markdown
| `scenes/bunny_test.json` | OBJ 网格 + 变换（mark.obj） |
| `scenes/no_normal_obj.json` | 无法线 OBJ + 平滑法线 |
```

- [ ] **Step 4: Commit**

```bash
git add README.md
git commit -m "Stage 9 task 9: update README with transform docs and status"
```

---

## Final Verification

- [ ] **Full clean build**

```bash
rm -rf build
cmake -S . -B build
cmake --build build 2>&1 | tail -5
```
Expected: `Built target raytracer`, no errors.

- [ ] **Render all scenes**

```bash
./build/raytracer --scene scenes/default.json --out /tmp/s9_default.ppm
./build/raytracer --scene scenes/three_balls.json --out /tmp/s9_three.ppm
./build/raytracer --scene scenes/triangle_test.json --out /tmp/s9_tri.ppm
./build/raytracer --scene scenes/bunny_test.json
./build/raytracer --scene scenes/no_normal_obj.json
```
Expected: all render successfully.

- [ ] **Verify PNGs**

```bash
sips -s format png bunny_test.ppm --out final_bunny.png
sips -s format png no_normal_obj.ppm --out final_cube.png
```
Open each and confirm:
- `final_bunny.png`: mark.obj visible, transformed, smooth-shaded
- `final_cube.png`: cube visible, rotated, smooth-shaded (no facet artifacts)

- [ ] **Git log check**

Run: `git log --oneline -12`
Expected: see Stage 9 commits on top.

---

## Self-Review Notes

- **Spec coverage**: All 9 decisions from spec §3 mapped to tasks. `Mat4` (Task 2) covers decisions 1, 2, 7. `obj.h` rewrite (Task 3) covers 3, 4, 5, 6. `scene.h` (Task 4) covers 8. Spec doc (commit) covers 9.
- **Type consistency**: `Mat4` API (Task 2) matches usage in `load_obj_mesh` (Task 3) and `parse_transform` (Task 4). `TriangleMesh` returned by `load_obj_mesh` matches what `scene.h` expects (Task 4).
- **Fallback chain**: tinyobjloader fails → catch → `load_obj_triangles` (legacy, scale+translate only) → if that throws, propagates to scene loader error handler. Documented in spec risk 5.
- **No placeholders**: All code blocks complete, all commands exact.
- **Backward compat**: Tasks 3, 4, 6 verify existing scenes still render.
- **Tinyobjloader warnings**: Task 3 Step 4 may show warnings from the library; acceptable since library is third-party. If they become noisy, suppress via `target_compile_options(raytracer PRIVATE -Wno-unused-parameter)` scoped to the third-party header (but not in this plan — only if needed).
