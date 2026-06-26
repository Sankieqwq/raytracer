// Module D: main -- CLI + JSON scene loading + render loop
#include "raytracer/math/vec3.h"
#include "raytracer/render/image.h"
#include "raytracer/math/util.h"
#include "raytracer/math/ray.h"
#include "raytracer/geometry/hittable.h"
#include "raytracer/scene/scene.h"
#include <vector>
#include <iostream>
#include <string>

Color ray_color(const Ray& r, const Hittable& world, int depth) {
    if (depth <= 0) return Color(0, 0, 0);

    HitRecord rec;
    if (world.hit(r, 0.001, infinity, rec)) {
        Ray scattered;
        Color attenuation;
        if (rec.material && rec.material->scatter(r, rec, attenuation, scattered))
            return attenuation * ray_color(scattered, world, depth - 1);
        return Color(0, 0, 0);
    }

    Vec3 unit_dir = r.direction.normalized();
    double t = 0.5 * (unit_dir.y + 1.0);
    return (1 - t) * Color(1.0, 1.0, 1.0) + t * Color(0.5, 0.7, 1.0);
}

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "  --scene <path>     scene JSON file (default: scenes/default.json)\n"
              << "  --out <path>       output PPM file (overrides scene)\n"
              << "  --samples <n>      samples per pixel (overrides scene)\n";
}

int main(int argc, char* argv[]) {
    std::string scene_path = "scenes/default.json";
    std::string out_override;
    int samples_override = -1;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--scene" && i + 1 < argc) {
            scene_path = argv[++i];
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

    Scene scene;
    try {
        load_scene(scene_path, scene);
    } catch (const std::exception& e) {
        std::cerr << "Error loading scene: " << e.what() << "\n";
        return 1;
    }

    if (!out_override.empty()) scene.output = out_override;
    if (samples_override > 0)  scene.samples = samples_override;

    std::cout << "Scene: " << scene_path << "\n"
              << "Image: " << scene.width << "x" << scene.height
              << ", samples=" << scene.samples
              << ", depth=" << scene.max_depth << "\n"
              << "Objects: " << scene.world.objects.size() << "\n";

    std::vector<Color> pixels(scene.width * scene.height);
    int last_pct = -1;

    for (int j = 0; j < scene.height; j++) {
        int pct = 100 * j / scene.height;
        if (pct % 5 == 0 && pct != last_pct) {
            std::cerr << "\rProgress: " << pct << "%" << std::flush;
            last_pct = pct;
        }
        for (int i = 0; i < scene.width; i++) {
            Color col(0, 0, 0);
            for (int s = 0; s < scene.samples; s++) {
                double u = (i + random_double()) / (scene.width - 1);
                double v = (j + random_double()) / (scene.height - 1);
                col += ray_color(scene.camera->get_ray(u, v), scene.world, scene.max_depth);
            }
            pixels[j * scene.width + i] = col;
        }
    }
    std::cerr << "\rProgress: 100%\n";

    write_ppm(scene.output, scene.width, scene.height, pixels, scene.samples);
    std::cout << "Wrote " << scene.output << "\n";
    return 0;
}
