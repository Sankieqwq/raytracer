# 阶段 11：PBR 材质、纹理与自发光 — 设计文档

- **日期**：2026-06-28
- **阶段**：Stage 11（PBR Materials, Textures & Emission）
- **状态**：Draft（待用户 review）
- **作者**：hudaijin
- **关联总体规划**：CPU 光线追踪器 → PBR/BVH/CUDA 离线渲染器扩展（阶段 8-16）
- **前置阶段**：阶段 8（三角形/网格）、阶段 9（OBJ + Mat4）、阶段 10（线性 BVH）已完成

---

## 1. 背景与目标

当前材质系统仅有 Lambertian/Metal/Dielectric（Phong 风格），无物理合理着色、无纹理、无自发光。阶段 11 目标：**新增 PBR 金属-粗糙度材质（Cook-Torrance BRDF）、纹理系统（含 normal map）、自发光材质**，让渲染器支持物理合理着色与面光源。

### 1.1 阶段 11 验收标准（来自总体任务）

- [ ] PBR 金属-粗糙度材质，Cook-Torrance BRDF（GGX 法线分布、Smith 几何项、Fresnel-Schlick 近似），参数 albedo/metallic/roughness
- [ ] 纹理抽象：纯色纹理 + 图片纹理（stb_image 读取），UV 采样
- [ ] 材质可引用 albedo / normal / metallic / roughness 贴图
- [ ] 自发光材质，emission 颜色，作为面光源
- [ ] JSON 解析支持 `"type": "pbr"` 和 `"type": "emissive"`
- [ ] 着色函数统一接口：输出散射方向、衰减、自发光贡献

---

## 2. 现状分析

### 2.1 现有材质系统

```cpp
class Material {
public:
    virtual bool scatter(const Ray& r_in, const HitRecord& rec,
                         Color& attenuation, Ray& scattered) const = 0;
};
// 派生：Lambertian, Metal, Dielectric
```

- `ray_color` 命中后 `attenuation * ray_color(scattered, depth-1)`，无 emission 路径
- `HitRecord` 有 `u/v` 但无 tangent（normal map 需要）

### 2.2 现有图元

- `Triangle`/`TriangleMesh` 有 UV（阶段 8 加的），但无 tangent 计算
- `Sphere` 无 UV（`HitRecord.u/v` 默认 0），normal map 需球面切线实时计算

---

## 3. 关键设计决策（已确认）

### 决策 1：stb_image 放置 — **third_party + main.cpp 实现宏（选项 A）**

`third_party/stb_image.h`，`#define STB_IMAGE_IMPLEMENTATION` 在 `src/main.cpp` 顶部（与 tinyobjloader 一致）。

### 决策 2：纹理抽象 — **tag dispatch POD（选项 B）**

```cpp
enum class TextureType { Solid, Image };
struct Texture {
    TextureType type = TextureType::Solid;
    Color solid_color;           // Solid
    std::vector<float> pixels;   // Image: RGB float, row-major
    int width = 0, height = 0;   // Image
    int channels = 3;            // Image: always 3 (force RGB on load)
};
```

纹理采样函数：
```cpp
inline Color texture_sample(const Texture& tex, double u, double v);
```

**理由**：与阶段 12 SoA 重构方向一致，避免多态指针，GPU 友好。材质持有 `Texture` 值或索引。

### 决策 3：PBR Cook-Torrance — **标准实现（GGX + Smith + Schlick）**

```
f_r(l, v) = kD * albedo/π + kS * (D * F * G) / (4 * |n·l| * |n·v|)

D (GGX): α = roughness²; D = α² / (π * ((n·h)² * (α² - 1) + 1)²)
G (Smith-GGX): G1(v) = 2 * |n·v| / (|n·v| + sqrt(α² + (1-α²) * (n·v)²))
               G = G1(l) * G1(v)
F (Schlick): F0 = mix(0.04, albedo, metallic); F = F0 + (1-F0) * (1 - |h·v|)^5
kD = (1 - F) * (1 - metallic)
kS = F
```

