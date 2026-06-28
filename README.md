# Raytracer

一个从零开始的 C++ 光线追踪渲染器，零外部依赖，输出 PPM 图片文件。
场景通过 JSON 文件配置，支持命令行参数覆盖。
适合学习渲染底层原理：射线-几何求交、漫反射、递归反射/折射、抗锯齿、相机投影。

## 快速开始

### 编译

方式一：项目脚本（推荐）

```bash
./build.sh
```

脚本默认使用 `g++`，也可以通过环境变量覆盖编译器和参数：

```bash
CXX=clang++ CXXFLAGS="-std=c++17 -O2 -Wall -Wextra" ./build.sh
```

方式二：CMake

```bash
cmake -S . -B build
cmake --build build
```

方式三：直接 g++

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude -o raytracer src/main.cpp
```

### 运行

```bash
# 用默认场景渲染
./run.sh

# 指定场景文件
./run.sh --scene scenes/three_balls.json

# 覆盖输出路径和采样数
./run.sh --scene scenes/three_balls.json --out my.ppm --samples 64

# 渲染 OBJ 示例场景
./run.sh --scene scenes/mark.json

# 直接指定 OBJ/GLB 模型，未指定 --scene 时默认使用 scenes/obj.json 作为通用模板
./run.sh --model models/obj/mark.obj --out mark.ppm
./run.sh --model models/glb/toyota_mark_ii_jzx100.glb --out car.ppm
```

命令行参数：

| 参数 | 含义 | 默认值 |
|------|------|--------|
| `--scene <path>` | 场景 JSON 文件 | `scenes/default.json` |
| `--model <path>` | OBJ/GLB 模型文件，覆盖场景中 `mesh` 的 `obj/path` | 未指定时使用场景配置 |
| `--obj <path>` | `--model` 的兼容别名 | 未指定时使用场景配置 |
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

场景用 JSON 描述，包含 `image`、`camera`、`lighting`、`lights`、`objects` 几部分。见 `scenes/` 目录示例。

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
    "lighting": {
        "ambient": [0.05, 0.05, 0.05]
    },
    "lights": [
        {
            "type": "directional",
            "direction": [-1, -1.5, -1],
            "color": [1.0, 0.96, 0.9],
            "intensity": 0.9
        },
        {
            "type": "point",
            "position": [3, 4, 5],
            "color": [0.8, 0.9, 1.0],
            "intensity": 5.0
        }
    ],
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

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `width` | int | 800 | 图像宽度 |
| `height` | int | 400 | 图像高度 |
| `samples` | int | 16 | 每像素采样数，越大越细腻、越慢 |
| `max_depth` | int | 32 | 光线递归深度上限 |
| `output` | string | `"out.ppm"` | 输出文件路径 |

**camera**（均可选，有默认值）

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `lookfrom` | [x,y,z] | [0,0,0] | 相机位置 |
| `lookat` | [x,y,z] | [0,0,-1] | 相机看向的点 |
| `vup` | [x,y,z] | [0,1,0] | 相机上方向 |
| `vfov` | float | 60 | 垂直视野角（度） |
| `aperture` | float | 0.0 | 光圈孔径，0 = 无景深 |
| `focus_dist` | float | 1.0 | 对焦距离 |
| `auto` | bool | false | 为 true 时根据模型包围盒自动放置相机，可搭配 `vfov` 使用 |

**lighting / lights**（均可选；不写 `lights` 时使用一组默认方向光和点光源）

| 字段 | 类型 | 说明 |
|------|------|------|
| `lighting.ambient` | [r,g,b] | 环境光颜色，影响阴影内的最低亮度 |
| `lights[].type` | string | 支持 `"directional"` 和 `"point"` |
| `lights[].direction` | [x,y,z] | 方向光方向，表示光线照射方向 |
| `lights[].position` | [x,y,z] | 点光源位置 |
| `lights[].color` | [r,g,b] | 光源颜色 |
| `lights[].intensity` | float | 光源强度；点光源会随距离平方衰减 |

渲染器会对点光源和方向光发射阴影射线，被遮挡的光源不会给当前命中点贡献直接光照。

**objects**（数组，每个元素是一个物体）

| 字段 | 类型 | 说明 |
|------|------|------|
| `type` | string | 目前支持 `"sphere"`、`"mesh"` |
| `center` | [x,y,z] | 球心坐标 |
| `radius` | float | 半径 |
| `obj` / `path` | string | `mesh` 使用的 OBJ/GLB 文件路径 |
| `auto_fit` | bool | `mesh` 是否自动居中和缩放，未写 `scale/translate` 时默认开启 |
| `fit_size` | float | 自动缩放后的最长轴尺寸，默认 3.0 |
| `fit_center` | [x,y,z] | 自动居中后的目标中心，默认 [0,0,0] |
| `scale` | float | 手动缩放，或在 `auto_fit` 下作为缩放倍率 |
| `translate` | [x,y,z] | 手动平移，或在 `auto_fit` 下作为额外偏移 |
| `material` | object | 模型没有内嵌材质时使用的 fallback 材质，见下 |
| `override_material` | bool | 为 true 时强制用 `material` 覆盖 GLB 内嵌材质 |

**material.type** 支持三种：

| 类型 | 额外字段 | 说明 |
|------|----------|------|
| `"lambertian"` | `albedo`: [r,g,b], `texture`: string/object | 漫反射（哑光） |
| `"metal"` | `albedo`: [r,g,b], `texture`: string/object, `fuzz`: 0~1 | 金属反射，fuzz=0 镜面，越大越粗糙 |
| `"dielectric"` | `ior`: float | 玻璃/水等透明介质，ior=1.5 玻璃，1.33 水 |

颜色 `albedo` 各分量范围 [0, 1]。`texture` 可以直接写图片路径，也可以写成 `{ "path": "..." }`。当前支持 PPM/PNM，PNG/JPG 会在 macOS 上通过 `sips` 转码后读取。

### 示例场景

| 文件 | 内容 |
|------|------|
| `scenes/default.json` | 漫反射球 + 地面（基础验证） |
| `scenes/three_balls.json` | 漫反射 + 玻璃 + 金属 三球场景 |
| `scenes/obj.json` | 通用 OBJ/GLB 模型模板，配合 `--model` 使用 |
| `scenes/mark.json` | 加载 `models/obj/mark.obj` 的网格场景 |
| `scenes/textured_quad.json` | 带 UV 和棋盘纹理的最小贴图验证场景 |

## OBJ / GLB 网格支持

当前版本已支持在场景 JSON 中通过 `mesh` 对象加载 Wavefront OBJ 或 GLB 模型，并输出渲染图片。
当前范围刻意保持最小：

- 支持 `v`、`vn`、`f`
- 支持 OBJ `vt`，命中三角形时会插值 UV 并采样材质纹理
- 支持三角形、四边形和一般多边形面，内部会自动扇形拆分为三角形
- 支持 `v` / `v//vn` / `v/vt/vn` 等常见面索引格式
- 支持 GLB 里的 `POSITION`、`NORMAL`、`TEXCOORD_0`、`indices` 和节点矩阵/TRS
- 支持 GLB 基础材质：`baseColorFactor`、`metallicFactor`、`roughnessFactor`、透明度 alpha
- 支持 GLB `baseColorTexture`，可读取内嵌 bufferView 图片或外部图片路径
- GLB 材质会按 primitive 自动绑定到三角形；`material` 字段只作为 fallback，除非设置 `override_material: true`
- 支持根据 OBJ 包围盒自动居中、缩放模型
- 支持没有显式 `camera` 时根据模型包围盒自动放置相机
- 支持 `scale` 和 `translate` 两个基础变换；在 `auto_fit` 下它们会作为自动适配后的额外调整
- 暂不解析 `mtl` 和旋转变换；PNG/JPG 贴图解码依赖 macOS `sips`

示例：

```json
{
    "type": "mesh",
    "obj": "../models/obj/mark.obj",
    "auto_fit": true,
    "fit_size": 3.0,
    "fit_center": [0, 0, 0],
    "override_material": false,
    "material": {
        "type": "lambertian",
        "texture": "../textures/checker.pnm",
        "albedo": [0.75, 0.2, 0.2]
    }
}
```

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

- `HitRecord` 字段：`p / normal / t / front_face / Material*`
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
- ⬜ 后续：BVH 加速、三角形网格、多线程

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
