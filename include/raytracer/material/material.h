// Module C: material -- abstract + concrete (Lambertian/Metal/Dielectric/PBR/Emissive)
#ifndef RT_MATERIAL_H
#define RT_MATERIAL_H

#include "raytracer/geometry/hittable.h"
#include "raytracer/material/texture.h"
#include "raytracer/math/ray.h"
#include "raytracer/math/util.h"
#include "raytracer/math/vec3.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

class Material {
public:
    virtual ~Material() = default;
    virtual bool scatter(const Ray& r_in, const HitRecord& rec,
                         Color& attenuation, Ray& scattered,
                         Color& emission) const = 0;
    virtual Color base_color(const HitRecord& rec) const {
        (void)rec;
        return Color(0.8, 0.8, 0.8);
    }
    virtual bool is_transparent() const { return false; }
    virtual Color f(const Ray& r_in, const Ray& scattered, const HitRecord& rec) const {
        (void)r_in; (void)scattered; (void)rec;
        return Color(0, 0, 0);
    }
    virtual double pdf(const Ray& r_in, const Ray& scattered, const HitRecord& rec) const {
        (void)r_in; (void)scattered; (void)rec;
        return 0;
    }
    virtual bool is_specular() const { return false; }
    virtual bool is_emissive() const { return false; }
};

class Lambertian : public Material {
public:
    std::shared_ptr<Texture> texture;

    explicit Lambertian(const Color& albedo)
        : texture(std::make_shared<SolidColorTexture>(albedo)) {}
    explicit Lambertian(std::shared_ptr<Texture> texture)
        : texture(std::move(texture)) {}

    Color base_color(const HitRecord& rec) const override {
        return texture->value(rec.u, rec.v, rec.p);
    }

    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered,
                 Color& emission) const override {
        (void)r_in;
        emission = Color(0, 0, 0);
        Vec3 dir = rec.normal + random_unit_vector();
        if (dir.length_squared() < 1e-8) dir = rec.normal;
        scattered = Ray(rec.p, dir);
        attenuation = base_color(rec);
        return true;
    }

    Color f(const Ray& r_in, const Ray& scattered, const HitRecord& rec) const override {
        (void)r_in; (void)scattered;
        return base_color(rec) / pi;
    }

    double pdf(const Ray& r_in, const Ray& scattered, const HitRecord& rec) const override {
        (void)r_in;
        double cos_theta = dot(rec.normal, scattered.direction);
        return cos_theta > 0 ? cos_theta / pi : 0;
    }
};

class Metal : public Material {
public:
    std::shared_ptr<Texture> texture;
    double fuzz;

    Metal(const Color& albedo, double fuzz = 0)
        : texture(std::make_shared<SolidColorTexture>(albedo)),
          fuzz(std::min(fuzz, 1.0)) {}
    Metal(std::shared_ptr<Texture> texture, double fuzz = 0)
        : texture(std::move(texture)), fuzz(std::min(fuzz, 1.0)) {}

    Color base_color(const HitRecord& rec) const override {
        return texture->value(rec.u, rec.v, rec.p);
    }

    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered,
                 Color& emission) const override {
        emission = Color(0, 0, 0);
        Vec3 reflected = reflect(r_in.direction.normalized(), rec.normal);
        scattered = Ray(rec.p, reflected + fuzz * random_in_unit_sphere());
        attenuation = base_color(rec);
        return dot(scattered.direction, rec.normal) > 0;
    }

    bool is_specular() const override { return true; }
};

class Dielectric : public Material {
public:
    double ior;
    Color albedo;
    std::shared_ptr<Texture> albedo_texture;

    explicit Dielectric(double index_of_refraction, Color albedo = Color(1.0, 1.0, 1.0))
        : ior(index_of_refraction), albedo(albedo) {}

    Dielectric(double index_of_refraction, std::shared_ptr<Texture> texture)
        : ior(index_of_refraction), albedo(1.0, 1.0, 1.0), albedo_texture(std::move(texture)) {}

