// Module B: geometry -- hittable abstraction + hit record
#ifndef RT_HITTABLE_H
#define RT_HITTABLE_H

#include "raytracer/math/ray.h"
#include "raytracer/geometry/aabb.h"
#include <cmath>

class Material;

struct HitRecord {
    Point3 p;
    Vec3 normal;
    Vec3 tangent;
    double t = 0;
    double u = 0;
    double v = 0;
    bool front_face = true;
    bool has_tangent = false;
    Material* material = nullptr;

    void set_face_normal(const Ray& r, const Vec3& outward_normal) {
        front_face = dot(r.direction, outward_normal) < 0;
        normal = front_face ? outward_normal : -outward_normal;
    }
};

inline Vec3 orthonormal_tangent(const Vec3& normal) {
    if (normal.length_squared() < 1e-12) return Vec3(1, 0, 0);
    Vec3 axis = std::fabs(normal.x) > 0.9 ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    Vec3 tangent = cross(axis, normal);
    if (tangent.length_squared() < 1e-12) {
        axis = Vec3(0, 0, 1);
        tangent = cross(axis, normal);
    }
    if (tangent.length_squared() < 1e-12) return Vec3(1, 0, 0);
    return tangent.normalized();
}

inline Vec3 safe_tangent_from_candidate(const Vec3& candidate, const Vec3& normal) {
    Vec3 tangent = candidate - dot(candidate, normal) * normal;
    if (tangent.length_squared() < 1e-12) return orthonormal_tangent(normal);
    return tangent.normalized();
}

class Hittable {
public:
    virtual ~Hittable() = default;
    virtual bool hit(const Ray& r, double t_min, double t_max,
                     HitRecord& rec) const = 0;
    virtual bool bounding_box(AABB& output_box) const = 0;
};

#endif
