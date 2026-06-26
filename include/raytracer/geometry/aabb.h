#ifndef RT_AABB_H
#define RT_AABB_H

#include "raytracer/math/ray.h"
#include <algorithm>

class AABB {
public:
    Point3 minimum;
    Point3 maximum;

    AABB() : minimum(), maximum() {}
    AABB(const Point3& minimum, const Point3& maximum)
        : minimum(minimum), maximum(maximum) {}

    bool hit(const Ray& r, double t_min, double t_max) const {
        for (int axis = 0; axis < 3; axis++) {
            double origin = axis_value(r.origin, axis);
            double direction = axis_value(r.direction, axis);
            double inv_d = 1.0 / direction;
            double t0 = (axis_value(minimum, axis) - origin) * inv_d;
            double t1 = (axis_value(maximum, axis) - origin) * inv_d;

            if (inv_d < 0.0) std::swap(t0, t1);

            t_min = t0 > t_min ? t0 : t_min;
            t_max = t1 < t_max ? t1 : t_max;
            if (t_max <= t_min) return false;
        }
        return true;
    }

    int longest_axis() const {
        Vec3 extent = maximum - minimum;
        if (extent.x >= extent.y && extent.x >= extent.z) return 0;
        if (extent.y >= extent.z) return 1;
        return 2;
    }

    static AABB surrounding_box(const AABB& a, const AABB& b) {
        Point3 small(
            std::min(a.minimum.x, b.minimum.x),
            std::min(a.minimum.y, b.minimum.y),
            std::min(a.minimum.z, b.minimum.z));
        Point3 big(
            std::max(a.maximum.x, b.maximum.x),
            std::max(a.maximum.y, b.maximum.y),
            std::max(a.maximum.z, b.maximum.z));
        return AABB(small, big);
    }

private:
    static double axis_value(const Vec3& v, int axis) {
        if (axis == 0) return v.x;
        if (axis == 1) return v.y;
        return v.z;
    }
};

#endif
