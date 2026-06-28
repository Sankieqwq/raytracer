// Module C: material -- abstract + concrete (Lambertian/Metal/Dielectric/PBR/Emissive)
#ifndef RT_MATERIAL_H
#define RT_MATERIAL_H

#include "raytracer/math/ray.h"
#include "raytracer/math/vec3.h"
#include "raytracer/math/util.h"
#include "raytracer/geometry/hittable.h"
#include "raytracer/render/texture.h"
#include <cmath>

class Material {
public:
    virtual ~Material() = default;
    virtual bool scatter(const Ray& r_in, const HitRecord& rec,
                         Color& attenuation, Ray& scattered,
                         Color& emission) const = 0;
};

class Lambertian : public Material {
public:
    Color albedo;
    Lambertian(const Color& albedo) : albedo(albedo) {}

    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered,
                 Color& emission) const override {
        emission = Color(0, 0, 0);
        (void)r_in;
        Vec3 dir = rec.normal + random_unit_vector();
        if (dir.length_squared() < 1e-8) dir = rec.normal;
        scattered = Ray(rec.p, dir);
        attenuation = albedo;
        return true;
    }
};

class Metal : public Material {
public:
    Color albedo;
    double fuzz;
    Metal(const Color& albedo, double fuzz = 0)
        : albedo(albedo), fuzz(fuzz < 1 ? fuzz : 1) {}

    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered,
                 Color& emission) const override {
        emission = Color(0, 0, 0);
        Vec3 reflected = reflect(r_in.direction.normalized(), rec.normal);
        scattered = Ray(rec.p, reflected + fuzz * random_in_unit_sphere());
        attenuation = albedo;
        return dot(scattered.direction, rec.normal) > 0;
    }
};

class Dielectric : public Material {
public:
    double ior;
    Dielectric(double index_of_refraction) : ior(index_of_refraction) {}

    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered,
                 Color& emission) const override {
        emission = Color(0, 0, 0);
        attenuation = Color(1.0, 1.0, 1.0);
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
    Texture albedo;
    Texture metallic;
    Texture roughness;
    Texture normal;
    bool has_normal_map = false;

    PBR(const Texture& albedo_tex, double metallic_val, double roughness_val)
        : albedo(albedo_tex),
          metallic(Texture(Color(metallic_val, 0, 0))),
          roughness(Texture(Color(roughness_val, 0, 0))),
          normal(Texture(Color(0.5, 0.5, 1.0))) {}

    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered,
                 Color& emission) const override {
        emission = Color(0, 0, 0);

        Color base = texture_sample(albedo, rec.u, rec.v);
        double met = texture_sample(metallic, rec.u, rec.v).x;
        double rough = std::max(0.001, texture_sample(roughness, rec.u, rec.v).x);

        Vec3 n = rec.normal;
        if (has_normal_map && rec.has_tangent) {
            Color ns = texture_sample(normal, rec.u, rec.v);
            Vec3 tn = Vec3(2*ns.x - 1, 2*ns.y - 1, 2*ns.z - 1).normalized();
            Vec3 T = rec.tangent.normalized();
            Vec3 B = cross(n, T).normalized();
            n = (T * tn.x + B * tn.y + n * tn.z).normalized();
            if (dot(n, rec.normal) < 0) n = -n;
        }

        Vec3 v = (-r_in.direction).normalized();
        double n_dot_v = std::max(0.0, dot(n, v));

        Vec3 up = std::fabs(n.x) > 0.9 ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
        Vec3 Tbn_T = cross(up, n).normalized();
        Vec3 Tbn_B = cross(n, Tbn_T);

        double r1 = random_double();
        double r2 = random_double();
        double alpha = rough * rough;
        double phi = 2 * pi * r1;
        double cos_theta = std::sqrt((1 - r2) / (1 + (alpha * alpha - 1) * r2));
        double sin_theta = std::sqrt(1 - cos_theta * cos_theta);
        Vec3 h_local(sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta);
        Vec3 h = (Tbn_T * h_local.x + Tbn_B * h_local.y + n * h_local.z).normalized();

        Vec3 scattered_dir = (2 * dot(v, h) * h - v).normalized();
        double n_dot_l = std::max(0.0, dot(n, scattered_dir));
        double n_dot_h = std::max(0.0, dot(n, h));
        double v_dot_h = std::max(0.0, dot(v, h));

        if (n_dot_l <= 0 || n_dot_v <= 0) return false;

        double a2 = alpha * alpha;
        double denom_d = n_dot_h * n_dot_h * (a2 - 1) + 1;
        double D = a2 / (pi * denom_d * denom_d);

        auto G1 = [&](double ndx) {
            double sq = std::sqrt(a2 + (1 - a2) * ndx * ndx);
            return 2 * ndx / (ndx + sq);
        };
        double G = G1(n_dot_l) * G1(n_dot_v);

        Color F0 = (1 - met) * Color(0.04, 0.04, 0.04) + met * base;
        Color F = F0 + (Color(1, 1, 1) - F0) * std::pow(1 - v_dot_h, 5);

        Color kD = (Color(1, 1, 1) - F) * (1 - met);
        Color diffuse = kD * base / pi;
        Color specular = F * (D * G) / (4 * n_dot_l * n_dot_v);
        Color brdf = diffuse + specular;

        double pdf = (D * n_dot_h) / (4 * v_dot_h);
        if (pdf <= 0) return false;

        attenuation = brdf * n_dot_l / pdf;
        scattered = Ray(rec.p, scattered_dir);
        return true;
    }
};

// Emissive material (area light)
class Emissive : public Material {
public:
    Color emission;
    Emissive(const Color& e) : emission(e) {}

    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered,
                 Color& emission_out) const override {
        (void)r_in; (void)rec; (void)scattered; (void)attenuation;
        emission_out = emission;
        return false;
    }
};

#endif
