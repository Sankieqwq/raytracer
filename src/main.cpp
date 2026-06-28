// Module D: main -- CLI + JSON scene loading + render loop
#include "raytracer/math/vec3.h"
#include "raytracer/render/image.h"
#include "raytracer/math/util.h"
#include "raytracer/math/ray.h"
#include "raytracer/geometry/hittable.h"
#include "raytracer/scene/scene.h"
#include <vector>
#include <atomic>
#include <filesystem>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>

struct RenderOptions {
    bool direct_only = false;
    bool preview = false;
    int threads = 0;
};

bool is_shadowed(const Hittable& world, const Ray& shadow_ray, double max_t) {
    HitRecord shadow_rec;
    return world.hit(shadow_ray, 0.001, max_t, shadow_rec);
}

Color direct_lighting(const HitRecord& rec, const Scene& scene) {
    Color base = rec.material ? rec.material->base_color(rec) : Color(0.8, 0.8, 0.8);
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

Color ray_color(const Ray& r, const Scene& scene, int depth, const RenderOptions& options) {
    if (depth <= 0) return Color(0, 0, 0);

    HitRecord rec;
    if (scene.world->hit(r, 0.001, infinity, rec)) {
        Color direct = direct_lighting(rec, scene);
        if (options.direct_only) return direct;

        Ray scattered;
        Color attenuation;
        if (rec.material && rec.material->scatter(r, rec, attenuation, scattered))
            return direct + 0.35 * attenuation * ray_color(scattered, scene, depth - 1, options);
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
              << "  --samples <n>      samples per pixel (overrides scene)\n"
              << "  --threads <n>      render worker threads (default: hardware concurrency)\n"
              << "  --direct-only      disable recursive random bounces, use direct light + shadows only\n"
              << "  --preview          fast preview mode: direct-only and samples=1 unless overridden\n";
}

int main(int argc, char* argv[]) {
    std::string scene_path = "scenes/default.json";
    bool scene_specified = false;
    std::string out_override;
    std::string model_override;
    int samples_override = -1;
    RenderOptions render_options;

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
        } else if (arg == "--threads" && i + 1 < argc) {
            render_options.threads = std::stoi(argv[++i]);
            if (render_options.threads <= 0) {
                std::cerr << "--threads must be greater than 0\n";
                return 1;
            }
        } else if (arg == "--direct-only") {
            render_options.direct_only = true;
        } else if (arg == "--preview") {
            render_options.preview = true;
            render_options.direct_only = true;
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
    if (samples_override > 0)  {
        scene.samples = samples_override;
    } else if (render_options.preview) {
        scene.samples = 1;
    }

    unsigned int hardware_threads = std::thread::hardware_concurrency();
    int thread_count = render_options.threads > 0
        ? render_options.threads
        : static_cast<int>(hardware_threads == 0 ? 1 : hardware_threads);
    if (thread_count > scene.height) thread_count = scene.height;
    if (thread_count < 1) thread_count = 1;

    std::cout << "Scene: " << scene_path << "\n"
              << "Model: " << (model_override.empty() ? "(scene)" : load_options.model_override) << "\n"
              << "Image: " << scene.width << "x" << scene.height
              << ", samples=" << scene.samples
              << ", depth=" << scene.max_depth
              << ", mode=" << (render_options.direct_only ? "direct-only" : "path-tracing")
              << ", threads=" << thread_count << "\n"
              << "Primitives: " << scene.primitive_count << "\n";

    std::vector<Color> pixels(scene.width * scene.height);
    std::atomic<int> next_row{0};
    std::atomic<int> rows_done{0};
    std::atomic<int> last_pct{-1};

    auto render_worker = [&]() {
        while (true) {
            int j = next_row.fetch_add(1);
            if (j >= scene.height) break;

            int sample_row = scene.height - 1 - j;
            for (int i = 0; i < scene.width; i++) {
                Color col(0, 0, 0);
                for (int s = 0; s < scene.samples; s++) {
                    double offset_x = (render_options.direct_only && scene.samples == 1) ? 0.5 : random_double();
                    double offset_y = (render_options.direct_only && scene.samples == 1) ? 0.5 : random_double();
                    double u = (i + offset_x) / (scene.width - 1);
                    double v = (sample_row + offset_y) / (scene.height - 1);
                    col += ray_color(scene.camera->get_ray(u, v), scene, scene.max_depth, render_options);
                }
                pixels[j * scene.width + i] = col;
            }

            int done = rows_done.fetch_add(1) + 1;
            int pct = 100 * done / scene.height;
            int previous = last_pct.load();
            if (pct % 5 == 0 && pct != previous &&
                last_pct.compare_exchange_strong(previous, pct)) {
                std::cerr << "\rProgress: " << pct << "%" << std::flush;
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (int t = 0; t < thread_count; t++) {
        workers.emplace_back(render_worker);
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    std::cerr << "\rProgress: 100%\n";

    write_ppm(scene.output, scene.width, scene.height, pixels, scene.samples);
    std::cout << "Wrote " << scene.output << "\n";
    return 0;
}
