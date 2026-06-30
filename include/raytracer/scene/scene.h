// Scene loader: JSON -> Camera + HittableList + image params
#ifndef RT_SCENE_H
#define RT_SCENE_H

#include "raytracer/geometry/bvh.h"
#include "raytracer/geometry/hittable.h"
#include "raytracer/geometry/hittable_list.h"
#include "raytracer/geometry/sphere.h"
#include "raytracer/geometry/triangle.h"
#include "raytracer/geometry/triangle_mesh.h"
#include "raytracer/material/material.h"
#include "raytracer/math/mat4.h"
#include "raytracer/math/vec3.h"
#include "raytracer/render/camera.h"
#include "raytracer/render/image.h"
#include "raytracer/scene/glb.h"
#include "raytracer/scene/json.h"
#include "raytracer/scene/obj.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

enum class LightType {
    Point,
    Directional
};

struct Light {
    LightType type = LightType::Directional;
    Point3 position;
    Vec3 direction = Vec3(0, -1, -1);
    Color color = Color(1, 1, 1);
    double intensity = 1.0;
};

struct EmissiveObject {
    Hittable* geometry;
    Color emission;
};

struct Scene {
    int width = 800;
    int height = 400;
    int samples = 16;
    int max_depth = 32;
    std::string output = "out.ppm";
    ImageOutputOptions output_options;
    unsigned int seed = 0;
    bool has_seed = false;

    std::unique_ptr<Camera> camera;
    HittableList primitives;
    std::unique_ptr<Hittable> world;
    size_t primitive_count = 0;
    bool has_mesh_bounds = false;
    AABB mesh_bounds;
    Color ambient_light = Color(0.04, 0.04, 0.04);
    std::vector<Light> lights;
    std::vector<EmissiveObject> emissive_objects;

    std::vector<std::unique_ptr<Material>> materials;
    std::vector<std::unique_ptr<Hittable>> objects;
    std::vector<std::unique_ptr<Hittable>> emissive_samplers;
};

struct SceneLoadOptions {
    std::string model_override;
};

inline Vec3 to_vec3(const JsonValue& arr) {
    return Vec3(arr.arrVal[0].numVal,
                arr.arrVal[1].numVal,
                arr.arrVal[2].numVal);
}

inline Vec2 to_vec2(const JsonValue& arr) {
    return Vec2(arr.arrVal[0].numVal, arr.arrVal[1].numVal);
}

inline std::string resolve_asset_path(const std::filesystem::path& base_dir,
                                      const std::string& asset_path) {
    std::filesystem::path path(asset_path);
    if (path.is_relative()) path = base_dir / path;
    return path.lexically_normal().string();
}

inline Mat4 parse_transform(const JsonValue& obj) {
    if (obj.has("transform")) {
        const JsonValue& t = obj.at("transform");
        Mat4 scale = Mat4::identity();
        Mat4 rotate_axis = Mat4::identity();
        Mat4 rotate_euler = Mat4::identity();
        Mat4 translate = Mat4::identity();

        if (t.has("scale")) {
            const JsonValue& s = t.at("scale");
            if (s.isArray() && s.arrVal.size() == 3)
                scale = Mat4::scale(s.arrVal[0].numVal, s.arrVal[1].numVal, s.arrVal[2].numVal);
            else
                scale = Mat4::scale(s.numVal, s.numVal, s.numVal);
        }
        if (t.has("rotate_axis")) {
            const JsonValue& ra = t.at("rotate_axis");
            Vec3 axis = to_vec3(ra.at("axis"));
            double angle = ra.at("angle").numVal;
            rotate_axis = Mat4::rotate_axis(axis, angle);
        }
        if (t.has("rotate")) {
            Vec3 r = to_vec3(t.at("rotate"));
            rotate_euler = Mat4::rotate_z(r.z) * Mat4::rotate_y(r.y) * Mat4::rotate_x(r.x);
        }
        if (t.has("translate")) {
            Vec3 tr = to_vec3(t.at("translate"));
            translate = Mat4::translate(tr.x, tr.y, tr.z);
        }
        return translate * rotate_axis * rotate_euler * scale;
    }

    double s = obj.has("scale") ? obj.at("scale").numVal : 1.0;
    Vec3 tr = obj.has("translate") ? to_vec3(obj.at("translate")) : Vec3(0, 0, 0);
    return Mat4::translate(tr.x, tr.y, tr.z) * Mat4::scale(s, s, s);
}

inline std::shared_ptr<Texture> make_solid_texture(const Color& color) {
    return std::make_shared<SolidColorTexture>(color);
}

