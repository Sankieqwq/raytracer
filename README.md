# Raytracer

一个从零开始的 C++ 光线追踪渲染器，输出 PPM 图片文件。
场景通过 JSON 文件配置，支持命令行参数覆盖。

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

# 高采样渲染时可指定线程数；不指定时默认使用 CPU 硬件线程数
./run.sh --model models/glb/toyota_mark_ii_jzx100.glb --samples 64 --threads 8 --out car_64.ppm

# 渲染 OBJ 示例场景
./run.sh --scene scenes/mark.json

# 直接指定 OBJ/GLB 模型，未指定 --scene 时默认使用 scenes/obj.json 作为通用模板
./run.sh --model models/obj/mark.obj --out mark.ppm
./run.sh --model models/glb/toyota_mark_ii_jzx100.glb --out car.ppm

# 快速清晰预览：关闭随机递归反弹，只保留直接光照和阴影
./run.sh --preview --model models/glb/toyota_mark_ii_jzx100.glb --out car_preview.ppm

# 也可以保留指定采样数，只关闭递归随机反弹
./run.sh --direct-only --model models/glb/toyota_mark_ii_jzx100.glb --samples 8 --out car_direct.ppm
```

命令行参数：

| 参数 | 含义 | 默认值 |
|------|------|--------|
| `--scene <path>` | 场景 JSON 文件 | `scenes/default.json` |
| `--model <path>` | OBJ/GLB 模型文件，覆盖场景中 `mesh` 的 `obj/path` | 未指定时使用场景配置 |
| `--obj <path>` | `--model` 的兼容别名 | 未指定时使用场景配置 |
| `--out <path>` | 输出 PPM 文件（覆盖场景配置） | 场景中的 `output` |
| `--samples <n>` | 每像素采样数（覆盖场景配置） | 场景中的 `samples` |
| `--threads <n>` | 渲染线程数，用于高采样加速 | CPU 硬件线程数 |
| `--direct-only` | 只计算直接光照、阴影和环境光，关闭递归随机反弹以减少噪声 | 关闭 |
| `--preview` | 快速预览模式：启用 `--direct-only`，且未指定 `--samples` 时使用 1 sample | 关闭 |
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

场景用 JSON 描述，包含 `image`、`camera`、`lighting`、`lights`、`ground`、`objects` 几部分。见 `scenes/` 目录示例。

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
    "ground": {
        "enabled": true,
        "material": {
            "type": "lambertian",
            "albedo": [0.62, 0.60, 0.55]
        }
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

**ground**（可选；用于自动生成接收阴影的地面）

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `enabled` | bool | true | 是否生成地面；只有写了 `ground` 块才会启用 |
| `size` | float | 根据主体包围盒自动计算 | 地面边长 |
| `y` | float | 主体包围盒最低点略下方 | 地面高度 |
| `material` | object | 灰米色漫反射材质 | 地面材质，格式同普通 `material` |

自动地面会加入 BVH 参与求交，因此模型可以把点光源/方向光阴影投到地面上，形成接触阴影。自动相机仍按模型主体包围盒取景，不会被大地面拉远。

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

**material.type** 支持五种：

| 类型 | 额外字段 | 说明 |
|------|----------|------|
| `"lambertian"` | `albedo`: [r,g,b], `texture`: string/object | 漫反射（哑光） |
| `"metal"` | `albedo`: [r,g,b], `texture`: string/object, `fuzz`: 0~1 | 金属反射，fuzz=0 镜面，越大越粗糙 |
| `"dielectric"` | `ior`: float, `albedo`: [r,g,b]（可选） | 玻璃/水等透明介质，ior=1.5 玻璃，1.33 水；albedo 默认白色，设为颜色可渲染有色玻璃 |
| `"pbr"` | `albedo`/`metallic`/`roughness` + 各 `_map` + `normal_map` | Cook-Torrance 金属-粗糙度材质 |
| `"emissive"` | `emission`: [r,g,b] | 自发光材质，可作为面光源 |

颜色 `albedo` 各分量范围 [0, 1]。`texture` 可以直接写图片路径，也可以写成 `{ "path": "..." }`。当前支持 PPM/PNM，PNG/JPG 通过 `stb_image` 解码。

### 示例场景

| 文件 | 内容 |
|------|------|
| `scenes/default.json` | 漫反射球 + 地面（基础验证） |
| `scenes/three_balls.json` | 漫反射 + 玻璃 + 金属 三球场景 |
| `scenes/glass_bottle.json` | GLB 玻璃瓶（自动识别 BLEND 为透明介质） |
| `scenes/glass.json` | 玻璃材质验证 |
| `scenes/obj.json` | 通用 OBJ/GLB 模型模板，配合 `--model` 使用 |
| `scenes/mark.json` | 加载 `models/obj/mark.obj` 的网格场景 |
| `scenes/triangle_test.json` | 三角形 + 三角网格（验证 UV 插值） |
| `scenes/bunny_test.json` | OBJ 网格 + 变换验证 |
| `scenes/no_normal_obj.json` | 无法线 OBJ + 平滑法线 |
| `scenes/pbr_test.json` | PBR 金属-粗糙度材质验证 |
| `scenes/texture_test.json` | 贴图采样验证 |
| `scenes/textured_quad.json` | 带 UV 和棋盘纹理的最小贴图验证场景 |

## OBJ / GLB 网格支持

当前版本已支持在场景 JSON 中通过 `mesh` 对象加载 Wavefront OBJ 或 GLB 模型，并输出渲染图片。

- 支持 OBJ `v`、`vn`、`vt`、`f`
- 支持 OBJ `vt`，命中三角形时会插值 UV 并采样材质纹理
- 支持三角形、四边形和一般多边形面，内部会自动扇形拆分为三角形
- 支持 `v` / `v//vn` / `v/vt/vn` 等常见面索引格式
- OBJ 缺法线时会按面积加权生成平滑法线；normal/UV 混合缺失时会补齐安全默认值
- 支持 GLB 里的 `POSITION`、`NORMAL`、`TEXCOORD_0`、`indices` 和节点矩阵/TRS
- 支持 GLB 基础材质：`baseColorFactor`、`metallicFactor`、`roughnessFactor`、透明度 alpha
- 支持 GLB `baseColorTexture`，可读取内嵌 bufferView 图片或外部图片路径
- 支持 GLB `alphaMode: "BLEND"` 自动识别为透明介质（Dielectric），常用于导出工具的玻璃近似
- 支持 GLB KHR 扩展：`KHR_materials_transmission`（透射）、`KHR_materials_ior`（折射率）
- GLB 材质路由优先级：KHR transmission > alphaMode BLEND > alpha < 0.35 > metallic > lambertian
- GLB 材质会按 primitive 自动绑定到三角形；`material` 字段只作为 fallback，除非设置 `override_material: true`
- 支持根据 OBJ/GLB 包围盒自动居中、缩放模型
- 支持没有显式 `camera` 时根据模型包围盒自动放置相机
- 支持 `transform` 块中的平移、欧拉旋转、轴角旋转、统一/非统一缩放
- 支持顶层 `scale` 和 `translate`；在 `auto_fit` 下它们会作为自动适配后的额外调整
- 网格会合并为 `TriangleMesh`，内部构建三角形级 BVH 加速求交
- 暂不解析 OBJ `mtl`；PNG/JPG 贴图通过 `stb_image` 解码

