// Scene loader: JSON -> Camera + HittableList + image params
#ifndef RT_SCENE_H
#define RT_SCENE_H

#include "raytracer/scene/json.h"
#include "raytracer/math/vec3.h"
#include "raytracer/render/camera.h"
#include "raytracer/geometry/hittable.h"
#include "raytracer/geometry/sphere.h"
#include "raytracer/geometry/hittable_list.h"
#include "raytracer/material/material.h"
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
    HittableList world;

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

inline void load_scene(const std::string& path, Scene& scene) {
    JsonValue root = parse_json_file(path);

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
                Material* mat = parse_material(obj.at("material"), scene);
                auto sph = std::make_unique<Sphere>(center, radius, mat);
                scene.world.add(sph.get());
                scene.objects.push_back(std::move(sph));
            } else {
                throw std::runtime_error("Unknown object type: " + type);
            }
        }
    }
}

#endif
