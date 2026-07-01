// Module B: geometry -- linear BVH (flat nodes, stack-based traversal)
#ifndef RT_BVH_H
#define RT_BVH_H

#include "raytracer/geometry/hittable.h"
#include <algorithm>
#include <limits>
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

    static double axis_val(const Vec3& v, int axis) {
        return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
    }

    void make_leaf(int node_idx, int start, int count) {
        nodes_[node_idx].is_leaf = true;
        nodes_[node_idx].prim_start = static_cast<int>(prim_indices_.size());
        nodes_[node_idx].prim_count = count;
        for (int i = 0; i < count; i++) {
            prim_indices_.push_back(static_cast<uint32_t>(start + i));
        }
    }

    // Midpoint-split fallback used when SAH cannot find a valid partition
    // (e.g. all centroids coincide, or the best split degenerates to one side).
    void midpoint_split_and_recurse(int node_idx, int start, int end, int axis) {
        int span = end - start;
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

    // Recursive SAH-based build. Returns node index. Sorts primitives_ in
    // [start, end) in place.
    int build(int start, int end) {
        AABB global_box;
        compute_range_box(start, end, global_box);

        int node_idx = static_cast<int>(nodes_.size());
        nodes_.push_back(LinearBVHNode{});
        nodes_[node_idx].box = global_box;

        int span = end - start;
        if (span == 1) {
            make_leaf(node_idx, start, 1);
            return node_idx;
        }

        // Compute centroid bounds for SAH binning.
        Point3 c_min( std::numeric_limits<double>::infinity(),
                      std::numeric_limits<double>::infinity(),
                      std::numeric_limits<double>::infinity());
        Point3 c_max(-std::numeric_limits<double>::infinity(),
                    -std::numeric_limits<double>::infinity(),
                    -std::numeric_limits<double>::infinity());
        for (int i = start; i < end; i++) {
            AABB tmp;
            primitives_[i]->bounding_box(tmp);
            Point3 c = tmp.center();
            c_min = Point3(std::min(c_min.x, c.x), std::min(c_min.y, c.y), std::min(c_min.z, c.z));
            c_max = Point3(std::max(c_max.x, c.x), std::max(c_max.y, c.y), std::max(c_max.z, c.z));
        }

        // Bin-based SAH: for each axis, bucket primitives by centroid, then
        // evaluate the SAH cost at every bucket boundary and keep the best.
        constexpr int NUM_BUCKETS = 12;
        double best_cost = std::numeric_limits<double>::infinity();
        int best_axis = -1, best_bucket = -1;

        struct Bucket { int count = 0; AABB box; bool empty = true; };

        for (int axis = 0; axis < 3; axis++) {
            double lo = axis_val(c_min, axis);
            double hi = axis_val(c_max, axis);
            double ext = hi - lo;
            if (ext <= 1e-12) continue;  // degenerate axis, centroids coincide

            Bucket buckets[NUM_BUCKETS];
            for (int i = start; i < end; i++) {
                AABB tmp;
                primitives_[i]->bounding_box(tmp);
                double c = axis_val(tmp.center(), axis);
                int b = static_cast<int>((c - lo) / ext * NUM_BUCKETS);
                b = std::clamp(b, 0, NUM_BUCKETS - 1);
                buckets[b].count++;
                buckets[b].box = buckets[b].empty
                    ? tmp
                    : AABB::surrounding_box(buckets[b].box, tmp);
                buckets[b].empty = false;
            }

            double parent_sa = global_box.surface_area();
            if (parent_sa <= 0.0) parent_sa = 1.0;

            for (int split = 1; split < NUM_BUCKETS; split++) {
                int left_count = 0, right_count = 0;
                AABB left_box, right_box;
                bool left_empty = true, right_empty = true;
                for (int i = 0; i < split; i++) {
                    if (buckets[i].empty) continue;
                    left_count += buckets[i].count;
                    left_box = left_empty ? buckets[i].box
                                          : AABB::surrounding_box(left_box, buckets[i].box);
                    left_empty = false;
                }
                for (int i = split; i < NUM_BUCKETS; i++) {
                    if (buckets[i].empty) continue;
                    right_count += buckets[i].count;
                    right_box = right_empty ? buckets[i].box
                                            : AABB::surrounding_box(right_box, buckets[i].box);
                    right_empty = false;
                }
                if (left_count == 0 || right_count == 0) continue;

                // SAH: C = (SA_left * N_left + SA_right * N_right) / SA_parent.
                // Traversal cost is omitted for simplicity (constant factor).
                double cost = (left_box.surface_area() * left_count +
                               right_box.surface_area() * right_count) / parent_sa;
                if (cost < best_cost) {
                    best_cost = cost;
                    best_axis = axis;
                    best_bucket = split;
                }
            }
        }

        // No axis produced a valid SAH split -> fall back to midpoint split.
        if (best_axis < 0) {
            midpoint_split_and_recurse(node_idx, start, end, global_box.longest_axis());
            return node_idx;
        }

        // Partition primitives by the chosen bucket boundary on best_axis.
        double lo = axis_val(c_min, best_axis);
        double hi = axis_val(c_max, best_axis);
        double ext = hi - lo;
        auto bucket_of = [&](Hittable* p) {
            AABB tmp;
            p->bounding_box(tmp);
            double c = axis_val(tmp.center(), best_axis);
            int b = static_cast<int>((c - lo) / ext * NUM_BUCKETS);
            return std::clamp(b, 0, NUM_BUCKETS - 1);
        };
        int mid = static_cast<int>(std::partition(primitives_.begin() + start,
                                                   primitives_.begin() + end,
            [&](Hittable* p) { return bucket_of(p) < best_bucket; }
        ) - primitives_.begin());

        // Guard against degenerate partitions (all primitives land on one side
        // despite the SAH split, which can happen with shared centroids).
        if (mid <= start || mid >= end) {
            midpoint_split_and_recurse(node_idx, start, end, global_box.longest_axis());
            return node_idx;
        }

        int left = build(start, mid);
        int right = build(mid, end);
        nodes_[node_idx].left = left;
        nodes_[node_idx].right = right;
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
