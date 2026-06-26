#ifndef RT_BVH_H
#define RT_BVH_H

#include "raytracer/geometry/hittable.h"
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

class BVHNode : public Hittable {
public:
    explicit BVHNode(std::vector<Hittable*> objects) {
        if (objects.empty()) {
            throw std::runtime_error("BVH requires at least one object");
        }
        build(objects, 0, objects.size());
    }

    bool hit(const Ray& r, double t_min, double t_max,
             HitRecord& rec) const override {
        if (!box_.hit(r, t_min, t_max)) return false;

        HitRecord left_rec;
        bool hit_left = left_child()->hit(r, t_min, t_max, left_rec);

        HitRecord right_rec;
        double right_t_max = hit_left ? left_rec.t : t_max;
        bool hit_right = right_child()->hit(r, t_min, right_t_max, right_rec);

        if (hit_right) {
            rec = right_rec;
            return true;
        }
        if (hit_left) {
            rec = left_rec;
            return true;
        }
        return false;
    }

    bool bounding_box(AABB& output_box) const override {
        output_box = box_;
        return true;
    }

private:
    const Hittable* left_ = nullptr;
    const Hittable* right_ = nullptr;
    std::unique_ptr<BVHNode> left_node_;
    std::unique_ptr<BVHNode> right_node_;
    AABB box_;

    void build(std::vector<Hittable*>& objects, size_t start, size_t end) {
        AABB global_box;
        if (!compute_range_box(objects, start, end, global_box)) {
            throw std::runtime_error("BVH object missing bounding box");
        }

        size_t span = end - start;
        int axis = global_box.longest_axis();
        auto comparator = [axis](const Hittable* a, const Hittable* b) {
            AABB box_a, box_b;
            a->bounding_box(box_a);
            b->bounding_box(box_b);
            if (axis == 0) return box_a.minimum.x < box_b.minimum.x;
            if (axis == 1) return box_a.minimum.y < box_b.minimum.y;
            return box_a.minimum.z < box_b.minimum.z;
        };

        if (span == 1) {
            left_ = right_ = objects[start];
        } else if (span == 2) {
            if (comparator(objects[start], objects[start + 1])) {
                left_ = objects[start];
                right_ = objects[start + 1];
            } else {
                left_ = objects[start + 1];
                right_ = objects[start];
            }
        } else {
            std::sort(objects.begin() + start, objects.begin() + end, comparator);
            size_t mid = start + span / 2;
            left_node_ = std::make_unique<BVHNode>(slice(objects, start, mid));
            right_node_ = std::make_unique<BVHNode>(slice(objects, mid, end));
        }

        AABB box_left, box_right;
        if (!left_child()->bounding_box(box_left) || !right_child()->bounding_box(box_right)) {
            throw std::runtime_error("BVH child missing bounding box");
        }
        box_ = AABB::surrounding_box(box_left, box_right);
    }

    static std::vector<Hittable*> slice(const std::vector<Hittable*>& objects,
                                        size_t start, size_t end) {
        return std::vector<Hittable*>(objects.begin() + start, objects.begin() + end);
    }

    static bool compute_range_box(const std::vector<Hittable*>& objects,
                                  size_t start, size_t end,
                                  AABB& output_box) {
        bool first_box = true;
        AABB tmp_box;

        for (size_t i = start; i < end; i++) {
            if (!objects[i]->bounding_box(tmp_box)) return false;
            output_box = first_box ? tmp_box : AABB::surrounding_box(output_box, tmp_box);
            first_box = false;
        }

        return !first_box;
    }

    const Hittable* left_child() const {
        return left_node_ ? static_cast<const Hittable*>(left_node_.get()) : left_;
    }

    const Hittable* right_child() const {
        return right_node_ ? static_cast<const Hittable*>(right_node_.get()) : right_;
    }
};

#endif
