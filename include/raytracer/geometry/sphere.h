// Module B: geometry -- sphere (STUB: owner fills real intersection)
#ifndef RT_SPHERE_H
#define RT_SPHERE_H

#include "raytracer/geometry/hittable.h"
#include "raytracer/math/util.h"
#include <algorithm>
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
        // UV: spherical coordinates. oc = normalized outward direction.
        double theta = std::acos(std::clamp(-outward.y, -1.0, 1.0));
        double phi = std::atan2(-outward.z, outward.x) + pi;
        rec.u = phi / (2 * pi);
        rec.v = theta / pi;
        // Tangent: along longitude (dp/du direction)
        rec.tangent = Vec3(-outward.z, 0, outward.x).normalized();
        rec.has_tangent = true;
        rec.material = material;
        return true;
    }

    bool bounding_box(AABB& output_box) const override {
        Vec3 radius_vec(radius, radius, radius);
        output_box = AABB(center - radius_vec, center + radius_vec);
        return true;
    }
};

#endif
