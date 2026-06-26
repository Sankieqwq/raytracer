// Module B: geometry -- sphere (STUB: owner fills real intersection)
#ifndef RT_SPHERE_H
#define RT_SPHERE_H

#include "raytracer/geometry/hittable.h"
#include <cmath>

class Sphere : public Hittable {
public:
    Point3 center;
    double radius;
    Material* material;

    Sphere() : center(), radius(0), material(nullptr) {}
    Sphere(Point3 center, double radius, Material* material = nullptr)
        : center(center), radius(radius), material(material) {}

    bool hit(const Ray& r, double t_min, double t_max,
             HitRecord& rec) const override {
        Vec3 oc = r.origin - center;
        double a = r.direction.length_squared();
        double half_b = dot(oc, r.direction);
        double c = oc.length_squared() - radius * radius;
        double disc = half_b * half_b - a * c;
        if (disc < 0) return false;
        double sq = std::sqrt(disc);

        double root = (-half_b - sq) / a;
        if (root < t_min || root > t_max) {
            root = (-half_b + sq) / a;
            if (root < t_min || root > t_max) return false;
        }

        rec.t = root;
        rec.p = r.at(root);
        Vec3 outward = (rec.p - center) / radius;
        rec.set_face_normal(r, outward);
        rec.material = material;
        return true;
    }
};

#endif
