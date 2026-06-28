// Module D: main -- CLI + JSON scene loading + render loop
#include "raytracer/math/vec3.h"
#include "raytracer/render/image.h"
#include "raytracer/math/util.h"
#include "raytracer/math/ray.h"
#include "raytracer/geometry/hittable.h"
#include "raytracer/scene/scene.h"
#include <vector>
#include <filesystem>
#include <cmath>
#include <iostream>
#include <string>

bool is_shadowed(const Hittable& world, const Ray& shadow_ray, double max_t) {
    HitRecord shadow_rec;
    return world.hit(shadow_ray, 0.001, max_t, shadow_rec);
}

Color direct_lighting(const HitRecord& rec, const Scene& scene) {
    Color base = rec.material ? rec.material->base_color() : Color(0.8, 0.8, 0.8);
    Color result = base * scene.ambient_light;

    for (const Light& light : scene.lights) {
        Vec3 light_dir;
        double max_t = infinity;
        double attenuation = 1.0;

        if (light.type == LightType::Point) {
            Vec3 to_light = light.position - rec.p;
            double dist2 = to_light.length_squared();
            if (dist2 <= 1e-8) continue;
            double dist = std::sqrt(dist2);
            light_dir = to_light / dist;
            max_t = dist - 0.001;
            attenuation = 1.0 / dist2;
        } else {
            light_dir = (-light.direction).normalized();
        }

        double n_dot_l = dot(rec.normal, light_dir);
        if (n_dot_l <= 0) continue;

        Ray shadow_ray(rec.p + 0.001 * rec.normal, light_dir);
        if (is_shadowed(*scene.world, shadow_ray, max_t)) continue;

        result += base * light.color * (light.intensity * attenuation * n_dot_l);
    }

    return result;
}

Color ray_color(const Ray& r, const Scene& scene, int depth) {
    if (depth <= 0) return Color(0, 0, 0);

    HitRecord rec;
    if (scene.world->hit(r, 0.001, infinity, rec)) {
        Color direct = direct_lighting(rec, scene);
        Ray scattered;
        Color attenuation;
        if (rec.material && rec.material->scatter(r, rec, attenuation, scattered))
            return direct + 0.35 * attenuation * ray_color(scattered, scene, depth - 1);
        return direct;
    }

    Vec3 unit_dir = r.direction.normalized();
    double t = 0.5 * (unit_dir.y + 1.0);
    return (1 - t) * Color(1.0, 1.0, 1.0) + t * Color(0.5, 0.7, 1.0);
}

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "  --scene <path>     scene JSON file (default: scenes/default.json)\n"
              << "  --model <path>     OBJ/GLB model path (uses scenes/obj.json if --scene is omitted)\n"
              << "  --obj <path>       alias of --model\n"
              << "  --out <path>       output PPM file (overrides scene)\n"
              << "  --samples <n>      samples per pixel (overrides scene)\n";
}

int main(int argc, char* argv[]) {
    std::string scene_path = "scenes/default.json";
    bool scene_specified = false;
    std::string out_override;
    std::string model_override;
    int samples_override = -1;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--scene" && i + 1 < argc) {
            scene_path = argv[++i];
            scene_specified = true;
        } else if ((arg == "--model" || arg == "--obj") && i + 1 < argc) {
            model_override = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            out_override = argv[++i];
        } else if (arg == "--samples" && i + 1 < argc) {
            samples_override = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!model_override.empty() && !scene_specified) {
        scene_path = "scenes/obj.json";
    }

    SceneLoadOptions load_options;
    if (!model_override.empty()) {
        std::filesystem::path model_path(model_override);
        if (model_path.is_relative()) {
            model_path = std::filesystem::absolute(model_path);
        }
        load_options.model_override = model_path.lexically_normal().string();
    }

    Scene scene;
    try {
        load_scene(scene_path, scene, load_options);
    } catch (const std::exception& e) {
        std::cerr << "Error loading scene: " << e.what() << "\n";
        return 1;
    }

    if (!out_override.empty()) scene.output = out_override;
    if (samples_override > 0)  scene.samples = samples_override;

    std::cout << "Scene: " << scene_path << "\n"
              << "Model: " << (model_override.empty() ? "(scene)" : load_options.model_override) << "\n"
              << "Image: " << scene.width << "x" << scene.height
              << ", samples=" << scene.samples
              << ", depth=" << scene.max_depth << "\n"
              << "Primitives: " << scene.primitive_count << "\n";

    std::vector<Color> pixels(scene.width * scene.height);
    int last_pct = -1;

    for (int j = 0; j < scene.height; j++) {
        int pct = 100 * j / scene.height;
        if (pct % 5 == 0 && pct != last_pct) {
            std::cerr << "\rProgress: " << pct << "%" << std::flush;
            last_pct = pct;
        }
        int sample_row = scene.height - 1 - j;
        for (int i = 0; i < scene.width; i++) {
            Color col(0, 0, 0);
            for (int s = 0; s < scene.samples; s++) {
                double u = (i + random_double()) / (scene.width - 1);
                double v = (sample_row + random_double()) / (scene.height - 1);
                col += ray_color(scene.camera->get_ray(u, v), scene, scene.max_depth);
            }
            pixels[j * scene.width + i] = col;
        }
    }
    std::cerr << "\rProgress: 100%\n";

    write_ppm(scene.output, scene.width, scene.height, pixels, scene.samples);
    std::cout << "Wrote " << scene.output << "\n";
    return 0;
}
