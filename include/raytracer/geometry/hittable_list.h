// Module B: geometry -- list of hittables (STUB: owner fills traversal)
#ifndef RT_HITTABLE_LIST_H
#define RT_HITTABLE_LIST_H

#include "raytracer/geometry/hittable.h"
#include <vector>

class HittableList : public Hittable {
public:
    std::vector<Hittable*> objects;

    HittableList() {}
    void add(Hittable* o) { objects.push_back(o); }
    void clear() { objects.clear(); }

    bool hit(const Ray& r, double t_min, double t_max,
             HitRecord& rec) const override {
        HitRecord tmp;
        bool hit_any = false;
        double closest = t_max;
        for (Hittable* o : objects) {
            if (o->hit(r, t_min, closest, tmp)) {
                hit_any = true;
                closest = tmp.t;
                rec = tmp;
            }
        }
        return hit_any;
    }

    bool bounding_box(AABB& output_box) const override {
        if (objects.empty()) return false;

        AABB tmp_box;
        bool first_box = true;
        for (Hittable* o : objects) {
            if (!o->bounding_box(tmp_box)) return false;
            output_box = first_box ? tmp_box : AABB::surrounding_box(output_box, tmp_box);
            first_box = false;
        }
        return true;
    }
};

#endif
