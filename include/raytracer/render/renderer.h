// Module D: render -- reusable render core shared by CLI and integrations
#ifndef RT_RENDERER_H
#define RT_RENDERER_H

#include "raytracer/geometry/hittable.h"
#include "raytracer/math/ray.h"
#include "raytracer/math/util.h"
#include "raytracer/math/vec3.h"
#include "raytracer/scene/scene.h"
#include <atomic>
#include <cmath>
#include <functional>
#include <thread>
#include <vector>

struct RenderOptions {
    bool direct_only = false;
    bool preview = false;
    int threads = 0;
};

struct RenderCallbacks {
    std::function<void(double)> progress;
    std::function<bool()> should_cancel;
};

struct RenderOutput {
    int width = 0;
    int height = 0;
    int samples = 1;
    bool cancelled = false;
    std::vector<Color> pixels;
};

inline bool is_shadowed(const Hittable& world, const Ray& shadow_ray, double max_t) {
    HitRecord shadow_rec;
    return world.hit(shadow_ray, 0.001, max_t, shadow_rec);
}

inline Color direct_delta_lights(const HitRecord& rec, const Scene& scene) {
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

inline Color ray_color(const Ray& r, const Scene& scene, int depth,
                       const RenderOptions& options,
                       double prev_pdf,
                       bool prev_brdf) {
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
            double total_area = 0;
            for (const EmissiveObject& eo : scene.emissive_objects) total_area += eo.geometry->area();
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

    Color direct = direct_delta_lights(rec, scene);

    if (!scene.emissive_objects.empty()) {
        double r1 = random_double(), r2 = random_double();
        size_t idx = static_cast<size_t>(random_double() * scene.emissive_objects.size());
        if (idx >= scene.emissive_objects.size()) idx = scene.emissive_objects.size() - 1;
        const EmissiveObject& eo = scene.emissive_objects[idx];
        Vec3 light_normal;
        Point3 light_point = eo.geometry->sample_point(r1, r2, &light_normal);

        Vec3 to_light = light_point - rec.p;
        double dist2 = to_light.length_squared();
        if (dist2 > 1e-8) {
            double dist = std::sqrt(dist2);
            Vec3 light_dir = to_light / dist;
            double n_dot_l = dot(rec.normal, light_dir);
            if (n_dot_l > 0) {
                double cos_light = dot(light_normal, -light_dir);
                if (cos_light > 0) {
                    Ray shadow_ray(rec.p + 0.001 * rec.normal, light_dir);
                    if (!is_shadowed(*scene.world, shadow_ray, dist - 0.001)) {
                        double total_area = 0;
                        for (const EmissiveObject& e : scene.emissive_objects) total_area += e.geometry->area();
                        double pdf_light = (dist2 / cos_light) / total_area;
                        Ray light_ray(rec.p, light_dir);
                        Color f_val = rec.material ? rec.material->f(r, light_ray, rec) : Color(0, 0, 0);
                        double brdf_pdf = rec.material ? rec.material->pdf(r, light_ray, rec) : 0;
                        double w_light = pdf_light / (pdf_light + brdf_pdf);
                        direct += eo.emission * f_val * n_dot_l * w_light / pdf_light;
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

inline int resolve_thread_count(const Scene& scene, const RenderOptions& options) {
    unsigned int hardware_threads = std::thread::hardware_concurrency();
    int thread_count = options.threads > 0
        ? options.threads
        : static_cast<int>(hardware_threads == 0 ? 1 : hardware_threads);
    if (thread_count > scene.height) thread_count = scene.height;
    if (thread_count < 1) thread_count = 1;
    return thread_count;
}

inline RenderOutput render_scene(const Scene& scene,
                                 const RenderOptions& options,
                                 const RenderCallbacks& callbacks = RenderCallbacks()) {
    RenderOutput output;
    output.width = scene.width;
    output.height = scene.height;
    output.samples = scene.samples;
    output.pixels.assign(static_cast<size_t>(scene.width) * static_cast<size_t>(scene.height),
                         Color(0, 0, 0));

    int thread_count = resolve_thread_count(scene, options);
    std::atomic<int> next_row{0};
    std::atomic<int> rows_done{0};
    std::atomic<int> last_pct{-1};
    std::atomic<bool> cancelled{false};

    auto report_progress = [&](int done_rows) {
        if (!callbacks.progress || scene.height <= 0) return;
        int pct = 100 * done_rows / scene.height;
        int previous = last_pct.load();
        if ((pct % 5 == 0 || done_rows == scene.height) && pct != previous &&
            last_pct.compare_exchange_strong(previous, pct)) {
            callbacks.progress(double(done_rows) / double(scene.height));
        }
    };

    auto render_worker = [&]() {
        while (!cancelled.load()) {
            if (callbacks.should_cancel && callbacks.should_cancel()) {
                cancelled.store(true);
                break;
            }

            int j = next_row.fetch_add(1);
            if (j >= scene.height) break;

            int sample_row = scene.height - 1 - j;
            for (int i = 0; i < scene.width; i++) {
                Color col(0, 0, 0);
                for (int s = 0; s < scene.samples; s++) {
                    double offset_x = (options.direct_only && scene.samples == 1) ? 0.5 : random_double();
                    double offset_y = (options.direct_only && scene.samples == 1) ? 0.5 : random_double();
                    double u = (i + offset_x) / (scene.width - 1);
                    double v = (sample_row + offset_y) / (scene.height - 1);
                    col += ray_color(scene.camera->get_ray(u, v), scene, scene.max_depth, options, infinity, false);
                }
                output.pixels[static_cast<size_t>(j) * static_cast<size_t>(scene.width) +
                              static_cast<size_t>(i)] = col;
            }

            int done = rows_done.fetch_add(1) + 1;
            report_progress(done);
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(thread_count));
    for (int t = 0; t < thread_count; t++) {
        workers.emplace_back(render_worker);
    }
    for (std::thread& worker : workers) {
        worker.join();
    }

    output.cancelled = cancelled.load();
    if (!output.cancelled && callbacks.progress) callbacks.progress(1.0);
    return output;
}

#endif
