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

    std::vector<std::unique_ptr<Material>> materials;
    std::vector<std::unique_ptr<Hittable>> objects;
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

inline void load_scene(const std::string& path, Scene& scene) {
    scene.primitives.clear();
    scene.world.reset();
    scene.objects.clear();
    scene.materials.clear();
    scene.primitive_count = 0;

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

    if (root.has("camera")) {
        const JsonValue& c = root.at("camera");
        Point3 lookfrom = to_vec3(c.at("lookfrom"));
        Point3 lookat   = to_vec3(c.at("lookat"));
        Vec3   vup      = c.has("vup") ? to_vec3(c.at("vup")) : Vec3(0, 1, 0);
        double vfov     = c.has("vfov") ? c.at("vfov").numVal : 60.0;
        double aperture = c.has("aperture") ? c.at("aperture").numVal : 0.0;
        double focus    = c.has("focus_dist") ? c.at("focus_dist").numVal : 1.0;
        scene.camera = std::make_unique<Camera>(
            lookfrom, lookat, vup, vfov, aspect, aperture, focus);
    } else {
        scene.camera = std::make_unique<Camera>(
            Point3(0, 0, 0), Point3(0, 0, -1), Vec3(0, 1, 0),
            60, aspect, 0, 1);
    }

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
                std::string obj_path = resolve_asset_path(
                    scene_dir, obj.has("obj") ? obj.at("obj").strVal : obj.at("path").strVal);
                double scale = obj.has("scale") ? obj.at("scale").numVal : 1.0;
                Vec3 translate = obj.has("translate") ? to_vec3(obj.at("translate")) : Vec3(0, 0, 0);
                Material* mat = ensure_material(obj, scene);
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
            } else {
                throw std::runtime_error("Unknown object type: " + type);
            }
        }
    }

    scene.primitive_count = scene.primitives.objects.size();
    if (scene.primitives.objects.empty()) {
        scene.world = std::make_unique<HittableList>(scene.primitives);
    } else {
        scene.world = std::make_unique<BVHNode>(scene.primitives.objects);
    }
}

#endif
