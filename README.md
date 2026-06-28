# Raytracer

一个从零开始的 C++ 光线追踪渲染器，零外部依赖，输出 PPM 图片文件。
场景通过 JSON 文件配置，支持命令行参数覆盖。
适合学习渲染底层原理：射线-几何求交、漫反射、递归反射/折射、抗锯齿、相机投影。

## 快速开始

### 编译

方式一：CMake（推荐）

```bash
cmake -S . -B build
cmake --build build
```

方式二：直接 g++（CMake 未装时可用）

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude -o raytracer src/main.cpp
```

### 运行

```bash
# 用默认场景渲染
./raytracer

# 指定场景文件
./raytracer --scene scenes/three_balls.json

# 覆盖输出路径和采样数
./raytracer --scene scenes/three_balls.json --out my.ppm --samples 64

# 渲染 OBJ 模型场景
./raytracer --scene scenes/mark.json
```

命令行参数：

| 参数 | 含义 | 默认值 |
|------|------|--------|
| `--scene <path>` | 场景 JSON 文件 | `scenes/default.json` |
| `--out <path>` | 输出 PPM 文件（覆盖场景配置） | 场景中的 `output` |
| `--samples <n>` | 每像素采样数（覆盖场景配置） | 场景中的 `samples` |
| `--help` | 显示帮助 | — |

### 预览

PPM 可被多数看图软件直接打开。macOS 下转 PNG：

```bash
sips -s format png out.ppm --out out.png
open out.png
```

也可以使用项目自带脚本自动转换：

```bash
# 默认把 out.ppm 转成 out.png
./scripts/ppm_to_png.sh

# 指定输入文件，输出名默认同名 .png
./scripts/ppm_to_png.sh three_balls.ppm

