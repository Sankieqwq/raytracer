# 阶段 9：OBJ 模型加载与变换 — 设计文档

- **日期**：2026-06-28
- **阶段**：Stage 9（OBJ Model Loading & Transforms）
- **状态**：Draft（待用户 review）
- **作者**：hudaijin
- **关联总体规划**：CPU 光线追踪器 → PBR/BVH/CUDA 离线渲染器扩展（阶段 8-16）
- **前置阶段**：阶段 8 已完成（三角形与三角网格求交，含 `TriangleMesh` SoA 类型）

---

## 1. 背景与目标

阶段 8 已实现 `TriangleMesh`（SoA 布局）和 `triangle_intersect()` 自由函数。阶段 8 的 `mesh`（OBJ）类型仍使用**手写 OBJ 解析器**（`include/raytracer/scene/obj.h`），仅支持 `scale` + `translate`，无 UV、无旋转、无平滑法线计算。

阶段 9 目标：**用 tinyobjloader 替换手写解析器**，引入 **4x4 矩阵变换**（平移/旋转/缩放/轴角），将变换**烘焙**到世界坐标，让渲染器能正确渲染带变换的 OBJ 模型，同时**保留手写解析器作为 fallback**（tinyobjloader 加载失败时降级）。

### 1.1 阶段 9 验收标准（来自总体任务）

- [ ] 在 `math/` 新增 4x4 矩阵类型，支持平移/旋转（绕 XYZ 轴 + 欧拉角）/缩放构造，矩阵相乘、变换点、变换方向向量、变换法线（逆转置矩阵）
- [ ] 在 `scene/` 新增模型加载器，封装 tinyobjloader：读取 OBJ 顶点/法线/UV/面，三角化，生成网格对象
- [ ] OBJ 缺少法线时，根据面自动计算平滑法线（面积加权）
- [ ] 扩展场景 JSON：支持 `"type": "mesh"`，含 `"file"` 和 `"transform"` 字段（translate/rotate/scale）
- [ ] 加载时将变换矩阵应用到所有顶点和法线（烘焙到世界坐标）
- [ ] 现有 sphere 类型继续可用（向后兼容）
- [ ] 能在 JSON 中引用 OBJ 文件并正确渲染，变换参数生效

---

## 2. 现状分析

### 2.1 阶段 8 相关文件状态

| 文件 | 状态 | 阶段 9 关联 |
|------|------|------------|
| `include/raytracer/math/vec3.h` | Vec3 + Vec2 | 不动 |
| `include/raytracer/geometry/triangle_mesh.h` | SoA TriangleMesh | **被 OBJ 加载器填充** |
| `include/raytracer/geometry/triangle.h` | triangle_intersect 自由函数 | 不动 |
| `include/raytracer/scene/obj.h` | 手写 OBJ 解析器 | **保留作 fallback，新增 tinyobjloader 路径** |
| `include/raytracer/scene/scene.h` | mesh 类型用旧 obj.h | **扩展 file/transform 字段** |
| `src/main.cpp` | 主循环 | **加 TINYOBJLOADER_IMPLEMENTATION** |
| `CMakeLists.txt` | 单 main.cpp | **加 third_party include 路径** |
| `scenes/mark.json` | OBJ 场景（旧字段） | 保留兼容 |
| `models/mark.obj` | 46546 行，有 vn 无 vt | 验收用 |

### 2.2 mark.obj 格式确认

- 顶点 `v`：13586 个
- 法线 `vn`：14248 个（有法线）
- 纹理坐标 `vt`：0 个（无 UV）
- 面 `f`：12460 个（多为四边形 `f v//vn v//vn v//vn v//vn` 格式）

### 2.3 tinyobjloader 确认

- 已下载到 `/tmp/tiny_obj_loader.h`（13865 行，467KB）
- 实现宏：`TINYOBJLOADER_IMPLEMENTATION`
- 命名空间：`tinyobj`
- header-only，单文件，仅依赖 C++ STL

---

## 3. 关键设计决策（已确认）

### 决策 1：4x4 矩阵存储约定 — **行主序（选项 A）**

`Mat4` 用 `double m[4][4]`，`m[i][j]` 表示第 i 行第 j 列。变换点按行向量约定：`result[i] = sum_j m[i][j] * p[j]`（其中 p 齐次化为 `[x,y,z,1]`）。

**理由**：阅读直观，平移矩阵 `m[0][3]=tx, m[1][3]=ty, m[2][3]=tz`，符合多数教材和 OpenGL 习惯。