    Color base_color(const HitRecord& rec) const override {
        return albedo_texture ? albedo_texture->value(rec.u, rec.v, rec.p) : albedo;
    }

    bool is_transparent() const override { return true; }
    bool is_specular() const override { return true; }

    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered,
                 Color& emission) const override {
        emission = Color(0, 0, 0);
        attenuation = base_color(rec);
        double ratio = rec.front_face ? (1.0 / ior) : ior;
        Vec3 unit_dir = r_in.direction.normalized();
        double cos_theta = std::fmin(dot(-unit_dir, rec.normal), 1.0);
        double sin_theta = std::sqrt(1.0 - cos_theta * cos_theta);

        bool cannot_refract = ratio * sin_theta > 1.0;
        Vec3 dir;
        if (cannot_refract || schlick(cos_theta, ratio) > random_double())
            dir = reflect(unit_dir, rec.normal);
        else
            dir = refract(unit_dir, rec.normal, ratio);

        scattered = Ray(rec.p, dir);
        return true;
    }

private:
    static double schlick(double cosine, double ref_idx) {
        double r0 = (1 - ref_idx) / (1 + ref_idx);
        r0 = r0 * r0;
        return r0 + (1 - r0) * std::pow(1 - cosine, 5);
    }
};

// PBR metal-roughness with Cook-Torrance BRDF (GGX + Smith + Schlick)
class PBR : public Material {
public:
    std::shared_ptr<Texture> albedo;
    std::shared_ptr<Texture> metallic;
    std::shared_ptr<Texture> roughness;
    std::shared_ptr<Texture> normal;
    bool has_normal_map = false;

    PBR(std::shared_ptr<Texture> albedo_tex, double metallic_val, double roughness_val)
        : albedo(std::move(albedo_tex)),
          metallic(std::make_shared<SolidColorTexture>(Color(metallic_val, 0, 0))),
          roughness(std::make_shared<SolidColorTexture>(Color(roughness_val, 0, 0))),
          normal(std::make_shared<SolidColorTexture>(Color(0.5, 0.5, 1.0))) {}