**采样**：重要性采样 GGX 法线分布（粗糙度低时集中在镜面方向），提高收敛速度。

### 决策 4：自发光材质 — **独立 Emissive 类（选项 A）**

```cpp
class Emissive : public Material {
public:
    Color emission;
    // scatter 返回 false（不散射），emission 通过输出参数传递
};
```

### 决策 5：着色接口 — **scatter 加 emission 输出参数（选项 A）**

```cpp
virtual bool scatter(const Ray& r_in, const HitRecord& rec,
                     Color& attenuation, Ray& scattered,
                     Color& emission) const = 0;
```

现有 Lambertian/Metal/Dielectric 加默认 `emission = Color(0,0,0)`。

### 决策 6：贴图范围 — **全部含 normal map（选项 A）**

支持 albedo / normal / metallic / roughness 四种贴图。需要：
- `HitRecord` 增加 `tangent` 字段（Vec3）
- `Triangle`/`TriangleMesh` 计算切线（从 UV 推导）
- `Sphere` 实时计算球面切线
- normal map 采样后用 TBN 变换扰动法线

### 决策 7：JSON 格式 — **标量值 + `_map` 字段覆盖**

```json
{
    "type": "pbr",
    "albedo": [0.8, 0.2, 0.2],
    "albedo_map": "textures/metal.png",
    "metallic": 0.0,
    "metallic_map": "textures/metallic.png",
    "roughness": 0.5,
    "roughness_map": "textures/rough.png",
    "normal_map": "textures/normal.png"
}
```

```json
{
    "type": "emissive",
    "emission": [4, 4, 4]
}
```

标量值是默认，有 `_map` 字段则该通道用贴图采样覆盖。

### 决策 8：ray_color — **emission + attenuation * recursive**

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
        return emission;  // 不散射但发光（纯光源）
    }
    // 天空背景
    Vec3 unit_dir = r.direction.normalized();
    double t = 0.5 * (unit_dir.y + 1.0);
    return (1 - t) * Color(1.0, 1.0, 1.0) + t * Color(0.5, 0.7, 1.0);
}
```

### 决策 9：测试场景 — **pbr_test + texture_test 两个（选项 A）**

- `pbr_test.json`：三个 PBR 球（金属、粗糙金属、漫反射）+ 自发光面光源
- `texture_test.json`：带 albedo_map 的球，验证 UV + 纹理采样

---

## 4. 详细设计

### 4.1 `HitRecord` 扩展（加 tangent）

文件：`include/raytracer/geometry/hittable.h`

```cpp
struct HitRecord {
    Point3 p;
    Vec3 normal;
    Vec3 tangent;            // ← 新增（normal map 用）
    double t = 0;
    double u = 0;
    double v = 0;
    bool front_face = true;
    bool has_tangent = false;  // ← 新增（无 UV 时 false，normal map 跳过）
    Material* material = nullptr;
    void set_face_normal(const Ray& r, const Vec3& outward_normal) { ... }
};
```

### 4.2 切线计算

#### 4.2.1 球面切线（`sphere.h`）

```cpp
// 在 hit() 中，命中后计算 UV + tangent
double theta = std::acos(-oc.y);  // oc = (p - center)/radius
double phi = std::atan2(-oc.z, oc.x) + pi;
rec.u = phi / (2 * pi);
rec.v = theta / pi;
// 球面切线：dp/du 方向
rec.tangent = Vec3(-oc.z, 0, oc.x).normalized();  // 沿经线方向
rec.has_tangent = true;
```

#### 4.2.2 三角形/网格切线（`triangle.h` / `triangle_mesh.h`）

从 UV 推导切线（标准方法）：

```cpp
// 给定 v0,v1,v2 和 uv0,uv1,uv2
Vec3 edge1 = v1 - v0;
Vec3 edge2 = v2 - v0;
double du1 = uv1.x - uv0.x, dv1 = uv1.y - uv0.y;
double du2 = uv2.x - uv0.x, dv2 = uv2.y - uv0.y;
double det = du1 * dv2 - du2 * dv1;
if (std::fabs(det) < 1e-8) {
    // 退化，用面法线构造任意正交切线
    rec.tangent = ...; rec.has_tangent = true; return;
}
double inv = 1.0 / det;
Vec3 t = inv * (dv2 * edge1 - dv1 * edge2);
// 用重心坐标插值（三角形）或直接取（网格按面）
rec.tangent = t.normalized();
rec.has_tangent = true;
```

### 4.3 纹理类型（tag dispatch POD）

文件：`include/raytracer/render/texture.h`（新文件）

```cpp
#ifndef RT_TEXTURE_H
#define RT_TEXTURE_H