### 决策 2：旋转表示 — **同时支持欧拉角和轴角（选项 C）**

- `rotate: [rx, ry, rz]`：绕 X/Y/Z 三轴的欧拉角（度数），按 **XYZ 顺序**应用
- `rotate_axis: {axis: [x,y,z], angle: 30}`：单个轴-角（angle 为度数）

最终变换顺序（ baking 时）：`M = Translate * RotateAxis * RotateEuler * Scale`（先缩放，再旋转，最后平移——标准 SRT 顺序）。`rotate_axis` 在 `rotate` 之前应用（用户可灵活组合）。

### 决策 3：OBJ 加载后填入 TriangleMesh — **SoA 按面展开不去重（选项 A）**

每个 OBJ 加载为一个 `TriangleMesh` 对象。按面展开：每个三角形的 3 顶点独立存入 `vertices` 数组（不去重）。`vertices.size() == 3 * triangle_count`，`normals`/`uvs` 同长（若缺失则为空）。

**理由**：与阶段 8 SoA 设计一致；阶段 12 GPU 友好；实现简单（去重是优化，阶段 12 再考虑）。

### 决策 4：旧手写 obj.h — **保留作 fallback（选项 B）**

`scene.h` 的 `mesh` 类型加载流程：
1. 若 `file`/`obj`/`path` 字段存在，先用 tinyobjloader 加载
2. 若 tinyobjloader 抛异常或返回空，**降级到旧 `load_obj_triangles`**（独立 Triangle 对象路径）
3. 两者都失败才报错

**理由**：tinyobjloader 对某些边缘 OBJ 格式可能失败，保留 fallback 提高鲁棒性。

### 决策 5：tinyobjloader 实现宏 — **在 main.cpp 定义（选项 B）**

在 `src/main.cpp` 顶部，`#include` 任何项目头文件之前：
```cpp
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
```

**理由**：最小改动，`main.cpp` 已是唯一 `.cpp`，避免新增源文件。

### 决策 6：缺法线时平滑法线 — **面积加权（选项 B）**

每个顶点的平滑法线 = 共享该顶点的所有面法线**按面面积加权**平均后归一化。

**算法**：
1. 第一遍：遍历所有面，计算每个面的法线和面积
2. 累加到每个顶点：`vertex_normal[vid] += face_area * face_normal`
3. 归一化所有顶点法线

**理由**：比算术平均更准确（大面权重高），避免长条三角形扭曲法线。mark.obj 自带法线，但验收需要一个缺法线的测试 OBJ。

### 决策 7：变换时机 — **加载时烘焙（选项 A）**

OBJ 顶点读出后立刻乘变换矩阵，存入 `TriangleMesh` 的是世界坐标。法线用逆转置矩阵变换。

**理由**：严格按任务要求，简化后续 BVH/GPU 处理（运行时无需变换矩阵）。

### 决策 8：新旧字段共存 — **同时支持**

`mesh` 类型的字段优先级：
- 文件路径：`file` > `obj` > `path`（任一可用）
- 变换：`transform` 块 > 散落的 `scale`/`translate`
  - 若有 `transform`：用其中的 `translate`/`rotate`/`rotate_axis`/`scale`
  - 若无 `transform`：用顶层 `scale`（默认 1.0）和 `translate`（默认 [0,0,0]）
- 材质：`material` 字段（同阶段 8）

现有 `scenes/mark.json`（若用 `obj` + `scale` + `translate`）继续可用。

---

## 4. 详细设计

### 4.1 新增 `Mat4` 类型

文件：`include/raytracer/math/mat4.h`（新文件）

