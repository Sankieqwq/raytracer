// Module B: geometry -- hittable abstraction + hit record
#ifndef RT_HITTABLE_H
#define RT_HITTABLE_H

#include "raytracer/math/ray.h"
#include "raytracer/geometry/aabb.h"

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

class Hittable {
public:
    virtual ~Hittable() = default;
    virtual bool hit(const Ray& r, double t_min, double t_max,
                     HitRecord& rec) const = 0;
    virtual bool bounding_box(AABB& output_box) const = 0;
};

#endif
