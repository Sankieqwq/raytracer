// Module D: main -- CLI + JSON scene loading + render loop
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION

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

double total_emissive_area(const Scene& scene) {
    double total_area = 0;
    for (const EmissiveObject& eo : scene.emissive_objects) {
        total_area += eo.geometry->area();
    }
    return total_area;
}

const EmissiveObject* sample_emissive_by_area(const Scene& scene,
                                              double r,
                                              double total_area) {
    if (total_area <= 0) return nullptr;
    double target = r * total_area;
    double accum = 0;
    for (const EmissiveObject& eo : scene.emissive_objects) {
        accum += eo.geometry->area();
        if (target <= accum) return &eo;
    }
    return scene.emissive_objects.empty() ? nullptr : &scene.emissive_objects.back();
}

Color direct_delta_lights(const Ray& r_in, const HitRecord& rec, const Scene& scene) {
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

        Ray light_ray(rec.p, light_dir);
        Color brdf = rec.material ? rec.material->f(r_in, light_ray, rec) : base / pi;
        result += brdf * light.color * (light.intensity * attenuation * n_dot_l);
    }

    return result;
}

Color ray_color(const Ray& r, const Scene& scene, int depth,
                const RenderOptions& options,
                double prev_pdf, bool prev_brdf) {
    if (depth <= 0) return Color(0, 0, 0);

    HitRecord rec;
    if (!scene.world->hit(r, 0.001, infinity, rec)) {
        Vec3 unit_dir = r.direction.normalized();
        double t = 0.5 * (unit_dir.y + 1.0);
        return (1 - t) * Color(1.0, 1.0, 1.0) + t * Color(0.5, 0.7, 1.0);
    }

    if (rec.material && rec.material->is_emissive()) {
        Emissive* em = static_cast<Emissive*>(rec.material);
        if (prev_brdf && prev_pdf > 0 && !scene.emissive_objects.empty()) {
            double total_area = total_emissive_area(scene);
            double pdf_light = 0;
            if (total_area > 0) {
                double dist2 = (rec.p - r.origin).length_squared();
                double cos_light = dot(rec.normal, -r.direction.normalized());
                if (cos_light > 0) pdf_light = dist2 / (cos_light * total_area);
            }
            double w_brdf = prev_pdf / (prev_pdf + pdf_light);
            return em->emission * w_brdf;
        }
        return em->emission;
    }

    Ray scattered;
    Color attenuation, emission;
    bool did_scatter = rec.material && rec.material->scatter(r, rec, attenuation, scattered, emission);

    if (rec.material && rec.material->is_specular()) {
        if (options.direct_only) return emission;
        if (!did_scatter) return emission;
        double brdf_pdf = rec.material->pdf(r, scattered, rec);
        if (brdf_pdf <= 0) brdf_pdf = 1;
        return emission + attenuation * ray_color(scattered, scene, depth - 1, options, brdf_pdf, true);
    }

    Color direct = direct_delta_lights(r, rec, scene);

    if (!scene.emissive_objects.empty()) {
        double total_area = total_emissive_area(scene);
        const EmissiveObject* eo = sample_emissive_by_area(scene, random_double(), total_area);
        Vec3 light_normal;
        Point3 light_point = eo ? eo->geometry->sample_point(random_double(), random_double(), &light_normal) : Point3();

        Vec3 to_light = light_point - rec.p;
        double dist2 = to_light.length_squared();
        if (eo && total_area > 0 && dist2 > 1e-8) {
            double dist = std::sqrt(dist2);
            Vec3 light_dir = to_light / dist;
            double n_dot_l = dot(rec.normal, light_dir);
            if (n_dot_l > 0) {
                double cos_light = dot(light_normal, -light_dir);
                if (cos_light > 0) {
                    Ray shadow_ray(rec.p + 0.001 * rec.normal, light_dir);
                    if (!is_shadowed(*scene.world, shadow_ray, dist - 0.001)) {
                        double pdf_light = (dist2 / cos_light) / total_area;
                        Ray light_ray(rec.p, light_dir);
                        Color f_val = rec.material ? rec.material->f(r, light_ray, rec) : Color(0,0,0);
                        double brdf_pdf = rec.material ? rec.material->pdf(r, light_ray, rec) : 0;
                        double w_light = pdf_light / (pdf_light + brdf_pdf);
                        direct += eo->emission * f_val * n_dot_l * w_light / pdf_light;
                    }
                }
            }
        }
    }

    if (options.direct_only) return emission + direct;
    if (!did_scatter) return emission + direct;

    double brdf_pdf = rec.material->pdf(r, scattered, rec);
    Color f_val = rec.material->f(r, scattered, rec);
    if (brdf_pdf <= 0) {
        return emission + direct + attenuation * ray_color(scattered, scene, depth - 1, options, 1.0, true);
    }

    Color indirect = f_val * dot(rec.normal, scattered.direction) / brdf_pdf
                   * ray_color(scattered, scene, depth - 1, options, brdf_pdf, true);

    return emission + direct + indirect;
}

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "  --scene <path>     scene JSON file (default: scenes/default.json)\n"
              << "  --model <path>     OBJ/GLB model path (uses scenes/obj.json if --scene is omitted)\n"
              << "  --obj <path>       alias of --model\n"
              << "  --out <path>       output image path, .ppm or .png (overrides scene)\n"
              << "  --samples <n>      samples per pixel (overrides scene)\n"
              << "  --threads <n>      render worker threads (default: hardware concurrency)\n"
              << "  --exposure <n>     display exposure multiplier (default: scene/default 1.0)\n"
              << "  --tone-map <mode>  tone mapping: aces, reinhard, none\n"
              << "  --seed <n>         deterministic random seed (default: random_device)\n"
              << "  --direct-only      disable recursive random bounces, use direct light + shadows only\n"
              << "  --preview          fast preview mode: direct-only and samples=1 unless overridden\n";
}

int main(int argc, char* argv[]) {
    std::string scene_path = "scenes/default.json";
    bool scene_specified = false;
    std::string out_override;
    std::string model_override;
    int samples_override = -1;
    double exposure_override = -1.0;
    std::string tone_map_override;
    long long seed_override = -1;
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
        } else if (arg == "--exposure" && i + 1 < argc) {
            exposure_override = std::stod(argv[++i]);
        } else if (arg == "--tone-map" && i + 1 < argc) {
            tone_map_override = argv[++i];
        } else if (arg == "--seed" && i + 1 < argc) {
            seed_override = std::stoll(argv[++i]);
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
    if (exposure_override > 0) scene.output_options.exposure = exposure_override;
    if (!tone_map_override.empty()) scene.output_options.tone_map = parse_tone_map_mode(tone_map_override);
    if (seed_override >= 0) {
        set_random_seed(static_cast<unsigned int>(seed_override));
    } else if (scene.has_seed) {
        set_random_seed(scene.seed);
    }
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
                    col += ray_color(scene.camera->get_ray(u, v), scene, scene.max_depth, render_options, infinity, false);
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

    write_image(scene.output, scene.width, scene.height, pixels, scene.samples, scene.output_options);
    std::cout << "Wrote " << scene.output << "\n";
    return 0;
}