inline std::shared_ptr<Texture> load_texture_from_path(const std::filesystem::path& base_dir,
                                                       const std::string& texture_path) {
    return std::make_shared<ImageTexture>(resolve_asset_path(base_dir, texture_path));
}

inline std::shared_ptr<Texture> material_texture_or_color(const JsonValue& m,
                                                          const std::filesystem::path& base_dir,
                                                          const Color& fallback) {
    if (m.has("texture")) {
        const JsonValue& texture = m.at("texture");
        if (texture.type == JsonValue::String) {
            return std::make_shared<TintedTexture>(
                load_texture_from_path(base_dir, texture.strVal), fallback);
        }
        if (texture.has("path")) {
            return std::make_shared<TintedTexture>(
                load_texture_from_path(base_dir, texture.at("path").strVal), fallback);
        }
    }
    if (m.has("albedo_texture")) {
        return std::make_shared<TintedTexture>(
            load_texture_from_path(base_dir, m.at("albedo_texture").strVal), fallback);
    }
    return make_solid_texture(fallback);
}

inline std::shared_ptr<Texture> parse_texture_color(const JsonValue& m,
                                                    const std::string& base_key,
                                                    const Color& default_color,
                                                    const std::filesystem::path& base_dir) {
    std::string map_key = base_key + "_map";
    if (m.has(map_key)) return load_texture_from_path(base_dir, m.at(map_key).strVal);
    if (m.has(base_key)) return make_solid_texture(to_vec3(m.at(base_key)));
    return make_solid_texture(default_color);
}

inline std::shared_ptr<Texture> parse_texture_scalar(const JsonValue& m,
                                                     const std::string& base_key,
                                                     double default_val,
                                                     const std::filesystem::path& base_dir) {
    std::string map_key = base_key + "_map";
    if (m.has(map_key)) return load_texture_from_path(base_dir, m.at(map_key).strVal);
    double v = m.has(base_key) ? m.at(base_key).numVal : default_val;
    return make_solid_texture(Color(v, 0, 0));
}