    Color base_color(const HitRecord& rec) const override {
        return albedo->value(rec.u, rec.v, rec.p);
    }

    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered,
                 Color& emission) const override {
        emission = Color(0, 0, 0);

        double rough = roughness_value(rec);
        Vec3 n = shading_normal(rec);
        Vec3 v = (-r_in.direction).normalized();
        double n_dot_v = std::max(0.0, dot(n, v));
        if (n_dot_v <= 0) return false;

        Vec3 up = std::fabs(n.x) > 0.9 ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
        Vec3 tbn_t = cross(up, n).normalized();
        Vec3 tbn_b = cross(n, tbn_t);

        double r1 = random_double();
        double r2 = random_double();
        double alpha = rough * rough;
        double phi = 2 * pi * r1;
        double cos_theta = std::sqrt((1 - r2) / (1 + (alpha * alpha - 1) * r2));
        double sin_theta = std::sqrt(1 - cos_theta * cos_theta);
        Vec3 h_local(sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta);
        Vec3 h = (tbn_t * h_local.x + tbn_b * h_local.y + n * h_local.z).normalized();

        Vec3 scattered_dir = (2 * dot(v, h) * h - v).normalized();
        double n_dot_l = std::max(0.0, dot(n, scattered_dir));
        if (n_dot_l <= 0 || dot(rec.normal, scattered_dir) <= 0) return false;

        scattered = Ray(rec.p, scattered_dir);
        double pdf_val = pdf(r_in, scattered, rec);
        if (pdf_val <= 0) return false;

        attenuation = f(r_in, scattered, rec) * n_dot_l / pdf_val;
        return true;
    }

    Color f(const Ray& r_in, const Ray& scattered, const HitRecord& rec) const override {
        Vec3 n = shading_normal(rec);
        Vec3 v = (-r_in.direction).normalized();
        Vec3 l = scattered.direction.normalized();
        double n_dot_l = std::max(0.0, dot(n, l));
        double n_dot_v = std::max(0.0, dot(n, v));
        if (n_dot_l <= 0 || n_dot_v <= 0) return Color(0, 0, 0);

        Vec3 h = (v + l).normalized();
        double n_dot_h = std::max(0.0, dot(n, h));
        double v_dot_h = std::max(0.0, dot(v, h));
        if (n_dot_h <= 0 || v_dot_h <= 0) return Color(0, 0, 0);

        Color base = base_color(rec);
        double met = metallic_value(rec);
        double rough = roughness_value(rec);
        double alpha = rough * rough;
        double a2 = alpha * alpha;
        double denom_d = n_dot_h * n_dot_h * (a2 - 1) + 1;
        double D = a2 / (pi * denom_d * denom_d);

        auto g1 = [&](double ndx) {
            double sq = std::sqrt(a2 + (1 - a2) * ndx * ndx);
            return 2 * ndx / (ndx + sq);
        };
        double G = g1(n_dot_l) * g1(n_dot_v);

        Color F0 = (1 - met) * Color(0.04, 0.04, 0.04) + met * base;
        Color F = F0 + (Color(1, 1, 1) - F0) * std::pow(1 - v_dot_h, 5);

        Color kD = (Color(1, 1, 1) - F) * (1 - met);
        Color diffuse = kD * base / pi;
        Color specular = F * (D * G) / (4 * n_dot_l * n_dot_v);
        return diffuse + specular;
    }

    double pdf(const Ray& r_in, const Ray& scattered, const HitRecord& rec) const override {
        Vec3 n = shading_normal(rec);
        Vec3 v = (-r_in.direction).normalized();
        Vec3 l = scattered.direction.normalized();
        if (dot(n, l) <= 0 || dot(n, v) <= 0) return 0;

        Vec3 h = (v + l).normalized();
        double n_dot_h = std::max(0.0, dot(n, h));
        double v_dot_h = std::max(0.0, dot(v, h));
        if (n_dot_h <= 0 || v_dot_h <= 0) return 0;

        double rough = roughness_value(rec);
        double alpha = rough * rough;
        double a2 = alpha * alpha;
        double denom_d = n_dot_h * n_dot_h * (a2 - 1) + 1;
        double D = a2 / (pi * denom_d * denom_d);
        double pdf_val = (D * n_dot_h) / (4 * v_dot_h);
        return pdf_val > 0 && std::isfinite(pdf_val) ? pdf_val : 0;
    }

    bool is_specular() const override { return false; }

private:
    double metallic_value(const HitRecord& rec) const {
        return std::clamp(metallic->value(rec.u, rec.v, rec.p).x, 0.0, 1.0);
    }

    double roughness_value(const HitRecord& rec) const {
        double rough = roughness->value(rec.u, rec.v, rec.p).x;
        return std::clamp(rough, 0.001, 1.0);
    }

    Vec3 shading_normal(const HitRecord& rec) const {
        Vec3 n = rec.normal;
        if (has_normal_map && rec.has_tangent) {
            Color ns = normal->value(rec.u, rec.v, rec.p);
            Vec3 tn = Vec3(2 * ns.x - 1, 2 * ns.y - 1, 2 * ns.z - 1).normalized();
            Vec3 T = rec.tangent.normalized();
            Vec3 B = cross(n, T).normalized();
            n = (T * tn.x + B * tn.y + n * tn.z).normalized();
            if (dot(n, rec.normal) < 0) n = -n;
        }
        return n;
    }
};

// Emissive material (area light)
class Emissive : public Material {
public:
    Color emission;

    explicit Emissive(const Color& e) : emission(e) {}

    Color base_color(const HitRecord& rec) const override {
        (void)rec;
        return emission;
    }

    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered,
                 Color& emission_out) const override {
        (void)r_in;
        (void)rec;
        (void)scattered;
        (void)attenuation;
        emission_out = emission;
        return false;
    }

    bool is_emissive() const override { return true; }
};

#endif