### 变换

`transform` 块包含可选的 `translate`/`rotate`/`rotate_axis`/`scale`，按 SRT 顺序（先缩放，再旋转，最后平移）烘焙到顶点：

```json
{
    "type": "mesh",
    "file": "../models/obj/mark.obj",
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

## BVH 加速结构

场景图元超过 1 个时自动构建线性 BVH 加速射线求交。

- **线性节点**：`LinearBVHNode` 为 POD 结构（AABB + 左右子索引 + 叶子图元起止），可直接 `memcpy` 到 GPU
- **构建**：中点划分（按最长轴排序取中点），叶子粒度 1 图元
- **遍历**：栈数组迭代（深度 64），与未来 GPU 栈遍历逻辑一致
- **结果一致性**：因 `hit()` 取最近命中，遍历顺序变化不影响渲染结果

后续阶段（12/14）将复用此线性结构与栈遍历逻辑实现 GPU BVH。

## PBR 材质与纹理（阶段 11）

支持物理合理的金属-粗糙度着色模型。

### 材质类型

| 类型 | 字段 | 说明 |
|------|------|------|
| `lambertian` | `albedo` | 漫反射（保留） |
| `metal` | `albedo`, `fuzz` | 金属反射（保留） |
| `dielectric` | `ior`, `albedo`（可选） | 玻璃/水（保留） |
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

## 项目结构

```
raytracer/
├── CMakeLists.txt                构建配置
├── build.sh                      构建脚本（g++ 直接编译）
├── run.sh                        运行脚本（编译后直接执行）
├── include/raytracer/            头文件库（header-only，按模块分目录）
│   ├── math/                     模块 A：数学库
│   │   ├── util.h                常量 (pi/infinity)、随机数、角度换算
│   │   ├── vec3.h                三维向量（兼作 Point3 / Color）、reflect/refract
│   │   ├── vec2.h                二维向量（纹理坐标）
│   │   ├── mat4.h                4x4 变换矩阵（平移/旋转/缩放/法线变换）
│   │   └── ray.h                 射线：origin + direction，at(t)
│   ├── geometry/                 模块 B：几何求交
│   │   ├── hittable.h            可命中物体抽象基类 + HitRecord（含 tangent/UV）
│   │   ├── sphere.h              球体：射线-球求交
│   │   ├── hittable_list.h       物体列表：遍历找最近交点
│   │   ├── triangle.h            单三角形（Möller–Trumbore + UV 插值）
│   │   ├── triangle_mesh.h       三角网格（SoA 布局 + 内部 BVH）
│   │   ├── aabb.h                轴对齐包围盒
│   │   └── bvh.h                 线性 BVH 加速结构（扁平节点 + 栈遍历）
│   ├── material/                 模块 C：材质
│   │   ├── material.h            Lambertian / Metal / Dielectric / PBR / Emissive
│   │   └── texture.h             纹理基类 + SolidColor / Image / Tinted / Checker
│   ├── render/                   模块 D：渲染
│   │   ├── camera.h              相机：FOV、lookat、景深
│   │   ├── image.h               PPM 写入 + 伽马校正
│   │   └── texture.h             渲染端纹理工具
│   └── scene/                    模块 D：场景
│       ├── json.h                最小 JSON 解析器（零依赖）
│       ├── obj.h                 OBJ 加载器（手写 + tinyobjloader）
│       ├── glb.h                 GLB 加载器（glTF 2.0 + KHR 扩展）
│       └── scene.h               场景加载器：JSON → Camera + HittableList + Lights
├── src/main.cpp                  命令行解析 + 渲染主循环 + ray_color 递归
├── third_party/                  第三方 header-only 库
│   ├── stb_image.h               PNG/JPG 解码
│   └── tiny_obj_loader.h         OBJ 解析
├── scenes/                       场景文件目录
├── models/                       测试模型（OBJ/GLB）
├── textures/                     测试纹理
├── scripts/                      辅助脚本（ppm_to_png.sh）
├── tests/                        回归测试
└── docs/                         设计文档（specs + plans）
```

头文件 include 路径统一为 `#include "raytracer/<模块>/<文件>.h"`，例如 `#include "raytracer/math/vec3.h"`。


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
- ✅ 阶段 11：PBR 材质 + 纹理 + 自发光（Cook-Torrance + GGX 重要性采样）
- ✅ GLB 玻璃材质支持：`alphaMode: BLEND` 自动识别 + `KHR_materials_transmission` / `KHR_materials_ior` 扩展 + Dielectric albedo/纹理
- ⬜ 后续：CUDA 移植

## 渲染原理速览

1. 相机对每个像素发射一条主射线
2. `ray_color` 求射线与场景最近交点
3. 命中 → 计算直接光照（`direct_lighting`：遍历光源 + 阴影射线 + Lambert 余弦）
4. 调材质 `scatter()` 生成间接射线，颜色按 attenuation 衰减后递归
5. 透明材质（Dielectric）跳过直接光照，间接光权重为 1.0；不透明材质间接光权重 0.35
6. 未命中 → 返回天空背景色（蓝白渐变）
7. 递归到 `max_depth` 或射线不散射则停止
8. 每像素多次采样平均，消除锯齿

## 开发环境要求

- C++17 编译器（g++ / clang++ / MSVC 均可）
- CMake ≥ 3.10（可选）
- 第三方 header-only 库（已包含在 `third_party/`）：`stb_image.h`（图片解码）、`tiny_obj_loader.h`（OBJ 解析）
