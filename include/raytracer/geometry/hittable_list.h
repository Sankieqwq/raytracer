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
};

#endif