# 同时指定输入和输出
./scripts/ppm_to_png.sh three_balls.ppm preview.png
```

## 场景文件格式

场景用 JSON 描述，包含 `image`、`camera`、`objects` 三部分。见 `scenes/` 目录示例。

```json
{
    "image": {
        "width": 800,
        "height": 400,
        "samples": 16,
        "max_depth": 32,
        "output": "out.ppm"
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
            "type": "sphere",
            "center": [0, 0, -1],
            "radius": 0.5,
            "material": {
                "type": "lambertian",
                "albedo": [0.1, 0.2, 0.5]
            }
        }
    ]
}
```

### 字段说明

**image**（均可选，有默认值）

| 字段　　　　| 类型　 | 默认值　　　| 说明　　　　　　　　　　　　　 |
| -------------| --------| -------------| --------------------------------|
| `width`　　 | int　　| 800　　　　 | 图像宽度　　　　　　　　　　　 |
| `height`　　| int　　| 400　　　　 | 图像高度　　　　　　　　　　　 |
| `samples`　 | int　　| 16　　　　　| 每像素采样数，越大越细腻、越慢 |
| `max_depth` | int　　| 32　　　　　| 光线递归深度上限　　　　　　　 |
| `output`　　| string | `"out.ppm"` | 输出文件路径　　　　　　　　　 |

**camera**（均可选，有默认值）

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `lookfrom` | [x,y,z] | [0,0,0] | 相机位置 |
| `lookat` | [x,y,z] | [0,0,-1] | 相机看向的点 |
| `vup` | [x,y,z] | [0,1,0] | 相机上方向 |
| `vfov` | float | 60 | 垂直视野角（度） |
| `aperture` | float | 0.0 | 光圈孔径，0 = 无景深 |
| `focus_dist` | float | 1.0 | 对焦距离 |

**objects**（数组，每个元素是一个物体）

| 字段 | 类型 | 说明 |
|------|------|------|
| `type` | string | 目前支持 `"sphere"` |
| `center` | [x,y,z] | 球心坐标 |
| `radius` | float | 半径 |
| `material` | object | 材质定义，见下 |

**material.type** 支持三种：

| 类型 | 额外字段 | 说明 |
|------|----------|------|
| `"lambertian"` | `albedo`: [r,g,b] | 漫反射（哑光） |
| `"metal"` | `albedo`: [r,g,b], `fuzz`: 0~1 | 金属反射，fuzz=0 镜面，越大越粗糙 |
| `"dielectric"` | `ior`: float | 玻璃/水等透明介质，ior=1.5 玻璃，1.33 水 |

颜色 `albedo` 各分量范围 [0, 1]。

### 示例场景

| 文件　　　　　　　　　　　| 内容　　　　　　　　　　　　　　　|
| ---------------------------| -----------------------------------|
| `scenes/default.json`　　 | 漫反射球 + 地面（基础验证）　　　 |
| `scenes/three_balls.json` | 漫反射 + 玻璃 + 金属 三球场景　　 |
| `scenes/mark.json`　　　　| 加载 `models/mark.obj` 的网格场景 |
| `scenes/triangle_test.json` | 三角形 + 三角网格（验证 UV 插值） |
| `scenes/bunny_test.json` | OBJ 网格 + 变换（mark.obj） |
| `scenes/no_normal_obj.json` | 无法线 OBJ + 平滑法线 |

## OBJ 网格支持

当前版本已支持在场景 JSON 中通过 `mesh` 对象加载 Wavefront OBJ 模型，并输出渲染图片。

- 加载器：[tinyobjloader](https://github.com/tinyobjloader/tinyobjloader)（单头文件，位于 `third_party/`），加载失败时自动降级到内置手写解析器
- 支持 `v`、`vn`、`vt`、`f`，三角化任意多边形面
- 缺法线时自动按面积加权计算平滑法线
- `mesh` 对象字段：`file`/`obj`/`path`（任一）+ `transform` + `material`

### 变换（阶段 9）

### 变换（阶段 9）

`transform` 块包含可选的 `translate`/`rotate`/`rotate_axis`/`scale`，按 SRT 顺序（先缩放，再旋转，最后平移）烘焙到顶点：

```json
{
    "type": "mesh",
    "file": "../models/mark.obj",
    "transform": {
        "translate": [-0.5, 0, 0],
        "rotate": [0, 180, 0],
        "rotate_axis": { "axis": [0, 1, 0], "angle": 45 },
        "scale": 0.2
    },
    "material": { "type": "lambertian", "albedo": [0.8, 0.6, 0.2] }
}
```

- `translate`: `[x, y, z]` 平移
- `rotate`: `[rx, ry, rz]` 欧拉角（度），XYZ 顺序
- `rotate_axis`: `{axis: [x,y,z], angle: 度数}` 轴角旋转（在 `rotate` 之前应用）
- `scale`: 数值（统一缩放）或 `[sx, sy, sz]`（非统一缩放）

### 向后兼容

旧字段 `obj` + 顶层 `scale` + 顶层 `translate` 仍然可用（仅支持缩放和平移，无旋转）。示例：

```json
{
    "type": "mesh",
    "obj": "../models/mark.obj",
    "scale": 0.18,
    "translate": [-0.462, -0.052, 0.0],
    "material": { "type": "lambertian", "albedo": [0.75, 0.2, 0.2] }
}
```

## BVH 加速结构

场景图元超过 1 个时自动构建线性 BVH 加速射线求交。

- **线性节点**：`LinearBVHNode` 为 POD 结构（AABB + 左右子索引 + 叶子图元起止），可直接 `memcpy` 到 GPU
- **构建**：中点划分（按最长轴排序取中点），叶子粒度 1 图元
- **遍历**：栈数组迭代（深度 64），与未来 GPU 栈遍历逻辑一致
- **结果一致性**：因 `hit()` 取最近命中，遍历顺序变化不影响渲染结果

后续阶段（12/14）将复用此线性结构与栈遍历逻辑实现 GPU BVH。

## 项目结构

```
raytracer/
├── CMakeLists.txt                构建配置
├── include/raytracer/            头文件库（header-only，按模块分目录）
│   ├── math/                     模块 A：数学库
│   │   ├── util.h                常量 (pi/infinity)、随机数、角度换算
│   │   ├── vec3.h                三维向量（兼作 Point3 / Color）、reflect/refract
│   │   └── ray.h                 射线：origin + direction，at(t)
│   ├── geometry/                 模块 B：几何求交
│   │   ├── hittable.h            可命中物体抽象基类 + HitRecord
│   │   ├── sphere.h              球体：射线-球求交
│   │   └── hittable_list.h       物体列表：遍历找最近交点
│   ├── material/                 模块 C：材质
│   │   └── material.h            Lambertian / Metal / Dielectric
│   ├── render/                   模块 D：渲染
│   │   ├── camera.h              相机：FOV、lookat、景深
│   │   └── image.h               PPM 写入 + 伽马校正
│   └── scene/                    模块 D：场景
│       ├── json.h                最小 JSON 解析器（零依赖）
│       └── scene.h               场景加载器：JSON → Camera + HittableList
├── src/main.cpp                  命令行解析 + 渲染主循环 + ray_color 递归
├── scenes/                       场景文件目录
│   ├── default.json
│   └── three_balls.json
└── out.ppm                       渲染输出（运行后生成）
```

头文件 include 路径统一为 `#include "raytracer/<模块>/<文件>.h"`，例如 `#include "raytracer/math/vec3.h"`。

