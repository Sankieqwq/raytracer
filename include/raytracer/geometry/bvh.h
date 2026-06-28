// Module B: geometry -- linear BVH (flat nodes, stack-based traversal)
#ifndef RT_BVH_H
#define RT_BVH_H

#include "raytracer/geometry/hittable.h"
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <cstdint>

// Flat BVH node. POD for easy GPU upload (Stage 12/14).
struct LinearBVHNode {
    AABB box;
    int left = -1;          // internal: left child index; leaf: -1
    int right = -1;         // internal: right child index; leaf: -1
    int prim_start = 0;     // leaf: start index into prim_indices_
    int prim_count = 0;     // leaf: primitive count; internal: 0
    bool is_leaf = false;
};

class LinearBVH : public Hittable {
public:
    explicit LinearBVH(std::vector<Hittable*> objects) {
        if (objects.empty()) {
            throw std::runtime_error("LinearBVH requires at least one object");
        }
        primitives_ = std::move(objects);
        nodes_.reserve(2 * primitives_.size() - 1);
        build(0, static_cast<int>(primitives_.size()));
    }

    bool hit(const Ray& r, double t_min, double t_max,
             HitRecord& rec) const override {
        if (nodes_.empty()) return false;
        constexpr int MAX_DEPTH = 64;
        int stack[MAX_DEPTH];
        int sp = 0;
        stack[sp++] = 0;
        bool hit_any = false;
        double closest = t_max;
        while (sp > 0) {
            int idx = stack[--sp];
            const LinearBVHNode& node = nodes_[idx];
            if (!node.box.hit(r, t_min, closest)) continue;
            if (node.is_leaf) {
                for (int i = 0; i < node.prim_count; i++) {
                    uint32_t pi = prim_indices_[node.prim_start + i];
                    HitRecord tmp;
                    if (primitives_[pi]->hit(r, t_min, closest, tmp)) {
                        rec = tmp;
                        closest = tmp.t;
                        hit_any = true;
                    }
                }
            } else {
                if (sp + 2 > MAX_DEPTH) break;  // stack overflow guard
                stack[sp++] = node.left;
                stack[sp++] = node.right;
            }
        }
        return hit_any;
    }

    bool bounding_box(AABB& output_box) const override {
        if (nodes_.empty()) return false;
        output_box = nodes_[0].box;
        return true;
    }

private:
    std::vector<LinearBVHNode> nodes_;
    std::vector<Hittable*> primitives_;
    std::vector<uint32_t> prim_indices_;

    // Recursive build. Returns node index. Sorts primitives_ in [start, end) in place.
    int build(int start, int end) {
        AABB global_box;
        compute_range_box(start, end, global_box);

        int node_idx = static_cast<int>(nodes_.size());
        nodes_.push_back(LinearBVHNode{});
        nodes_[node_idx].box = global_box;

        int span = end - start;
        int axis = global_box.longest_axis();

        if (span == 1) {
            nodes_[node_idx].is_leaf = true;
            nodes_[node_idx].prim_start = static_cast<int>(prim_indices_.size());
            nodes_[node_idx].prim_count = 1;
            prim_indices_.push_back(static_cast<uint32_t>(start));
        } else {
            std::sort(primitives_.begin() + start, primitives_.begin() + end,
                [axis](const Hittable* a, const Hittable* b) {
                    AABB ba, bb;
                    a->bounding_box(ba);
                    b->bounding_box(bb);
                    if (axis == 0) return ba.minimum.x < bb.minimum.x;
                    if (axis == 1) return ba.minimum.y < bb.minimum.y;
                    return ba.minimum.z < bb.minimum.z;
                });
            int mid = start + span / 2;
            int left = build(start, mid);
            int right = build(mid, end);
            nodes_[node_idx].left = left;
            nodes_[node_idx].right = right;
        }
        return node_idx;
    }

    void compute_range_box(int start, int end, AABB& out) {
        bool first = true;
        AABB tmp;
        for (int i = start; i < end; i++) {
            primitives_[i]->bounding_box(tmp);
            out = first ? tmp : AABB::surrounding_box(out, tmp);
            first = false;
        }
    }
};

#endif