#include "raytracer/math/vec3.h"
#include <vector>
#include <string>

enum class TextureType { Solid, Image };

struct Texture {
    TextureType type = TextureType::Solid;
    Color solid_color;              // Solid
    std::vector<float> pixels;      // Image: RGB float, row-major, [0,1]
    int width = 0;
    int height = 0;

    // 采样：u/v ∈ [0,1]，返回 Color
    // 越界用 repeat（wrapping）
};

inline Color texture_sample(const Texture& tex, double u, double v) {
    if (tex.type == TextureType::Solid) return tex.solid_color;
    // Image: nearest or bilinear
    double u_wrapped = u - std::floor(u);  // repeat
    double v_wrapped = v - std::floor(v);
    int x = static_cast<int>(u_wrapped * tex.width);
    int y = static_cast<int>(v_wrapped * tex.height);
    x = (x < 0) ? 0 : (x >= tex.width ? tex.width - 1 : x);
    y = (y < 0) ? 0 : (y >= tex.height ? tex.height - 1 : y);
    int idx = (y * tex.width + x) * 3;
    return Color(tex.pixels[idx], tex.pixels[idx + 1], tex.pixels[idx + 2]);
}

// 从文件加载图片纹理（用 stb_image）
inline Texture load_image_texture(const std::string& path) {
    Texture tex;
    tex.type = TextureType::Image;
    int w, h, ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 3);  // 强制 3 通道
    if (!data) throw std::runtime_error("Failed to load image: " + path);
    tex.width = w; tex.height = h;
    tex.pixels.resize(w * h * 3);
    for (int i = 0; i < w * h * 3; i++) {
        tex.pixels[i] = data[i] / 255.0f;
    }
    stbi_image_free(data);
    return tex;
}

#endif
```

### 4.4 PBR 材质

文件：`include/raytracer/material/material.h`（修改，新增 PBR + Emissive）

#### 4.4.1 PBR 类

```cpp
class PBR : public Material {
public:
    Texture albedo;        // Solid 或 Image
    Texture metallic;      // Solid（标量存 x 分量）或 Image
    Texture roughness;     // Solid 或 Image
    Texture normal;        // Solid（无 normal map）或 Image
    bool has_normal_map = false;