## 模块划分（四人协作）

| 模块 | 目录/文件 | 职责 |
|------|-----------|------|
| **A 数学库** | `include/raytracer/math/` | 向量、射线、随机数、常量。全项目地基 |
| **B 几何求交** | `include/raytracer/geometry/` | 物体抽象、球求交、列表遍历 |
| **C 材质着色** | `include/raytracer/material/` | Lambert / Metal / Dielectric 的 scatter |
| **D 相机/输出** | `include/raytracer/render/` `include/raytracer/scene/` `src/` | 主射线、PPM 写入、JSON 解析、场景加载、渲染循环 |

依赖方向（无环）：

```
A 数学库
  └─> B 几何 ──(持 Material* 指针，前向声明)──> C 材质
        └──────────────> D 相机/渲染/输出 (集成 B、C，调 A)
```

每人改自己模块的子目录，文件不重叠，合并零冲突。

## 接口契约（签名冻结，改接口需团队协商）

- `HitRecord` 字段：`p / normal / t / u / v / front_face / Material*`（`u`/`v` 为纹理坐标，阶段 8 新增）
- `Material*` 生命周期由 `Scene`（`scene.h`）通过 `unique_ptr` 持有，几何体只存裸指针
- 颜色统一 RGB ∈ [0,1]，输出前由 `image.h` 做伽马 2.2 校正
- 随机数统一走 `util.h` 的 `random_double()`，避免各写各的

## 当前实现状态

- ✅ 阶段 0：骨架 + PPM 渐变图
- ✅ 阶段 1：射线-球求交
- ✅ 阶段 2：法线 / 天空背景
- ✅ 阶段 3：多物体（球 + 地面）
- ✅ 阶段 4：Lambert 漫反射 + 柔和阴影
- ✅ 阶段 5：抗锯齿（多次采样平均）
- ✅ 阶段 6：材质系统（Lambert / Metal / Dielectric 全部完成）
- ✅ 阶段 7：场景文件化（JSON 场景 + 命令行参数）
- ✅ 阶段 8：三角形与三角网格求交（Möller–Trumbore + UV 插值 + SoA 网格）
- ✅ 阶段 9：OBJ 模型加载与变换（tinyobjloader + 4x4 矩阵 + 平滑法线）
- ✅ 阶段 10：BVH 线性化加速（扁平节点 + 栈遍历，GPU 友好）
- ⬜ 后续：PBR 材质、CUDA 移植

## 渲染原理速览

1. 相机对每个像素发射一条主射线
2. `ray_color` 求射线与场景最近交点
3. 命中 → 调材质 `scatter()` 生成新射线，颜色按 albedo 衰减后递归
4. 未命中 → 返回天空背景色
5. 递归到 `max_depth` 或射线不散射则停止
6. 每像素多次采样平均，消除锯齿

## 开发环境要求

- C++17 编译器（g++ / clang++ / MSVC 均可）
- CMake ≥ 3.10（可选）
- 零外部依赖，仅使用 C++ 标准库