inline Material* parse_material(const JsonValue& m,
                                Scene& scene,
                                const std::filesystem::path& base_dir = std::filesystem::current_path()) {
    const std::string& type = m.at("type").strVal;
    std::unique_ptr<Material> mat;

    if (type == "lambertian") {
        Color albedo = m.has("albedo") ? to_vec3(m.at("albedo")) : Color(1.0, 1.0, 1.0);
        mat = std::make_unique<Lambertian>(material_texture_or_color(m, base_dir, albedo));
    } else if (type == "metal") {
        double fuzz = m.has("fuzz") ? m.at("fuzz").numVal : 0.0;
        Color albedo = m.has("albedo") ? to_vec3(m.at("albedo")) : Color(0.8, 0.8, 0.8);
        mat = std::make_unique<Metal>(material_texture_or_color(m, base_dir, albedo), fuzz);
    } else if (type == "dielectric") {
        Color alb = m.has("albedo") ? to_vec3(m.at("albedo")) : Color(1.0, 1.0, 1.0);
        mat = std::make_unique<Dielectric>(m.at("ior").numVal, alb);
    } else if (type == "pbr") {
        auto albedo_tex = parse_texture_color(m, "albedo", Color(0.8, 0.8, 0.8), base_dir);
        auto metallic_tex = parse_texture_scalar(m, "metallic", 0.0, base_dir);
        auto roughness_tex = parse_texture_scalar(m, "roughness", 0.5, base_dir);
        double met_def = m.has("metallic") ? m.at("metallic").numVal : 0.0;
        double rough_def = m.has("roughness") ? m.at("roughness").numVal : 0.5;
        auto pbr = std::make_unique<PBR>(albedo_tex, met_def, rough_def);
        pbr->metallic = metallic_tex;
        pbr->roughness = roughness_tex;
        if (m.has("normal_map")) {
            pbr->normal = load_texture_from_path(base_dir, m.at("normal_map").strVal);
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

inline Material* ensure_material(const JsonValue& obj,
                                 Scene& scene,
                                 const std::filesystem::path& base_dir = std::filesystem::current_path()) {
    if (obj.has("material")) return parse_material(obj.at("material"), scene, base_dir);
    JsonValue fallback;
    fallback.type = JsonValue::Object;
    JsonValue type;
    type.type = JsonValue::String;
    type.strVal = "lambertian";
    JsonValue albedo;
    albedo.type = JsonValue::Array;
    for (double c : {0.7, 0.7, 0.7}) {
        JsonValue v;
        v.type = JsonValue::Number;
        v.numVal = c;
        albedo.arrVal.push_back(v);
    }
    fallback.objVal["type"] = type;
    fallback.objVal["albedo"] = albedo;
    return parse_material(fallback, scene, base_dir);
}

inline std::shared_ptr<Texture> make_loaded_texture(const ObjMeshData& mesh,
                                                    const LoadedMaterialData& data) {
    if (data.base_color_texture >= 0 &&
        static_cast<size_t>(data.base_color_texture) < mesh.textures.size()) {
        const LoadedTextureData& texture = mesh.textures[data.base_color_texture];
        if (!texture.encoded.empty()) {
            return std::make_shared<TintedTexture>(
                std::make_shared<ImageTexture>(texture.encoded, texture.mime_type), data.albedo);
        }
        if (!texture.path.empty()) {
            return std::make_shared<TintedTexture>(
                std::make_shared<ImageTexture>(texture.path), data.albedo);
        }
    }
    return make_solid_texture(data.albedo);
}

inline Material* add_loaded_material(const ObjMeshData& mesh,
                                      const LoadedMaterialData& data,
                                      Scene& scene) {
    std::unique_ptr<Material> mat;
    if (data.transmission > 0.5 || data.alpha_blend) {
        mat = std::make_unique<Dielectric>(data.ior, make_loaded_texture(mesh, data));
    } else if (data.metallic > 0.5) {
        double fuzz = std::clamp(data.roughness, 0.0, 1.0);
        mat = std::make_unique<Metal>(make_loaded_texture(mesh, data), fuzz);
    } else {
        mat = std::make_unique<Lambertian>(make_loaded_texture(mesh, data));
    }

    Material* ptr = mat.get();
    scene.materials.push_back(std::move(mat));
    return ptr;
}

inline Light parse_light(const JsonValue& light_json) {
    Light light;
    std::string type = light_json.has("type") ? light_json.at("type").strVal : "directional";
    if (type == "point") {
        light.type = LightType::Point;
        light.position = light_json.has("position") ? to_vec3(light_json.at("position")) : Point3(0, 3, 4);
    } else if (type == "directional") {
        light.type = LightType::Directional;
        light.direction = light_json.has("direction") ? to_vec3(light_json.at("direction")) : Vec3(-1, -1, -1);
    } else {
        throw std::runtime_error("Unknown light type: " + type);
    }

    if (light_json.has("color")) light.color = to_vec3(light_json.at("color"));
    if (light_json.has("intensity")) light.intensity = light_json.at("intensity").numVal;
    return light;
}

inline std::vector<Light> default_lights() {
    Light sun;
    sun.type = LightType::Directional;
    sun.direction = Vec3(-1, -1.5, -1).normalized();
    sun.color = Color(1.0, 0.96, 0.9);
    sun.intensity = 0.9;

    Light fill;
    fill.type = LightType::Point;
    fill.position = Point3(3.0, 4.0, 5.0);
    fill.color = Color(0.8, 0.9, 1.0);
    fill.intensity = 5.0;

    return {sun, fill};
}

struct LightSample {
    Vec3 direction;
    Color radiance;
    double pdf = 0;
    double distance = infinity;
    bool is_delta = true;
};

inline LightSample sample_any_light(const Scene& scene, const Point3& p) {
    size_t n_delta = scene.lights.size();
    size_t n_area = scene.emissive_objects.size();
    size_t total = n_delta + n_area;
    if (total == 0) return LightSample{};

    double choice = random_double() * total;
    LightSample s;

    if (choice < n_delta) {
        const Light& light = scene.lights[static_cast<size_t>(choice)];
        s.is_delta = true;
        s.pdf = 1.0 / total;
        if (light.type == LightType::Point) {
            Vec3 to_light = light.position - p;
            double dist2 = to_light.length_squared();
            if (dist2 < 1e-8) return LightSample{};
            double dist = std::sqrt(dist2);
            s.direction = to_light / dist;
            s.distance = dist;
            s.radiance = light.color * light.intensity / dist2;
        } else {
            s.direction = (-light.direction).normalized();
            s.distance = infinity;
            s.radiance = light.color * light.intensity;
        }
    } else {
        size_t idx = static_cast<size_t>(choice - n_delta);
        if (idx >= n_area) return LightSample{};
        const EmissiveObject& eo = scene.emissive_objects[idx];
        double r1 = random_double(), r2 = random_double();
        Vec3 light_normal;
        Point3 light_point = eo.geometry->sample_point(r1, r2, &light_normal);

        Vec3 to_light = light_point - p;
        double dist2 = to_light.length_squared();
        if (dist2 < 1e-8) return LightSample{};
        double dist = std::sqrt(dist2);
        s.direction = to_light / dist;
        s.distance = dist;

        double cos_light = dot(light_normal, -s.direction);
        if (cos_light <= 1e-8) return LightSample{};

        double geom_area = eo.geometry->area();
        if (geom_area <= 0) return LightSample{};

        double total_area = 0;
        for (const EmissiveObject& e : scene.emissive_objects) total_area += e.geometry->area();

        s.radiance = eo.emission;
        s.is_delta = false;
        s.pdf = (dist2 / cos_light) / total_area * (double(n_area) / total);
    }
    return s;
}

inline std::string lower_ext(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

inline ObjMeshData load_model_mesh(const std::string& path) {
    std::string ext = lower_ext(path);
    if (ext == ".obj") return load_obj_mesh(path);
    if (ext == ".glb") return load_glb_mesh(path);
    throw std::runtime_error("Unsupported model format: " + ext + " (" + path + ")");
}

inline ObjMeshData transform_mesh(const ObjMeshData& input, const Mat4& transform) {
    ObjMeshData output = input;
    bool first = true;
    AABB box;

    for (ObjTriangleData& tri : output.triangles) {
        tri.v0 = transform.transform_point(tri.v0);
        tri.v1 = transform.transform_point(tri.v1);
        tri.v2 = transform.transform_point(tri.v2);

        if (tri.has_normals) {
            tri.n0 = transform.transform_normal(tri.n0).normalized();
            tri.n1 = transform.transform_normal(tri.n1).normalized();
            tri.n2 = transform.transform_normal(tri.n2).normalized();
        }

        AABB tri_box;
        Triangle(tri.v0, tri.v1, tri.v2).bounding_box(tri_box);
        box = first ? tri_box : AABB::surrounding_box(box, tri_box);
        first = false;
    }

    output.bounds = box;
    return output;
}

inline bool get_bool(const JsonValue& obj, const std::string& key, bool fallback) {
    return obj.has(key) ? obj.at(key).boolVal : fallback;
}

inline Mat4 mesh_transform_from_object(const ObjMeshData& raw_mesh, const JsonValue& obj) {
    bool has_manual_transform = obj.has("transform") || obj.has("scale") || obj.has("translate");
    bool auto_fit = get_bool(obj, "auto_fit", !has_manual_transform);
    if (!auto_fit) return parse_transform(obj);

    double scale = obj.has("scale") ? obj.at("scale").numVal : 1.0;
    Vec3 translate = obj.has("translate") ? to_vec3(obj.at("translate")) : Vec3(0, 0, 0);
    double fit_size = obj.has("fit_size") ? obj.at("fit_size").numVal : 3.0;
    Point3 fit_center = obj.has("fit_center") ? to_vec3(obj.at("fit_center")) : Point3(0, 0, 0);
    double raw_size = raw_mesh.bounds.max_extent();
    if (raw_size <= 1e-8) {
        throw std::runtime_error("Model bounds are too small");
    }

    scale *= fit_size / raw_size;
    translate = fit_center - scale * raw_mesh.bounds.center() + translate;
    return Mat4::translate(translate.x, translate.y, translate.z) *
           Mat4::scale(scale, scale, scale);
}

inline void add_mesh_bounds(Scene& scene, const AABB& bounds) {
    scene.mesh_bounds = scene.has_mesh_bounds
        ? AABB::surrounding_box(scene.mesh_bounds, bounds)
        : bounds;
    scene.has_mesh_bounds = true;
}

inline Material* make_lambertian_material(Scene& scene, const Color& color) {
    auto mat = std::make_unique<Lambertian>(color);
    Material* ptr = mat.get();
    scene.materials.push_back(std::move(mat));
    return ptr;
}

inline std::unique_ptr<TriangleMesh> make_triangle_mesh_from_loaded_mesh(
    const ObjMeshData& mesh,
    Material* fallback_mat,
    const std::vector<Material*>& embedded_materials) {
    auto tri_mesh = std::make_unique<TriangleMesh>();
    bool any_uv = false;
    for (const ObjTriangleData& tri : mesh.triangles) {
        any_uv = any_uv || tri.has_uvs;
    }

    tri_mesh->vertices.reserve(mesh.triangles.size() * 3);
    tri_mesh->normals.reserve(mesh.triangles.size() * 3);
    if (any_uv) tri_mesh->uvs.reserve(mesh.triangles.size() * 3);
    tri_mesh->indices.reserve(mesh.triangles.size() * 3);
    tri_mesh->material_per_tri.reserve(mesh.triangles.size());

    for (const ObjTriangleData& tri : mesh.triangles) {
        int base = static_cast<int>(tri_mesh->vertices.size());
        tri_mesh->vertices.push_back(tri.v0);
        tri_mesh->vertices.push_back(tri.v1);
        tri_mesh->vertices.push_back(tri.v2);
        tri_mesh->indices.push_back(base);
        tri_mesh->indices.push_back(base + 1);
        tri_mesh->indices.push_back(base + 2);

        Vec3 face_n = cross(tri.v1 - tri.v0, tri.v2 - tri.v0);
        if (face_n.length_squared() < 1e-12) face_n = Vec3(0, 1, 0);
        face_n = face_n.normalized();
        tri_mesh->normals.push_back(tri.has_normals ? tri.n0 : face_n);
        tri_mesh->normals.push_back(tri.has_normals ? tri.n1 : face_n);
        tri_mesh->normals.push_back(tri.has_normals ? tri.n2 : face_n);

        if (any_uv) {
            tri_mesh->uvs.push_back(tri.has_uvs ? tri.uv0 : Vec2(0, 0));
            tri_mesh->uvs.push_back(tri.has_uvs ? tri.uv1 : Vec2(0, 0));
            tri_mesh->uvs.push_back(tri.has_uvs ? tri.uv2 : Vec2(0, 0));
        }

        Material* tri_mat = fallback_mat;
        if (tri.material_index >= 0 &&
            static_cast<size_t>(tri.material_index) < embedded_materials.size()) {
            tri_mat = embedded_materials[static_cast<size_t>(tri.material_index)];
        }
        tri_mesh->material_per_tri.push_back(tri_mat);
    }

    tri_mesh->acceleration_node_count();
    return tri_mesh;
}

inline void add_auto_ground(const JsonValue& root,
                            Scene& scene,
                            const std::filesystem::path& scene_dir) {
    if (!root.has("ground")) return;

    const JsonValue& ground = root.at("ground");
    if (ground.has("enabled") && !ground.at("enabled").boolVal) return;

    AABB bounds;
    if (scene.has_mesh_bounds) {
        bounds = scene.mesh_bounds;
    } else if (!scene.primitives.bounding_box(bounds)) {
        return;
    } else {
        scene.mesh_bounds = bounds;
        scene.has_mesh_bounds = true;
    }

    double max_extent = bounds.max_extent();
    if (max_extent <= 1e-8) max_extent = 1.0;

    Point3 center = bounds.center();
    double half_size = ground.has("size")
        ? 0.5 * ground.at("size").numVal
        : std::max(3.0, max_extent * 1.8);
    double y = ground.has("y")
        ? ground.at("y").numVal
        : bounds.minimum.y - std::max(1e-4, max_extent * 0.0005);

    Color default_color(0.62, 0.60, 0.55);
    Material* mat = ground.has("material")
        ? parse_material(ground.at("material"), scene, scene_dir)
        : make_lambertian_material(scene, default_color);

    Point3 p0(center.x - half_size, y, center.z - half_size);
    Point3 p1(center.x + half_size, y, center.z - half_size);
    Point3 p2(center.x + half_size, y, center.z + half_size);
    Point3 p3(center.x - half_size, y, center.z + half_size);

    auto tri_a = std::make_unique<Triangle>(p0, p2, p1, mat);
    scene.primitives.add(tri_a.get());
    scene.objects.push_back(std::move(tri_a));

    auto tri_b = std::make_unique<Triangle>(p0, p3, p2, mat);
    scene.primitives.add(tri_b.get());
    scene.objects.push_back(std::move(tri_b));
}

inline Camera make_auto_camera(const AABB& bounds, double aspect, const JsonValue* camera_json) {
    double vfov = 35.0;
    double aperture = 0.0;
    Vec3 vup(0, 1, 0);

    if (camera_json) {
        if (camera_json->has("vfov")) vfov = camera_json->at("vfov").numVal;
        if (camera_json->has("aperture")) aperture = camera_json->at("aperture").numVal;
        if (camera_json->has("vup")) vup = to_vec3(camera_json->at("vup"));
    }

    Point3 center = bounds.center();
    Vec3 extent = bounds.extent();
    double fit_height = std::max(extent.y, extent.x / aspect);
    if (fit_height <= 1e-8) fit_height = 1.0;

    double theta = degrees_to_radians(vfov);
    double distance = (0.5 * fit_height) / std::tan(theta / 2);
    distance += 0.5 * extent.z;
    distance *= 1.35;
    if (distance < 1.0) distance = 1.0;

    Point3 lookat = center;
    Point3 lookfrom = center + Vec3(0, 0.12 * bounds.max_extent(), distance);

    if (camera_json) {
        if (camera_json->has("lookat")) lookat = to_vec3(camera_json->at("lookat"));
        if (camera_json->has("lookfrom")) lookfrom = to_vec3(camera_json->at("lookfrom"));
    }
    double focus_dist = camera_json && camera_json->has("focus_dist")
        ? camera_json->at("focus_dist").numVal
        : (lookfrom - lookat).length();

    return Camera(lookfrom, lookat, vup, vfov, aspect, aperture, focus_dist);
}

inline std::unique_ptr<Camera> build_camera(const JsonValue& root,
                                            const Scene& scene,
                                            double aspect) {
    const JsonValue* camera_json = root.has("camera") ? &root.at("camera") : nullptr;
    bool auto_camera = !camera_json || get_bool(*camera_json, "auto", false);

    if (auto_camera) {
        AABB bounds;
        if (scene.has_mesh_bounds) {
            bounds = scene.mesh_bounds;
        } else if (!scene.primitives.bounding_box(bounds)) {
            bounds = AABB(Point3(-1, -1, -1), Point3(1, 1, 1));
        }
        return std::make_unique<Camera>(make_auto_camera(bounds, aspect, camera_json));
    }

    const JsonValue& c = *camera_json;
    Point3 lookfrom = to_vec3(c.at("lookfrom"));
    Point3 lookat   = to_vec3(c.at("lookat"));
    Vec3   vup      = c.has("vup") ? to_vec3(c.at("vup")) : Vec3(0, 1, 0);
    double vfov     = c.has("vfov") ? c.at("vfov").numVal : 60.0;
    double aperture = c.has("aperture") ? c.at("aperture").numVal : 0.0;
    double focus    = c.has("focus_dist") ? c.at("focus_dist").numVal : (lookfrom - lookat).length();
    return std::make_unique<Camera>(lookfrom, lookat, vup, vfov, aspect, aperture, focus);
}

class TriangleMeshEmissiveSubset : public Hittable {
public:
    TriangleMeshEmissiveSubset(const TriangleMesh* mesh, std::vector<int> tri_indices)
        : mesh_(mesh), tri_indices_(std::move(tri_indices)) {
        build_area_cdf();
    }

    bool hit(const Ray& r, double t_min, double t_max,
             HitRecord& rec) const override {
        bool hit_any = false;
        double closest = t_max;
        for (int tri_idx : tri_indices_) {
            HitRecord tmp;
            if (mesh_->hit_triangle(tri_idx, r, t_min, closest, tmp)) {
                rec = tmp;
                closest = tmp.t;
                hit_any = true;
            }
        }
        return hit_any;
    }

    bool bounding_box(AABB& output_box) const override {
        bool first = true;
        for (int tri_idx : tri_indices_) {
            Point3 p0, p1, p2;
            if (!triangle_points(tri_idx, p0, p1, p2)) continue;
            AABB box;
            Triangle(p0, p1, p2).bounding_box(box);
            output_box = first ? box : AABB::surrounding_box(output_box, box);
            first = false;
        }
        return !first;
    }

    Point3 sample_point(double r1, double r2, Vec3* normal_out = nullptr) const override {
        if (area_cdf_.empty() || area_tri_indices_.empty() || total_area_ <= 0) return Point3();

        double target = r1 * total_area_;
        auto it = std::lower_bound(area_cdf_.begin(), area_cdf_.end(), target);
        int local_idx = static_cast<int>(std::distance(area_cdf_.begin(), it));
        if (local_idx >= static_cast<int>(area_tri_indices_.size())) {
            local_idx = static_cast<int>(area_tri_indices_.size()) - 1;
        }

        double previous = local_idx > 0 ? area_cdf_[local_idx - 1] : 0.0;
        double span = area_cdf_[local_idx] - previous;
        double local_r1 = span > 0 ? (target - previous) / span : 0.0;

        Point3 p0, p1, p2;
        if (!triangle_points(area_tri_indices_[local_idx], p0, p1, p2)) return Point3();

        double sq = std::sqrt(std::clamp(local_r1, 0.0, 1.0));
        double b1 = 1.0 - sq;
        double b2 = sq * (1.0 - r2);
        double b0 = sq * r2;

        if (normal_out) {
            Vec3 n = cross(p1 - p0, p2 - p0);
            double len = n.length();
            *normal_out = len > 1e-12 ? n / len : Vec3(0, 1, 0);
        }
        return b0 * p0 + b1 * p1 + b2 * p2;
    }

    double area() const override {
        return total_area_;
    }

private:
    const TriangleMesh* mesh_;
    std::vector<int> tri_indices_;
    std::vector<int> area_tri_indices_;
    std::vector<double> area_cdf_;
    double total_area_ = 0;

    bool triangle_points(int tri_idx, Point3& p0, Point3& p1, Point3& p2) const {
        if (!mesh_ || tri_idx < 0) return false;
        size_t base = static_cast<size_t>(tri_idx) * 3;
        if (base + 2 >= mesh_->indices.size()) return false;
        int i0 = mesh_->indices[base + 0];
        int i1 = mesh_->indices[base + 1];
        int i2 = mesh_->indices[base + 2];
        if (i0 < 0 || i1 < 0 || i2 < 0) return false;
        if (static_cast<size_t>(i0) >= mesh_->vertices.size() ||
            static_cast<size_t>(i1) >= mesh_->vertices.size() ||
            static_cast<size_t>(i2) >= mesh_->vertices.size()) {
            return false;
        }
        p0 = mesh_->vertices[static_cast<size_t>(i0)];
        p1 = mesh_->vertices[static_cast<size_t>(i1)];
        p2 = mesh_->vertices[static_cast<size_t>(i2)];
        return true;
    }

    double triangle_area(int tri_idx) const {
        Point3 p0, p1, p2;
        if (!triangle_points(tri_idx, p0, p1, p2)) return 0;
        return 0.5 * cross(p1 - p0, p2 - p0).length();
    }

    void build_area_cdf() {
        area_tri_indices_.clear();
        area_cdf_.clear();
        total_area_ = 0;
        for (int tri_idx : tri_indices_) {
            double a = triangle_area(tri_idx);
            if (a <= 1e-20) continue;
            total_area_ += a;
            area_tri_indices_.push_back(tri_idx);
            area_cdf_.push_back(total_area_);
        }
    }
};

inline void collect_emissive_objects(Scene& scene) {
    scene.emissive_objects.clear();
    scene.emissive_samplers.clear();
    for (Hittable* obj : scene.primitives.objects) {
        Sphere* sph = dynamic_cast<Sphere*>(obj);
        if (sph && sph->material && sph->material->is_emissive()) {
            Emissive* em = static_cast<Emissive*>(sph->material);
            scene.emissive_objects.push_back({sph, em->emission});
            continue;
        }
        Triangle* tri = dynamic_cast<Triangle*>(obj);
        if (tri && tri->material && tri->material->is_emissive()) {
            Emissive* em = static_cast<Emissive*>(tri->material);
            scene.emissive_objects.push_back({tri, em->emission});
            continue;
        }
        TriangleMesh* mesh = dynamic_cast<TriangleMesh*>(obj);
        if (mesh) {
            std::vector<Material*> emissive_materials;
            std::vector<std::vector<int>> tris_by_material;
            for (size_t tri_idx = 0; tri_idx < mesh->material_per_tri.size(); tri_idx++) {
                Material* mat = mesh->material_per_tri[tri_idx];
                if (!mat || !mat->is_emissive()) continue;

                size_t group = 0;
                for (; group < emissive_materials.size(); group++) {
                    if (emissive_materials[group] == mat) break;
                }
                if (group == emissive_materials.size()) {
                    emissive_materials.push_back(mat);
                    tris_by_material.push_back(std::vector<int>());
                }
                tris_by_material[group].push_back(static_cast<int>(tri_idx));
            }

            for (size_t group = 0; group < emissive_materials.size(); group++) {
                Emissive* em = dynamic_cast<Emissive*>(emissive_materials[group]);
                if (!em) continue;
                auto sampler = std::make_unique<TriangleMeshEmissiveSubset>(
                    mesh, tris_by_material[group]);
                if (sampler->area() <= 0) continue;
                Hittable* sampler_ptr = sampler.get();
                scene.emissive_samplers.push_back(std::move(sampler));
                scene.emissive_objects.push_back({sampler_ptr, em->emission});
            }
        }
    }
}

inline void load_scene(const std::string& path,
                       Scene& scene,
                       const SceneLoadOptions& options = SceneLoadOptions()) {
    scene.primitives.clear();
    scene.world.reset();
    scene.objects.clear();
    scene.materials.clear();
    scene.primitive_count = 0;
    scene.has_mesh_bounds = false;
    scene.ambient_light = Color(0.04, 0.04, 0.04);
    scene.lights.clear();
    scene.emissive_objects.clear();
    scene.emissive_samplers.clear();
    scene.output_options = ImageOutputOptions();
    scene.seed = 0;
    scene.has_seed = false;

    JsonValue root = parse_json_file(path);
    std::filesystem::path scene_dir = std::filesystem::absolute(path).parent_path();

    if (root.has("image")) {
        const JsonValue& img = root.at("image");
        if (img.has("width"))     scene.width     = static_cast<int>(img.at("width").numVal);
        if (img.has("height"))    scene.height    = static_cast<int>(img.at("height").numVal);
        if (img.has("samples"))   scene.samples   = static_cast<int>(img.at("samples").numVal);
        if (img.has("max_depth")) scene.max_depth = static_cast<int>(img.at("max_depth").numVal);
        if (img.has("output"))    scene.output    = img.at("output").strVal;
        if (img.has("exposure"))  scene.output_options.exposure = img.at("exposure").numVal;
        if (img.has("tone_map"))  scene.output_options.tone_map = parse_tone_map_mode(img.at("tone_map").strVal);
        if (img.has("seed")) {
            scene.seed = static_cast<unsigned int>(img.at("seed").numVal);
            scene.has_seed = true;
        }
    }

    double aspect = double(scene.width) / scene.height;

    if (root.has("lighting")) {
        const JsonValue& lighting = root.at("lighting");
        if (lighting.has("ambient")) scene.ambient_light = to_vec3(lighting.at("ambient"));
    }

    if (root.has("lights")) {
        for (const JsonValue& light_json : root.at("lights").arrVal) {
            scene.lights.push_back(parse_light(light_json));
        }
    } else {
        scene.lights = default_lights();
    }

    if (root.has("objects")) {
        for (const JsonValue& obj : root.at("objects").arrVal) {
            const std::string& type = obj.at("type").strVal;
            if (type == "sphere") {
                Point3 center = to_vec3(obj.at("center"));
                double radius = obj.at("radius").numVal;
                Material* mat = ensure_material(obj, scene, scene_dir);
                auto sph = std::make_unique<Sphere>(center, radius, mat);
                scene.primitives.add(sph.get());
                scene.objects.push_back(std::move(sph));
            } else if (type == "mesh") {
                std::string obj_source;
                if (!options.model_override.empty()) {
                    obj_source = options.model_override;
                } else if (obj.has("file")) {
                    obj_source = obj.at("file").strVal;
                } else if (obj.has("obj")) {
                    obj_source = obj.at("obj").strVal;
                } else if (obj.has("path")) {
                    obj_source = obj.at("path").strVal;
                } else {
                    throw std::runtime_error("mesh requires file/obj/path or --model override");
                }

                std::string obj_path = resolve_asset_path(scene_dir, obj_source);
                ObjMeshData raw_mesh = load_model_mesh(obj_path);
                ObjMeshData mesh = transform_mesh(raw_mesh, mesh_transform_from_object(raw_mesh, obj));
                add_mesh_bounds(scene, mesh.bounds);

                Material* fallback_mat = ensure_material(obj, scene, scene_dir);
                std::vector<Material*> embedded_materials;
                bool override_material = get_bool(obj, "override_material", false);
                if (!override_material) {
                    for (const LoadedMaterialData& material : mesh.materials) {
                        embedded_materials.push_back(add_loaded_material(mesh, material, scene));
                    }
                }

                auto mesh_ptr = make_triangle_mesh_from_loaded_mesh(mesh, fallback_mat, embedded_materials);
                scene.primitives.add(mesh_ptr.get());
                scene.objects.push_back(std::move(mesh_ptr));
            } else if (type == "triangle") {
                const JsonValue& verts = obj.at("vertices");
                Point3 a = to_vec3(verts.arrVal[0]);
                Point3 b = to_vec3(verts.arrVal[1]);
                Point3 c = to_vec3(verts.arrVal[2]);
                Material* mat = ensure_material(obj, scene, scene_dir);
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
                for (const JsonValue& v : verts.arrVal) {
                    mesh->vertices.push_back(to_vec3(v));
                }
                const JsonValue& idxs = obj.at("indices");
                for (const JsonValue& i : idxs.arrVal) {
                    mesh->indices.push_back(static_cast<int>(i.numVal));
                }
                if (obj.has("normals")) {
                    for (const JsonValue& n : obj.at("normals").arrVal) {
                        mesh->normals.push_back(to_vec3(n));
                    }
                }
                if (obj.has("uvs")) {
                    for (const JsonValue& uv : obj.at("uvs").arrVal) {
                        mesh->uvs.push_back(to_vec2(uv));
                    }
                }
                Material* mat = ensure_material(obj, scene, scene_dir);
                size_t tri_count = mesh->indices.size() / 3;
                mesh->material_per_tri.assign(tri_count, mat);
                mesh->acceleration_node_count();
                scene.primitives.add(mesh.get());
                scene.objects.push_back(std::move(mesh));
            } else {
                throw std::runtime_error("Unknown object type: " + type);
            }
        }
    }

    add_auto_ground(root, scene, scene_dir);

    scene.camera = build_camera(root, scene, aspect);

    scene.primitive_count = scene.primitives.objects.size();
    if (scene.primitives.objects.empty()) {
        scene.world = std::make_unique<HittableList>(scene.primitives);
    } else {
        scene.world = std::make_unique<LinearBVH>(scene.primitives.objects);
    }

    collect_emissive_objects(scene);
}

#endif
