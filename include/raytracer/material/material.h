// Module C: material -- abstract + concrete
#ifndef RT_MATERIAL_H
#define RT_MATERIAL_H

#include "raytracer/math/ray.h"
#include "raytracer/math/vec3.h"
#include "raytracer/geometry/hittable.h"
#include <cmath>

class Material {
public:
    virtual ~Material() = default;
    virtual bool scatter(const Ray& r_in, const HitRecord& rec,
                         Color& attenuation, Ray& scattered) const = 0;
    virtual Color base_color() const { return Color(0.8, 0.8, 0.8); }
};

class Lambertian : public Material {
public:
    Color albedo;
    Lambertian(const Color& albedo) : albedo(albedo) {}

    Color base_color() const override { return albedo; }

    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered) const override {
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

    Color base_color() const override { return albedo; }

    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered) const override {
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

    Color base_color() const override { return Color(1.0, 1.0, 1.0); }

    bool scatter(const Ray& r_in, const HitRecord& rec,
                 Color& attenuation, Ray& scattered) const override {
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

#endif
