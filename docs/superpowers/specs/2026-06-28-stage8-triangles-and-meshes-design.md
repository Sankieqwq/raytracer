# 阶段 8：三角形与三角网格求交 — 设计文档

- **日期**：2026-06-28
- **阶段**：Stage 8（Triangles & Triangle Meshes）
- **状态**：Draft（待用户 review）
- **作者**：hudaijin
- **关联总体规划**：CPU 光线追踪器 → PBR/BVH/CUDA 离线渲染器扩展（阶段 8-16）

---

## 1. 背景与目标

现有项目是一个 header-only 的 C++ 光线追踪器，已完成数学库、sphere 求交、`hittable_list`、Lambertian/Metal/Dielectric 材质、camera、PPM 输出、JSON 场景加载、抗锯齿。

经探索发现：**阶段 8/9/10 的部分实现已存在于代码库**（`aabb.h`、`triangle.h`、`bvh.h`、`obj.h`），但不完整且不完全满足任务规范。本阶段目标是**补全阶段 8 的所有缺口**，让渲染器能正确渲染由三角形构成的几何体，同时为后续阶段（BVH 加速、GPU 数据结构重构）做好铺垫。

### 1.1 阶段 8 验收标准（来自总体任务）

- [x] AABB 提供 slab 求交、合并两个盒子
- [ ] AABB 提供计算表面积方法（为 SAH 准备）← **需补**
- [x] 三角形使用 Möller–Trumbore 算法求交
- [ ] 三角形存储顶点位置/法线/**UV 坐标** ← UV 需补
- [ ] 命中时用重心坐标插值出交点的法线和 **UV**，写入 HitRecord ← UV 插值需补
- [ ] **扩展 HitRecord，增加 UV 坐标字段** ← 需补
- [ ] **新增三角网格类型**（顶点/法线/UV 数组 + 索引数组 + 每三角形材质索引），求交先遍历所有三角形 ← 需补
- [x] 三角形提供获取自身 AABB 的方法
- [ ] 能在 JSON 中手写一个三角形或一组三角形并正确渲染 ← 需补
- [ ] 平滑法线插值正确（已有，需在新网格类型中复用）

---

## 2. 现状分析

### 2.1 现有文件清单

| 文件 | 现状 | 阶段 8 关联 |
|------|------|------------|
| `include/raytracer/math/vec3.h` | Vec3 + Point3/Color 别名 + 基础运算 | 需新增 Vec2 |
| `include/raytracer/math/util.h` | 常量、随机数 | 不动 |
| `include/raytracer/math/ray.h` | Ray 类 | 不动 |
| `include/raytracer/geometry/aabb.h` | slab hit + longest_axis + surrounding_box | **加 surface_area** |
| `include/raytracer/geometry/hittable.h` | HitRecord + Hittable 抽象基类 | **HitRecord 加 UV 字段** |
| `include/raytracer/geometry/hittable_list.h` | 线性遍历 | 不动 |
| `include/raytracer/geometry/sphere.h` | 球求交 | 不动 |
| `include/raytracer/geometry/triangle.h` | Möller–Trumbore + 顶点法线插值 | **加 UV 存储与插值** |
| `include/raytracer/geometry/bvh.h` | 指针式递归 BVH（阶段 10 范畴） | 本阶段不动 |
| `include/raytracer/material/material.h` | Lambert/Metal/Dielectric | 不动 |
| `include/raytracer/render/camera.h` | 相机 | 不动 |
| `include/raytracer/render/image.h` | PPM 输出 | 不动 |
| `include/raytracer/scene/json.h` | 最小 JSON 解析器 | 不动 |
| `include/raytracer/scene/obj.h` | 手写 OBJ 解析（阶段 9 将换 tinyobjloader） | 本阶段不动 |
| `include/raytracer/scene/scene.h` | 场景加载 | **扩展 triangle/triangles 类型** |
| `src/main.cpp` | 主循环 | 不动 |
| `scenes/default.json` / `three_balls.json` | 现有场景 | 不动 |

### 2.2 Gap 表（阶段 8 范围）

| 要求 | 当前状态 | 缺口 |
|------|---------|------|
| AABB surface_area | ❌ | 新增 `surface_area()` |
| 三角形 UV 存储 | ❌ | 加 `Vec2 uv0/uv1/uv2; bool has_uvs;` |
| HitRecord UV 字段 | ❌ | 加 `double u = 0, v = 0;` |
| UV 重心坐标插值 | ❌ | 在 `Triangle::hit` 中补 |
| 三角网格类型 | ❌ | 新建 `triangle_mesh.h` |
| JSON 三角形类型 | ❌ | 扩展 `scene.h` |
| 验收场景 | ❌ | 新建 `scenes/triangle_test.json` |

---

## 3. 关键设计决策

### 决策 1：TriangleMesh 求交方式 — **SoA 布局（选项 B）**

`TriangleMesh` 内部采用 Structure-of-Arrays 布局：

```
vertices:  std::vector<Point3>   // 共享顶点位置
normals:   std::vector<Vec3>     // 共享顶点法线（可空）
uvs:       std::vector<Vec2>     // 共享顶点 UV（可空）
indices:   std::vector<int>      // 每 3 个一组引用上面三个数组
material_per_tri: std::vector<Material*>  // 每三角形一个材质
```

**理由**：
- 与阶段 12「面向 GPU 的数据结构重构」目标方向一致，届时可直接复用为设备端 SoA
- 内存紧凑，缓存友好（遍历连续顶点数据）
- 避免每三角形独立对象带来的指针追逐
- 三角形求交逻辑提取为自由函数 `triangle_intersect()`，`Triangle` 与 `TriangleMesh` 共用，消除重复

**求交流程**：
1. 遍历 `indices` 每 3 个一组
2. 取出 v0/v1/v2、n0/n1/n2（若有）、uv0/uv1/uv2（若有）
3. 调 `triangle_intersect()` 得到 t、重心坐标 (b1, b2)（其中 b0 = 1-b1-b2）
4. 命中则插值法线/UV，写入 HitRecord，返回

### 决策 2：无 UV 时的默认值 — **用重心坐标作为 UV（选项 B）**

当三角形没有 UV 数据时，命中后 `HitRecord.u` 写重心坐标 b1，`HitRecord.v` 写重心坐标 b2。

**理由**：
- 方便调试：在材质中用 `Color(rec.u, rec.v, 0)` 可直接可视化重心坐标分布
- 无副作用：当材质不引用 UV 时，写入任何值都不影响渲染结果
- 无需额外标志位区分"有 UV / 无 UV"

### 决策 3：向后兼容 — **接受过渡方案**

- 现有 `sphere` 类型：完全不动
- 现有 `mesh`（OBJ）类型：继续走 `obj.h` 手写解析器（无 UV），阶段 9 再换 tinyobjloader 并补 UV
- 现有 `default.json` / `three_balls.json`：渲染结果与重构前像素一致
- `HitRecord` 新增字段带默认值，现有 `sphere.h` / `obj.h` 不需改动即可继续工作（UV 保持默认 0,0）

---

## 4. 详细设计

### 4.1 新增 `Vec2` 类型

文件：`include/raytracer/math/vec3.h`（追加，不修改 Vec3）

```cpp
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

### 4.2 `AABB::surface_area()`

文件：`include/raytracer/geometry/aabb.h`（追加方法）

```cpp
double surface_area() const {
    Vec3 d = maximum - minimum;
    return 2.0 * (d.x * d.y + d.y * d.z + d.z * d.x);
}
```

### 4.3 `HitRecord` 扩展

文件：`include/raytracer/geometry/hittable.h`

```cpp
struct HitRecord {
    Point3 p;
    Vec3 normal;
    double t = 0;
    double u = 0;          // ← 新增
    double v = 0;          // ← 新增
    bool front_face = true;
    Material* material = nullptr;
    // set_face_normal 不变
};
```

### 4.4 `Triangle` 扩展

文件：`include/raytracer/geometry/triangle.h`

新增字段：
```cpp
Vec2 uv0, uv1, uv2;
bool has_uvs = false;
```

新增构造函数：
```cpp
Triangle(const Point3& a, const Point3& b, const Point3& c,
         const Vec3& na, const Vec3& nb, const Vec3& nc,
         const Vec2& t0, const Vec2& t1, const Vec2& t2,
         Material* material = nullptr);
```

`hit()` 修改（在已有重心坐标 u/v 基础上）：
```cpp
double b0 = 1.0 - u - v;  // 重心坐标三权重
if (has_uvs) {
    rec.u = (b0 * uv0 + u * uv1 + v * uv2).x;
    rec.v = (b0 * uv0 + u * uv1 + v * uv2).y;
} else {
    rec.u = u;  // 用重心坐标作为 UV
    rec.v = v;
}
```

> 注意：现有代码中变量 `u`/`v` 表示重心坐标（Möller–Trumbore 约定），与 `HitRecord.u/v`（纹理坐标）语义不同。保留现有变量名以减少改动，仅在新代码处用 `b0/b1/b2` 注释明确。

### 4.5 三角形求交自由函数

文件：`include/raytracer/geometry/triangle.h`（追加）

```cpp
// Möller–Trumbore 射线-三角形求交，返回是否命中及重心坐标
// 命中时 t/b1/b2 写入输出参数；b0 = 1 - b1 - b2
inline bool triangle_intersect(const Ray& r, double t_min, double t_max,
                              const Point3& v0, const Point3& v1, const Point3& v2,
                              double& t, double& b1, double& b2);
```

`Triangle::hit` 改为调用此函数，消除重复实现。`TriangleMesh` 也调用此函数。

### 4.6 新增 `TriangleMesh`

文件：`include/raytracer/geometry/triangle_mesh.h`（新文件）

```cpp
#ifndef RT_TRIANGLE_MESH_H
#define RT_TRIANGLE_MESH_H

#include "raytracer/geometry/hittable.h"
#include "raytracer/geometry/triangle.h"  // triangle_intersect
#include "raytracer/math/vec3.h"
#include <vector>

class TriangleMesh : public Hittable {
public:
    std::vector<Point3> vertices;
    std::vector<Vec3> normals;       // 可空（size == 0 表示无顶点法线）
    std::vector<Vec2> uvs;           // 可空
    std::vector<int> indices;         // size 必须是 3 的倍数
    std::vector<Material*> material_per_tri;  // size == indices.size()/3
    AABB bbox;
    TriangleMesh() = default;

    // 命中时遍历所有三角形（暂不加速，阶段 10 上 BVH）
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
            // 法线
            Vec3 outward;
            if (!normals.empty()) {
                outward = (b0 * normals[i0] + b1 * normals[i1] + b2 * normals[i2]).normalized();
            } else {
                outward = cross(vertices[i1]-vertices[i0], vertices[i2]-vertices[i0]).normalized();
            }
            tmp.set_face_normal(r, outward);
            // UV
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

    // const 方法，用 mutable 字段支持惰性计算（避免 const_cast）
    void compute_bbox() const {
        if (vertices.empty()) { bbox_ = AABB(); bbox_computed_ = true; return; }
        Point3 mn = vertices[0], mx = vertices[0];
        for (const Point3& p : vertices) {
            mn = Point3(std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z));
            mx = Point3(std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z));
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

### 4.7 JSON 场景扩展

文件：`include/raytracer/scene/scene.h`

新增两种物体类型：

#### 4.7.1 `type: "triangle"`（单三角形）

```json
{
    "type": "triangle",
    "vertices": [[0,0,0], [1,0,0], [0,1,0]],
    "normals": [[0,0,1], [0,0,1], [0,0,1]],   // 可选
    "uvs": [[0,0], [1,0], [0,1]],             // 可选
    "material": { "type": "lambertian", "albedo": [0.8, 0.2, 0.2] }
}
```

解析后构造一个 `Triangle` 对象加入 `primitives`（与现有 OBJ 三角形一致的处理路径）。

#### 4.7.2 `type: "triangles"`（三角形网格，手写共享顶点 + 索引）

```json
{
    "type": "triangles",
    "vertices": [[0,0,0], [1,0,0], [1,1,0], [0,1,0]],
    "indices": [0, 1, 2,  0, 2, 3],
    "normals": [[0,0,1], [0,0,1], [0,0,1], [0,0,1]],  // 可选
    "uvs": [[0,0], [1,0], [1,1], [0,1]],              // 可选
    "material": { "type": "lambertian", "albedo": [0.8, 0.8, 0.2] }
}
```

解析后构造一个 `TriangleMesh` 对象，所有三角形共享同一材质（`material_per_tri` 填充同一指针），加入 `primitives`。

#### 4.7.3 现有类型保持不变

- `sphere`：完全不动
- `mesh`（OBJ）：继续走 `obj.h`，本阶段不引入 UV

### 4.8 验收场景

文件：`scenes/triangle_test.json`（新文件）

```json
{
    "image": {
        "width": 800, "height": 400, "samples": 32, "max_depth": 32,
        "output": "triangle_test.ppm"
    },
    "camera": {
        "lookfrom": [0, 0, 0], "lookat": [0, 0, -1], "vup": [0, 1, 0],
        "vfov": 60, "aperture": 0.0, "focus_dist": 1.0
    },
    "objects": [
        {
            "type": "triangles",
            "vertices": [[-2,-1,-2],[2,-1,-2],[2,1,-2],[-2,1,-2]],
            "indices": [0,1,2, 0,2,3],
            "uvs": [[0,0],[1,0],[1,1],[0,1]],
            "material": { "type": "lambertian", "albedo": [0.9,0.9,0.9] }
        },
        {
            "type": "sphere",
            "center": [0, 0, -1.2], "radius": 0.4,
            "material": { "type": "lambertian", "albedo": [0.1,0.2,0.5] }
        }
    ]
}
```

**验证项**：
1. 编译通过，无 warning
2. `default.json` 渲染与重构前一致（向后兼容）
3. `triangle_test.json` 渲染出四边形地面 + 球（纯色材质，UV 已写入 HitRecord 但不影响纯色渲染）
4. **UV 可视化测试**（单独步骤）：临时新建 `scenes/triangle_uv_debug.json`，四边形材质用一个临时新增的 `"type": "uv_debug"` 材质（在 `material.h` 中临时加一个 `UvDebugMaterial`，`scatter` 时把 `attenuation = Color(rec.u, rec.v, 0)`）。渲染后四边形上应呈现 RGB 渐变（左下黑、右下红、右上黄、左上绿），验证 UV 插值正确。**此材质仅用于验证，验收后删除，不入主分支**。

---

## 5. 不在本阶段范围

以下属于后续阶段，**本阶段不实现**：

- BVH 线性化重构（阶段 10）
- tinyobjloader 替换手写 OBJ 解析（阶段 9）
- 4x4 矩阵与变换（阶段 9）
- PBR 材质与纹理（阶段 11）
- 材质/图元去多态 SoA 重构（阶段 12）
- CUDA 移植（阶段 13+）

但本阶段的 SoA `TriangleMesh` 设计**已为阶段 12 对齐**，无需返工。

---

## 6. 风险与注意事项

1. **`HitRecord` 新增字段的默认值**：`u=0, v=0`，确保现有 `sphere.h` / `obj.h` 不写 UV 也能正常工作。
2. **变量命名歧义**：`Triangle::hit` 中 Möller–Trumbore 的 `u`/`v` 是重心坐标，与 `HitRecord.u/v`（纹理坐标）语义不同。保留现有变量名以减少 diff，仅在新增 UV 插值代码处加注释说明。可在后续阶段统一改名。
3. **`TriangleMesh` 惰性 bbox 计算**：`bounding_box()` 是 `const` 但需惰性计算。用 `mutable` 字段（`bbox_` / `bbox_computed_`）实现，避免 `const_cast`。
4. **索引越界**：`TriangleMesh::hit` 需校验 `indices` 大小为 3 的倍数、`material_per_tri` 大小匹配，加载时断言。
5. **空网格**：`vertices` 为空时 `bounding_box` 返回默认 AABB，加载时也应断言。
6. **向后兼容验证**：必须用 `default.json` 和 `three_balls.json` 实跑，确认像素无变化（UV 字段对纯色材质无影响，理论上一致）。

---

## 7. 验证清单

- [ ] `cmake -S . -B build && cmake --build build` 编译通过，无 warning
- [ ] `./build/raytracer --scene scenes/default.json` 输出 `out.ppm`，与重构前一致
- [ ] `./build/raytracer --scene scenes/three_balls.json` 输出与重构前一致
- [ ] `./build/raytracer --scene scenes/triangle_test.json` 输出 `triangle_test.ppm`，四边形地面正确渲染
- [ ] UV 插值验证：临时调试材质渲染 `(u,v,0)` 渐变图，确认插值正确
- [ ] `README.md`「当前实现状态」更新阶段 8 完成项
- [ ] Git commit：`Stage 8: triangles and triangle meshes`

---

## 8. 后续阶段衔接

| 后续阶段 | 本阶段如何铺垫 |
|---------|---------------|
| 阶段 9（OBJ 加载） | `TriangleMesh` 已就绪，tinyobjloader 读取后填充 SoA 即可；4x4 矩阵变换顶点/法线 |
| 阶段 10（BVH） | `TriangleMesh::bounding_box` 已提供，可整体作为一个图元进 BVH，也可拆分为单三角形进 BVH；线性 BVH 数据结构在本阶段后构建 |
| 阶段 12（GPU SoA 重构） | `TriangleMesh` 已是 SoA，可直接拷贝到显存；`triangle_intersect` 是自由函数，加 `RT_HOSTDEV` 即可设备端复用 |

---

## 9. 实施顺序

1. `vec3.h` 加 `Vec2`
2. `aabb.h` 加 `surface_area`
3. `hittable.h` 加 `HitRecord.u/v`
4. `triangle.h` 提取 `triangle_intersect` 自由函数 + `Triangle` 加 UV 字段/构造/插值
5. 新建 `triangle_mesh.h`
6. `scene.h` 扩展 `triangle`/`triangles` 类型解析
7. 新建 `scenes/triangle_test.json`
8. 编译 + 三场景验证
9. 更新 `README.md`
10. Git commit
