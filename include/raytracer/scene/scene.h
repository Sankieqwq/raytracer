// Scene loader: JSON -> Camera + HittableList + image params
#ifndef RT_SCENE_H
#define RT_SCENE_H

#include "raytracer/scene/json.h"
#include "raytracer/scene/obj.h"
#include "raytracer/math/vec3.h"
#include "raytracer/render/camera.h"
#include "raytracer/geometry/hittable.h"
#include "raytracer/geometry/sphere.h"
#include "raytracer/geometry/hittable_list.h"
#include "raytracer/geometry/triangle.h"
#include "raytracer/geometry/bvh.h"
#include "raytracer/material/material.h"
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>
#include <string>

struct Scene {
    int width = 800;
    int height = 400;
    int samples = 16;
    int max_depth = 32;
    std::string output = "out.ppm";

    std::unique_ptr<Camera> camera;
    HittableList primitives;
    std::unique_ptr<Hittable> world;
    size_t primitive_count = 0;
    bool has_mesh_bounds = false;
    AABB mesh_bounds;

    std::vector<std::unique_ptr<Material>> materials;
    std::vector<std::unique_ptr<Hittable>> objects;
};

struct SceneLoadOptions {
    std::string obj_override;
};

inline Vec3 to_vec3(const JsonValue& arr) {
    return Vec3(arr.arrVal[0].numVal,
                arr.arrVal[1].numVal,
                arr.arrVal[2].numVal);
}

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
    } else {
        throw std::runtime_error("Unknown material type: " + type);
    }

    Material* ptr = mat.get();
    scene.materials.push_back(std::move(mat));
    return ptr;
}

inline std::string resolve_asset_path(const std::filesystem::path& base_dir,
                                      const std::string& asset_path) {
    std::filesystem::path path(asset_path);
    if (path.is_relative()) path = base_dir / path;
    return path.lexically_normal().string();
}

inline Material* ensure_material(const JsonValue& obj, Scene& scene) {
    if (obj.has("material")) return parse_material(obj.at("material"), scene);
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
    return parse_material(fallback, scene);
}

inline bool get_bool(const JsonValue& obj, const std::string& key, bool fallback) {
    return obj.has(key) ? obj.at(key).boolVal : fallback;
}

inline void add_mesh_bounds(Scene& scene, const AABB& bounds) {
    scene.mesh_bounds = scene.has_mesh_bounds
        ? AABB::surrounding_box(scene.mesh_bounds, bounds)
        : bounds;
    scene.has_mesh_bounds = true;
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

inline void load_scene(const std::string& path,
                       Scene& scene,
                       const SceneLoadOptions& options = SceneLoadOptions()) {
    scene.primitives.clear();
    scene.world.reset();
    scene.objects.clear();
    scene.materials.clear();
    scene.primitive_count = 0;
    scene.has_mesh_bounds = false;

    JsonValue root = parse_json_file(path);
    std::filesystem::path scene_dir = std::filesystem::absolute(path).parent_path();

    if (root.has("image")) {
        const JsonValue& img = root.at("image");
        if (img.has("width"))     scene.width     = (int)img.at("width").numVal;
        if (img.has("height"))    scene.height    = (int)img.at("height").numVal;
        if (img.has("samples"))   scene.samples   = (int)img.at("samples").numVal;
        if (img.has("max_depth")) scene.max_depth = (int)img.at("max_depth").numVal;
        if (img.has("output"))    scene.output    = img.at("output").strVal;
    }

    double aspect = double(scene.width) / scene.height;

    if (root.has("objects")) {
        for (const JsonValue& obj : root.at("objects").arrVal) {
            const std::string& type = obj.at("type").strVal;
            if (type == "sphere") {
                Point3 center = to_vec3(obj.at("center"));
                double radius = obj.at("radius").numVal;
                Material* mat = ensure_material(obj, scene);
                auto sph = std::make_unique<Sphere>(center, radius, mat);
                scene.primitives.add(sph.get());
                scene.objects.push_back(std::move(sph));
            } else if (type == "mesh") {
                std::string obj_source;
                if (!options.obj_override.empty()) {
                    obj_source = options.obj_override;
                } else if (obj.has("obj")) {
                    obj_source = obj.at("obj").strVal;
                } else if (obj.has("path")) {
                    obj_source = obj.at("path").strVal;
                } else {
                    throw std::runtime_error("mesh requires obj/path or --obj override");
                }

                std::string obj_path = resolve_asset_path(scene_dir, obj_source);
                Material* mat = ensure_material(obj, scene);
                ObjMeshData raw_mesh = load_obj_mesh(obj_path);

                bool auto_fit = get_bool(obj, "auto_fit", !(obj.has("scale") || obj.has("translate")));
                double scale = obj.has("scale") ? obj.at("scale").numVal : 1.0;
                Vec3 translate = obj.has("translate") ? to_vec3(obj.at("translate")) : Vec3(0, 0, 0);

                if (auto_fit) {
                    double fit_size = obj.has("fit_size") ? obj.at("fit_size").numVal : 3.0;
                    Point3 fit_center = obj.has("fit_center") ? to_vec3(obj.at("fit_center")) : Point3(0, 0, 0);
                    double raw_size = raw_mesh.bounds.max_extent();
                    if (raw_size <= 1e-8) {
                        throw std::runtime_error("OBJ bounds are too small: " + obj_path);
                    }
                    scale *= fit_size / raw_size;
                    translate = fit_center - scale * raw_mesh.bounds.center() + translate;
                }

                ObjMeshData mesh = load_obj_mesh(obj_path, scale, translate);
                add_mesh_bounds(scene, mesh.bounds);

                for (const ObjTriangleData& tri : mesh.triangles) {
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
            } else {
                throw std::runtime_error("Unknown object type: " + type);
            }
        }
    }

    scene.camera = build_camera(root, scene, aspect);

    scene.primitive_count = scene.primitives.objects.size();
    if (scene.primitives.objects.empty()) {
        scene.world = std::make_unique<HittableList>(scene.primitives);
    } else {
        scene.world = std::make_unique<BVHNode>(scene.primitives.objects);
    }
}

#endif