```cpp
#ifndef RT_MAT4_H
#define RT_MAT4_H

#include "raytracer/math/vec3.h"
#include <cmath>

class Mat4 {
public:
    double m[4][4];

    Mat4() {
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                m[i][j] = (i == j) ? 1.0 : 0.0;
    }

    // 静态构造函数
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
        r.m[1][1] = c; r.m[1][2] = -s;
        r.m[2][1] = s; r.m[2][2] = c;
        return r;
    }

    static Mat4 rotate_y(double deg) {
        double c = std::cos(degrees_to_radians(deg));
        double s = std::sin(degrees_to_radians(deg));
        Mat4 r;
        r.m[0][0] = c; r.m[0][2] = s;
        r.m[2][0] = -s; r.m[2][2] = c;
        return r;
    }

    static Mat4 rotate_z(double deg) {
        double c = std::cos(degrees_to_radians(deg));
        double s = std::sin(degrees_to_radians(deg));
        Mat4 r;
        r.m[0][0] = c; r.m[0][1] = -s;
        r.m[1][0] = s; r.m[1][1] = c;
        return r;
    }

    // 轴-角旋转（Rodrigues 公式），axis 需归一化
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

    // 矩阵相乘：this * other（行主序，先应用 other 再应用 this）
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

    // 变换点（齐次坐标 w=1）
    Vec3 transform_point(const Vec3& p) const {
        double x = m[0][0]*p.x + m[0][1]*p.y + m[0][2]*p.z + m[0][3];
        double y = m[1][0]*p.x + m[1][1]*p.y + m[1][2]*p.z + m[1][3];
        double z = m[2][0]*p.x + m[2][1]*p.y + m[2][2]*p.z + m[2][3];
        double w = m[3][0]*p.x + m[3][1]*p.y + m[3][2]*p.z + m[3][3];
        if (w != 0 && w != 1) { x /= w; y /= w; z /= w; }
        return Vec3(x, y, z);
    }

    // 变换方向向量（w=0，不受平移影响）
    Vec3 transform_direction(const Vec3& d) const {
        double x = m[0][0]*d.x + m[0][1]*d.y + m[0][2]*d.z;
        double y = m[1][0]*d.x + m[1][1]*d.y + m[1][2]*d.z;
        double z = m[2][0]*d.x + m[2][1]*d.y + m[2][2]*d.z;
        return Vec3(x, y, z);
    }

    // 变换法线：用逆转置矩阵的 3x3 部分
    Vec3 transform_normal(const Vec3& n) const {
        // 计算 3x3 子矩阵的逆 = adjugate / det；逆转置 = adjugate^T / det
        // 由于法线最后会归一化，det 缩放不影响方向，故只用 adjugate^T
        double a = m[0][0], b = m[0][1], c = m[0][2];
        double d = m[1][0], e = m[1][1], f = m[1][2];
        double g = m[2][0], h = m[2][1], i = m[2][2];
        // adjugate of 3x3 (转置后即逆转置的未归一化形式)
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

**实现说明**：
- `transform_normal` 用 3x3 子矩阵的伴随矩阵（已转置），省去除以行列式（法线后续会归一化，det 缩放不影响方向）。这等价于逆转置矩阵的 3x3 部分。
- `rotate_axis` 用 Rodrigues 公式。

### 4.2 重写 `obj.h`：tinyobjloader + 烘焙变换 + 平滑法线

文件：`include/raytracer/scene/obj.h`（重写）

设计要点：
- **保留** `load_obj_triangles`（旧手写解析器，fallback 用，签名不变）
- **新增** `load_obj_mesh`：用 tinyobjloader 加载，返回 `TriangleMesh`（已烘焙变换）

```cpp
#ifndef RT_OBJ_H
#define RT_OBJ_H

#include "raytracer/geometry/triangle_mesh.h"
#include "raytracer/math/mat4.h"
#include <stdexcept>
#include <string>
#include <vector>

// 旧手写解析器（fallback 用），返回独立三角形数据
struct ObjTriangleData {
    Point3 v0, v1, v2;
    Vec3 n0, n1, n2;
    bool has_normals = false;
};
std::vector<ObjTriangleData> load_obj_triangles(const std::string& path,
                                                 double scale = 1.0,
                                                 const Vec3& translate = Vec3(0, 0, 0));

// 新：用 tinyobjloader 加载，返回 SoA TriangleMesh（变换已烘焙）
// 若 OBJ 缺法线，按面积加权计算平滑法线
// 若 tinyobjloader 失败，抛 std::runtime_error（调用方可降级到 load_obj_triangles）
TriangleMesh load_obj_mesh(const std::string& path, const Mat4& transform);