    PBR(const Texture& albedo, double metallic, double roughness)
        : albedo(albedo),
          metallic(Texture{TextureType::Solid, Color(metallic, 0, 0), {}, 0, 0}),
          roughness(Texture{TextureType::Solid, Color(roughness, 0, 0), {}, 0, 0}) {}

    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered,
                 Color& emission) const override {
        emission = Color(0, 0, 0);

        // 采样参数
        Color base = texture_sample(albedo, rec.u, rec.v);
        double met = texture_sample(metallic, rec.u, rec.v).x;
        double rough = std::max(0.001, texture_sample(roughness, rec.u, rec.v).x);

        // Normal mapping: 采样切线空间法线，TBN 变换到世界
        Vec3 n = rec.normal;
        if (has_normal_map && rec.has_tangent) {
            Color ns = texture_sample(normal, rec.u, rec.v);
            Vec3 tn = Vec3(2*ns.x - 1, 2*ns.y - 1, 2*ns.z - 1).normalized();
            Vec3 T = rec.tangent.normalized();
            Vec3 B = cross(n, T).normalized();
            n = (T * tn.x + B * tn.y + n * tn.z).normalized();
            if (dot(n, rec.normal) < 0) n = -n;  // 朝向入射侧
        }

        Vec3 v = (-r_in.direction).normalized();  // 指向光源的反方向（即指向相机）
        double n_dot_v = std::max(0.0, dot(n, v));

        // 构建 TBN（n 为 z 轴），用于将 GGX 采样的局部 h 转世界
        Vec3 up = std::fabs(n.x) > 0.9 ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
        Vec3 Tbn_T = cross(up, n).normalized();
        Vec3 Tbn_B = cross(n, Tbn_T);

        // Importance sample GGX half-vector (local space: n = +z)
        double r1 = random_double();
        double r2 = random_double();
        double alpha = rough * rough;
        double phi = 2 * pi * r1;
        double cos_theta = std::sqrt((1 - r2) / (1 + (alpha * alpha - 1) * r2));
        double sin_theta = std::sqrt(1 - cos_theta * cos_theta);
        Vec3 h_local(sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta);
        Vec3 h = (Tbn_T * h_local.x + Tbn_B * h_local.y + n * h_local.z).normalized();

        // 散射方向 = v 关于 h 反射
        Vec3 scattered_dir = (2 * dot(v, h) * h - v).normalized();
        double n_dot_l = std::max(0.0, dot(n, scattered_dir));
        double n_dot_h = std::max(0.0, dot(n, h));
        double v_dot_h = std::max(0.0, dot(v, h));

        if (n_dot_l <= 0 || n_dot_v <= 0) return false;  // 背面

        // D (GGX)
        double a2 = alpha * alpha;
        double denom_d = n_dot_h * n_dot_h * (a2 - 1) + 1;
        double D = a2 / (pi * denom_d * denom_d);

        // G (Smith-GGX): G1(x) = 2*n·x / (n·x + sqrt(α² + (1-α²)(n·x)²))
        auto G1 = [&](double ndx) {
            double sq = std::sqrt(a2 + (1 - a2) * ndx * ndx);
            return 2 * ndx / (ndx + sq);
        };
        double G = G1(n_dot_l) * G1(n_dot_v);

        // F (Schlick): F0 = mix(0.04, albedo, metallic)
        Color F0 = (1 - met) * Color(0.04, 0.04, 0.04) + met * base;
        Color F = F0 + (Color(1, 1, 1) - F0) * std::pow(1 - v_dot_h, 5);

        // kD = (1 - F) * (1 - metallic)
        Color kD = (Color(1, 1, 1) - F) * (1 - met);

        // BRDF: kD * albedo/π + F * D*G / (4 * n·l * n·v)
        Color diffuse = kD * base / pi;
        Color specular = F * (D * G) / (4 * n_dot_l * n_dot_v);
        Color brdf = diffuse + specular;

        // PDF (importance sampled GGX): D * n·h / (4 * v·h)
        double pdf = (D * n_dot_h) / (4 * v_dot_h);
        if (pdf <= 0) return false;

        // attenuation = BRDF * cos(θ_l) / pdf
        attenuation = brdf * n_dot_l / pdf;
        scattered = Ray(rec.p, scattered_dir);
        return true;
    }
};
```

> **注**：实施时需仔细验证 BRDF + PDF 推导，确保能量守恒。先用纯金属（metallic=1, roughness=0）测镜面反射，纯漫反射（metallic=0, roughness=1）测接近 Lambertian。

#### 4.4.2 Emissive 类

```cpp
class Emissive : public Material {
public:
    Color emission;
    Emissive(const Color& emission) : emission(emission) {}

    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered,
                 Color& emission_out) const override {
        (void)r_in; (void)rec; (void)scattered; (void)attenuation;
        emission_out = emission;
        return false;  // 不散射
    }
};
```

#### 4.4.3 现有材质加 emission 参数

`Lambertian`/`Metal`/`Dielectric` 的 `scatter` 签名加 `Color& emission`，函数内 `emission = Color(0,0,0);`。

### 4.5 `ray_color` 修改

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

### 4.6 `scene.h` 扩展 `parse_material`

```cpp
inline Material* parse_material(const JsonValue& m, Scene& scene) {
    const std::string& type = m.at("type").strVal;
    std::unique_ptr<Material> mat;

    if (type == "lambertian") {
        mat = std::make_unique<Lambertian>(to_vec3(m.at("albedo")));
    } else if (type == "metal") {
        double fuzz = m.has("fuzz") ? m.at("fuzz").numVal : 0.0;
        mat = std::make_unique<Metal>(to_vec3(m.at("albedo")), fuzz);
    } else if (type == "dielectric") {
        mat = std::make_unique<Dielectric>(m.at("ior").numVal);
    } else if (type == "pbr") {
        Texture albedo_tex = parse_texture_color(m, "albedo", Color(0.8, 0.8, 0.8));
        Texture metallic_tex = parse_texture_scalar(m, "metallic", 0.0);
        Texture roughness_tex = parse_texture_scalar(m, "roughness", 0.5);
        auto pbr = std::make_unique<PBR>(albedo_tex, 0.0, 0.5);  // 临时，下面覆盖
        pbr->albedo = albedo_tex;
        pbr->metallic = metallic_tex;
        pbr->roughness = roughness_tex;
        if (m.has("normal_map")) {
            pbr->normal = load_image_texture(resolve_asset_path(scene_dir, m.at("normal_map").strVal));
            pbr->has_normal_map = true;
        }
        mat = std::move(pbr);
    } else if (type == "emissive") {
        mat = std::make_unique<Emissive>(to_vec3(m.at("emission")));
    } else {
        throw std::runtime_error("Unknown material type: " + type);
    }

    Material* ptr = mat.get();
    scene.materials.push_back(std::move(mat));
    return ptr;
}
```

辅助函数 `parse_texture_color`/`parse_texture_scalar`：标量值 → Solid 纹理；有 `_map` → Image 纹理。

### 4.7 `main.cpp` 加 stb_image 实现宏

```cpp
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
```

---

## 5. 验收场景

### 5.1 `scenes/pbr_test.json`

```json
{
    "image": { "width": 600, "height": 400, "samples": 32, "max_depth": 32, "output": "pbr_test.ppm" },
    "camera": { "lookfrom": [0, 0, 5], "lookat": [0, 0, 0], "vup": [0, 1, 0], "vfov": 40, "aperture": 0.0, "focus_dist": 5.0 },
    "objects": [
        { "type": "sphere", "center": [-1.5, 0, 0], "radius": 1,
          "material": { "type": "pbr", "albedo": [0.8, 0.2, 0.2], "metallic": 1.0, "roughness": 0.05 } },
        { "type": "sphere", "center": [0, 0, 0], "radius": 1,
          "material": { "type": "pbr", "albedo": [0.7, 0.7, 0.7], "metallic": 0.5, "roughness": 0.4 } },
        { "type": "sphere", "center": [1.5, 0, 0], "radius": 1,
          "material": { "type": "pbr", "albedo": [0.2, 0.8, 0.2], "metallic": 0.0, "roughness": 0.8 } },
        { "type": "sphere", "center": [0, 3, 0], "radius": 1,
          "material": { "type": "emissive", "emission": [8, 8, 8] } },
        { "type": "sphere", "center": [0, -100.5, 0], "radius": 100,
          "material": { "type": "lambertian", "albedo": [0.3, 0.3, 0.3] } }
    ]
}
```

**验证**：左球（金属）镜面反射环境；中球（半金属）柔和高光；右球（漫反射）哑光；顶部自发光球照亮场景。

### 5.2 `scenes/texture_test.json`

需要一个测试贴图（程序生成简单棋盘格 PNG）。用 Python 生成：

```bash
python3 -c "
import struct
# 生成 64x64 棋盘格 PPM，转 PNG
..."
```

或直接用 PPM 贴图（stb_image 支持 PPM？实际不支持，需 PNG/JPG）。用 sips 从 PPM 转 PNG。

场景引用棋盘格 PNG 作为 albedo_map，验证 UV 映射。

---

## 6. 不在本阶段范围

- 多重重要性采样（MIS）优化（本阶段用纯 BRDF 重要性采样）
- IBL（基于图像的光照）
- 透明材质的 PBR 扩展（transmission）
- 区域光重要性采样（自发光作为普通面光源参与路径追踪）

---

## 7. 风险与注意事项

1. **PBR BRDF 正确性**：Cook-Torrance 实现需仔细验证能量守恒。`attenuation = BRDF * cos / pdf` 必须正确，否则颜色过亮/过暗。**建议**：先用纯金属（metallic=1, roughness=0）测试，应接近镜面反射；纯漫反射（metallic=0, roughness=1）应接近 Lambertian。
2. **Normal map TBN 一致性**：切线空间必须与 UV 一致，否则法线扰动方向错误。三角形切线从 UV 推导时，退化情况（UV 共线）需 fallback。
3. **现有材质签名变更**：`scatter` 加 `emission` 参数，所有现有材质和测试代码都要更新。`ray_color` 也改。**向后兼容**：现有六场景渲染结果应不变（emission=0，散射逻辑不变）。
4. **纹理坐标 wrapping**：球面 UV 在极点可能奇异，需 clamp。
5. **stb_image 通道**：强制 `stbi_load(..., 3)` 为 3 通道 RGB，避免 alpha 通道处理。
6. **性能**：PBR 比 Lambertian 慢（每 bounce 多次三角函数 + pow），`max_depth=32` 的 PBR 场景可能慢 5-10 倍。`pbr_test.json` 用 `samples=32` 平衡。
7. **自发光强度**：emission 值需 >1（如 [8,8,8]）才能有效照明，因为路径追踪衰减快。

---

## 8. 实施顺序

1. 复制 stb_image 到 `third_party/`
2. 新建 `include/raytracer/render/texture.h`
3. 修改 `include/raytracer/geometry/hittable.h`（HitRecord 加 tangent/has_tangent）
4. 修改 `include/raytracer/geometry/sphere.h`（hit 中计算 UV + tangent）
5. 修改 `include/raytracer/geometry/triangle.h`（hit 中计算 tangent）
6. 修改 `include/raytracer/geometry/triangle_mesh.h`（hit 中计算 tangent）
7. 修改 `include/raytracer/material/material.h`（scatter 加 emission；新增 PBR + Emissive；现有材质加 emission=0）
8. 修改 `src/main.cpp`（stb_image 实现宏；ray_color 加 emission）
9. 修改 `include/raytracer/scene/scene.h`（parse_material 支持 pbr/emissive + 纹理加载）
10. 生成测试贴图 PNG
11. 新建 `scenes/pbr_test.json` + `scenes/texture_test.json`
12. 编译 + 六场景向后兼容验证 + 两新场景验收
13. 更新 README
14. Git commit

---

## 9. 验证清单

- [ ] `cmake -S . -B build && cmake --build build` 编译通过
- [ ] 六场景（default/three_balls/triangle_test/mark/bunny_test/no_normal_obj）渲染与阶段 10 一致
- [ ] `pbr_test.json` 渲染：金属/粗糙金属/漫反射三球物理合理，自发光照亮
- [ ] `texture_test.json` 渲染：棋盘格贴图正确映射球面
- [ ] README「当前实现状态」更新阶段 11 完成项
- [ ] Git commit：`Stage 11: PBR materials, textures, emission`

---

## 10. 后续阶段衔接

| 后续阶段 | 本阶段如何铺垫 |
|---------|---------------|
| 阶段 12（GPU SoA 重构） | `Texture` 已是 POD，材质去多态时 PBR/Emissive 转为 tag + POD 字段；纹理用纹理索引 |
| 阶段 15（CUDA 纹理） | stb_image 加载的纹理上传为 CUDA texture object，设备端采样 |
| 阶段 16（glTF） | glTF 的 PBR 材质（baseColor/metallic/roughness/normal）直接映射到本阶段 PBR 类 |