#endif
```

`load_obj_mesh` 实现要点（在 `.h` 中 inline，或放 `obj.cpp`——但项目 header-only，故 inline）：

```cpp
inline TriangleMesh load_obj_mesh(const std::string& path, const Mat4& transform) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str())) {
        throw std::runtime_error("tinyobjloader failed: " + err + " " + warn);
    }

    TriangleMesh mesh;
    bool has_vn = !attrib.normals.empty();
    bool has_vt = !attrib.texcoords.empty();

    // 临时存储：每顶点法线累加（用于缺法线时平滑计算）
    // 由于按面展开，每顶点独立，平滑法线需在同顶点出现多次时共享
    // 用一个 map<vertex_index, Vec3> 累加加权法线
    std::vector<Vec3> smooth_normals;  // 仅 has_vn=false 时用
    if (!has_vn) {
        smooth_normals.assign(attrib.vertices.size() / 3, Vec3(0,0,0));
    }

    // 第一遍（仅缺法线时）：累加面积加权法线
    if (!has_vn) {
        for (const auto& shape : shapes) {
            size_t idx_offset = 0;
            for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
                int fv = shape.mesh.num_face_vertices[f];
                if (fv < 3) { idx_offset += fv; continue; }
                // 三角化（fan），取 v0,v1,v2
                tinyobj::index_t i0 = shape.mesh.indices[idx_offset + 0];
                tinyobj::index_t i1 = shape.mesh.indices[idx_offset + 1];
                tinyobj::index_t i2 = shape.mesh.indices[idx_offset + 2];
                Vec3 p0(attrib.vertices[3*i0.vertex_index], ...);
                Vec3 p1(...);
                Vec3 p2(...);
                Vec3 face_n = cross(p1-p0, p2-p0);
                double area = 0.5 * face_n.length();
                face_n = face_n.normalized();
                smooth_normals[i0.vertex_index] += area * face_n;
                smooth_normals[i1.vertex_index] += area * face_n;
                smooth_normals[i2.vertex_index] += area * face_n;
                idx_offset += fv;
            }
        }
        for (auto& n : smooth_normals) n = n.normalized();
    }

    // 第二遍：展开三角形，烘焙变换
    for (const auto& shape : shapes) {
        size_t idx_offset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            int fv = shape.mesh.num_face_vertices[f];
            for (int k = 1; k + 1 < fv; k++) {
                // fan triangulation: (0, k, k+1)
                tinyobj::index_t idx0 = shape.mesh.indices[idx_offset + 0];
                tinyobj::index_t idxk = shape.mesh.indices[idx_offset + k];
                tinyobj::index_t idxk1 = shape.mesh.indices[idx_offset + k + 1];

                auto get_pos = [&](const tinyobj::index_t& idx) {
                    return Vec3(attrib.vertices[3*idx.vertex_index + 0],
                               attrib.vertices[3*idx.vertex_index + 1],
                               attrib.vertices[3*idx.vertex_index + 2]);
                };
                Vec3 p0 = transform.transform_point(get_pos(idx0));
                Vec3 p1 = transform.transform_point(get_pos(idxk));
                Vec3 p2 = transform.transform_point(get_pos(idxk1));
                mesh.vertices.push_back(p0);
                mesh.vertices.push_back(p1);
                mesh.vertices.push_back(p2);
                mesh.indices.push_back(mesh.vertices.size() - 3);
                mesh.indices.push_back(mesh.vertices.size() - 2);
                mesh.indices.push_back(mesh.vertices.size() - 1);

                if (has_vn) {
                    auto get_n = [&](const tinyobj::index_t& idx) {
                        return Vec3(attrib.normals[3*idx.normal_index + 0],
                                    attrib.normals[3*idx.normal_index + 1],
                                    attrib.normals[3*idx.normal_index + 2]);
                    };
                    mesh.normals.push_back(transform.transform_normal(get_n(idx0)).normalized());
                    mesh.normals.push_back(transform.transform_normal(get_n(idxk)).normalized());
                    mesh.normals.push_back(transform.transform_normal(get_n(idxk1)).normalized());
                } else {
                    mesh.normals.push_back(transform.transform_normal(smooth_normals[idx0.vertex_index]).normalized());
                    mesh.normals.push_back(transform.transform_normal(smooth_normals[idxk.vertex_index]).normalized());
                    mesh.normals.push_back(transform.transform_normal(smooth_normals[idxk1.vertex_index]).normalized());
                }

                if (has_vt) {
                    auto get_uv = [&](const tinyobj::index_t& idx) {
                        return Vec2(attrib.texcoords[2*idx.texcoord_index + 0],
                                    attrib.texcoords[2*idx.texcoord_index + 1]);
                    };
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
```

### 4.3 `scene.h` 扩展：mesh 类型新字段

文件：`include/raytracer/scene/scene.h`

新增 `parse_transform` 辅助函数，构建 `Mat4`：

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
            M = M * Mat4::rotate_z(r.z) * Mat4::rotate_y(r.y) * Mat4::rotate_x(r.x);
        }
        if (t.has("translate")) {
            Vec3 tr = to_vec3(t.at("translate"));
            M = M * Mat4::translate(tr.x, tr.y, tr.z);
        }
    } else {
        // 兼容旧字段
        double s = obj.has("scale") ? obj.at("scale").numVal : 1.0;
        Vec3 tr = obj.has("translate") ? to_vec3(obj.at("translate")) : Vec3(0,0,0);
        M = Mat4::translate(tr.x, tr.y, tr.z) * Mat4::scale(s, s, s);
    }
    return M;
}
```

`mesh` 类型加载流程（替换现有 `else if (type == "mesh")` 块）：

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
        std::cerr << "tinyobjloader failed, fallback to legacy parser: " << e.what() << "\n";
    }
    if (!loaded) {
        // 降级到旧解析器
        double scale = obj.has("scale") ? obj.at("scale").numVal : 1.0;
        Vec3 translate = obj.has("translate") ? to_vec3(obj.at("translate")) : Vec3(0,0,0);
        std::vector<ObjTriangleData> tris = load_obj_triangles(obj_path, scale, translate);
        for (const ObjTriangleData& tri : tris) {
            std::unique_ptr<Triangle> mesh_tri;
            if (tri.has_normals) {
                mesh_tri = std::make_unique<Triangle>(tri.v0, tri.v1, tri.v2, tri.n0, tri.n1, tri.n2, mat);
            } else {
                mesh_tri = std::make_unique<Triangle>(tri.v0, tri.v1, tri.v2, mat);
            }
            scene.primitives.add(mesh_tri.get());
            scene.objects.push_back(std::move(mesh_tri));
        }
    }
}
```

### 4.4 `main.cpp` 修改

在 `src/main.cpp` 最顶部（所有 include 之前）加：

```cpp
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
```

### 4.5 `CMakeLists.txt` 修改

加 `third_party/` 到 include 路径：

```cmake
cmake_minimum_required(VERSION 3.10)
project(Raytracer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(raytracer src/main.cpp)
target_include_directories(raytracer PRIVATE include third_party)
target_compile_options(raytracer PRIVATE -O2 -Wall -Wextra)
```

### 4.6 tinyobjloader 放置

文件：`third_party/tiny_obj_loader.h`（从 `/tmp/tiny_obj_loader.h` 复制）

---

## 5. 验收场景

### 5.1 主验收场景：`scenes/bunny_test.json`

复用 `models/mark.obj`，带变换：

```json
{
    "image": {
        "width": 400, "height": 400, "samples": 16, "max_depth": 32,
        "output": "bunny_test.ppm"
    },
    "camera": {
        "lookfrom": [0, 0, 5], "lookat": [0, 0, 0], "vup": [0, 1, 0],
        "vfov": 30, "aperture": 0.0, "focus_dist": 5.0
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
            "material": { "type": "lambertian", "albedo": [0.8, 0.6, 0.2] }
        }
    ]
}
```

**验证项**：
1. 编译通过
2. mark.obj 正确加载（13586 顶点 → 约 12460 三角形）
3. transform 生效（模型位置/朝向正确）
4. 顶点法线被正确变换（逆转置矩阵），光照平滑

### 5.2 缺法线 OBJ 测试：`scenes/no_normal_obj.json`

新建一个简单无法线的 OBJ（手写）：

`models/cube_no_normal.obj`:
```
# 立方体，无法线，测试平滑法线计算
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

场景 JSON 引用它，验证：
1. tinyobjloader 加载成功
2. 平滑法线按面积加权计算（立方体共边面法线加权平均，角点法线呈各面法线平均方向）
3. 渲染无可见瑕疵（无明显黑斑或法线翻转）

### 5.3 向后兼容验证

- `scenes/default.json`：渲染不变
- `scenes/three_balls.json`：渲染不变
- `scenes/triangle_test.json`：渲染不变
- `scenes/mark.json`（若存在，旧字段 `obj` + `scale` + `translate`）：渲染不变

---

## 6. 不在本阶段范围

- PBR 材质与纹理（阶段 11）
- BVH 线性化（阶段 10）
- glTF 加载（阶段 16）
- 顶点去重（阶段 12 优化）
- mtl 材质文件解析（阶段 11 配合 PBR）

---

## 7. 风险与注意事项

1. **tinyobjloader 编译警告**：库本身可能有 warning，CMake 已有 `-Wall -Wextra`。若 tinyobjloader 产生大量 warning，考虑用 `target_compile_options` 单独抑制（但先观察，不过度处理）。
2. **`transform_normal` 逆转置实现**：用伴随矩阵法（省 det 除法），法线后续归一化保证方向正确。但若矩阵含非均匀缩放 + 镜像（det<0），法线方向可能翻转。**用 `dot(transformed_normal, transformed_face_normal) > 0` 检测，翻转则取反**——但简化起见，本阶段不处理镜像（用户不应在 OBJ 变换中用负缩放）。若需处理，后续加。
3. **tinyobjloader `normal_index = -1`**：某些面可能部分顶点有法线部分无。tinyobjloader 已处理为 `-1`，本实现假设要么全有要么全无（mark.obj 是全有）。若混合，`has_vn` 判断应改为 per-face 检查。本阶段按整体判断，若遇混合 OBJ 会 fallback 到旧解析器。
4. **面积加权法线的法线方向**：面法线 `cross(p1-p0, p2-p0)` 方向取决于顶点顺序。OBJ 面顶点顺序约定 CCW（逆时针，从外侧看）。若 OBJ 用 CW 顺序，法线会朝内。**通过 `set_face_normal` 的 `front_face` 判断处理**：ray_color 中法线已根据射线方向翻转，故 OBJ 顶点顺序问题不影响最终渲染（但会影响平滑法线的"平均方向"——若面法线朝向不一致，平均后可能接近零）。**对此，平滑法线累加后若模长接近零，fallback 到面法线**：
   ```cpp
   for (auto& n : smooth_normals) {
       if (n.length_squared() < 1e-12) n = Vec3(0, 1, 0);  // fallback
       else n = n.normalized();
   }
   ```
5. **fallback 路径的变换支持不全**：旧 `load_obj_triangles` 仅支持 scale + translate，不支持 rotate/rotate_axis。若 tinyobjloader 失败且用了 rotate，fallback 会忽略旋转。**降级时打印 warning 提示用户**。
6. **`mark.obj` 的 mtl 引用**：`mtllib mark.mtl` 会被 tinyobjloader 解析但忽略（materials 数组填充但本阶段不用材质）。若 `mark.mtl` 文件不存在，tinyobjloader 产生 warning 但不失败。

---

## 8. 实施顺序

1. 复制 tinyobjloader 到 `third_party/`
2. 新建 `include/raytracer/math/mat4.h`
3. 重写 `include/raytracer/scene/obj.h`（保留旧函数 + 新增 `load_obj_mesh`）
4. 修改 `src/main.cpp`（加 TINYOBJLOADER_IMPLEMENTATION）
5. 修改 `CMakeLists.txt`（加 third_party include）
6. 修改 `include/raytracer/scene/scene.h`（parse_transform + mesh 类型新逻辑）
7. 新建 `models/cube_no_normal.obj` + `scenes/no_normal_obj.json`
8. 新建 `scenes/bunny_test.json`
9. 编译 + 验证全部场景
10. 更新 `README.md`
11. Git commit

---

## 9. 验证清单

- [ ] `cmake -S . -B build && cmake --build build` 编译通过，无新 warning
- [ ] `./build/raytracer --scene scenes/default.json` 输出不变
- [ ] `./build/raytracer --scene scenes/three_balls.json` 输出不变
- [ ] `./build/raytracer --scene scenes/triangle_test.json` 输出不变
- [ ] `./build/raytracer --scene scenes/bunny_test.json` 渲染 mark.obj，transform 生效
- [ ] `./build/raytracer --scene scenes/no_normal_obj.json` 渲染立方体，平滑法线无瑕疵
- [ ] README「当前实现状态」更新阶段 9 完成项
- [ ] Git commit：`Stage 9: OBJ loading with tinyobjloader + 4x4 transform`

---

## 10. 后续阶段衔接

| 后续阶段 | 本阶段如何铺垫 |
|---------|---------------|
| 阶段 10（BVH） | `TriangleMesh::bounding_box` 已就绪，OBJ 加载后作为整体进 BVH |
| 阶段 11（PBR 材质） | tinyobjloader 已解析 mtl（存入 materials 数组），阶段 11 可读取 PBR 参数 |
| 阶段 12（GPU SoA） | `TriangleMesh` 已是 SoA，`load_obj_mesh` 直接填充，无需重构 |
| 阶段 16（glTF） | 加载器架构（`load_*_mesh` 函数 + `Mat4` 变换）可复用 |
