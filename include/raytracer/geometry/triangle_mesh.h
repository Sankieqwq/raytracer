// Module B: geometry -- triangle mesh (SoA layout for future GPU upload)
#ifndef RT_TRIANGLE_MESH_H
#define RT_TRIANGLE_MESH_H

#include "raytracer/geometry/hittable.h"
#include "raytracer/geometry/triangle.h"  // triangle_intersect
#include "raytracer/math/vec3.h"
#include <algorithm>
#include <limits>
#include <vector>

class TriangleMesh : public Hittable {
public:
    std::vector<Point3> vertices;
    std::vector<Vec3> normals;              // empty = no vertex normals
    std::vector<Vec2> uvs;                   // empty = no uvs
    std::vector<int> indices;               // size must be multiple of 3
    std::vector<Material*> material_per_tri;  // size == indices.size() / 3

    TriangleMesh() = default;

    bool hit(const Ray& r, double t_min, double t_max,
             HitRecord& rec) const override {
        ensure_acceleration();
        if (bvh_nodes_.empty()) return false;

        bool hit_any = false;
        double closest = t_max;

        constexpr int MAX_STACK = 64;
        int stack[MAX_STACK];
        int sp = 0;
        stack[sp++] = 0;

        while (sp > 0) {
            int node_idx = stack[--sp];
            const TriangleMeshBVHNode& node = bvh_nodes_[node_idx];
            if (!node.box.hit(r, t_min, closest)) continue;

            if (node.is_leaf) {
                for (int i = 0; i < node.tri_count; i++) {
                    int tri_idx = bvh_tri_indices_[node.tri_start + i];
                    HitRecord tmp;
                    if (hit_triangle(tri_idx, r, t_min, closest, tmp)) {
                        rec = tmp;
                        closest = tmp.t;
                        hit_any = true;
                    }
                }
            } else {
                if (sp + 2 > MAX_STACK) {
                    // Balanced midpoint builds should not reach this, but keep
                    // correctness if the stack budget is ever exceeded.
                    for (int tri_idx : bvh_tri_indices_) {
                        HitRecord tmp;
                        if (hit_triangle(tri_idx, r, t_min, closest, tmp)) {
                            rec = tmp;
                            closest = tmp.t;
                            hit_any = true;
                        }
                    }
                    return hit_any;
                }
                stack[sp++] = node.left;
                stack[sp++] = node.right;
            }
        }
        return hit_any;
    }

    size_t acceleration_node_count() const {
        ensure_acceleration();
        return bvh_nodes_.size();
    }

    size_t triangle_count() const {
        return indices.size() / 3;
    }

    bool hit_triangle(int tri_idx, const Ray& r, double t_min, double t_max,
                      HitRecord& rec) const {
        if (tri_idx < 0 || static_cast<size_t>(tri_idx * 3 + 2) >= indices.size()) {
            return false;
        }
        int i0 = indices[tri_idx*3 + 0];
        int i1 = indices[tri_idx*3 + 1];
        int i2 = indices[tri_idx*3 + 2];
        if (!valid_vertex_index(i0) || !valid_vertex_index(i1) || !valid_vertex_index(i2)) {
            return false;
        }

        double t, b1, b2;
        if (!triangle_intersect(r, t_min, t_max,
                                vertices[i0], vertices[i1], vertices[i2],
                                t, b1, b2)) {
            return false;
        }
        double b0 = 1.0 - b1 - b2;
        HitRecord tmp;
        tmp.t = t;
        tmp.p = r.at(t);
        Vec3 outward;
        if (normals.size() == vertices.size()) {
            outward = b0 * normals[i0] + b1 * normals[i1] + b2 * normals[i2];
            if (outward.length_squared() < 1e-12) {
                outward = cross(vertices[i1] - vertices[i0],
                                vertices[i2] - vertices[i0]);
            } else {
                outward = outward.normalized();
            }
        } else {
            outward = cross(vertices[i1] - vertices[i0],
                            vertices[i2] - vertices[i0]);
        }
        if (outward.length_squared() < 1e-12) return false;
        outward = outward.normalized();
        tmp.set_face_normal(r, outward);
        if (uvs.size() == vertices.size()) {
            Vec2 uv = b0 * uvs[i0] + b1 * uvs[i1] + b2 * uvs[i2];
            tmp.u = uv.x;
            tmp.v = uv.y;
            // Tangent from UV
            double du1 = uvs[i1].x - uvs[i0].x;
            double dv1 = uvs[i1].y - uvs[i0].y;
            double du2 = uvs[i2].x - uvs[i0].x;
            double dv2 = uvs[i2].y - uvs[i0].y;
            double det = du1 * dv2 - du2 * dv1;
            Vec3 edge1 = vertices[i1] - vertices[i0];
            Vec3 edge2 = vertices[i2] - vertices[i0];
            if (std::fabs(det) > 1e-10) {
                double inv = 1.0 / det;
                tmp.tangent = safe_tangent_from_candidate(inv * (dv2 * edge1 - dv1 * edge2), tmp.normal);
            } else {
                tmp.tangent = orthonormal_tangent(tmp.normal);
            }
            tmp.has_tangent = true;
        } else {
            tmp.u = b1;
            tmp.v = b2;
        }
        tmp.material = static_cast<size_t>(tri_idx) < material_per_tri.size()
            ? material_per_tri[tri_idx]
            : nullptr;
        rec = tmp;
        return true;
    }

    bool bounding_box(AABB& output_box) const override {
        if (!bbox_computed_) compute_bbox();
        output_box = bbox_;
        return true;
    }

    Point3 sample_point(double r1, double r2, Vec3* normal_out = nullptr) const override {
        ensure_area_cdf();
        if (tri_area_cdf_.empty()) return Point3();

        double target = r1 * tri_area_total_;
        auto it = std::lower_bound(tri_area_cdf_.begin(), tri_area_cdf_.end(), target);
        int tri_idx = static_cast<int>(std::distance(tri_area_cdf_.begin(), it));
        int tri_count = static_cast<int>(indices.size() / 3);
        if (tri_idx >= tri_count) tri_idx = tri_count - 1;

        int i0 = indices[tri_idx*3 + 0];
        int i1 = indices[tri_idx*3 + 1];
        int i2 = indices[tri_idx*3 + 2];

        double sq = std::sqrt(r2);
        double b1 = 1.0 - sq;
        double b2 = sq * (1.0 - r1);
        double b0 = sq * r1;

        if (normal_out) {
            Vec3 n = cross(vertices[i1] - vertices[i0], vertices[i2] - vertices[i0]);
            double len = n.length();
            *normal_out = len > 1e-12 ? n / len : Vec3(0, 1, 0);
        }
        return b0 * vertices[i0] + b1 * vertices[i1] + b2 * vertices[i2];
    }

    double area() const override {
        ensure_area_cdf();
        return tri_area_total_;
    }

    void compute_bbox() const {
        if (vertices.empty()) {
            bbox_ = AABB();
            bbox_computed_ = true;
            return;
        }
        Point3 mn = vertices[0], mx = vertices[0];
        for (const Point3& p : vertices) {
            if (p.x < mn.x) mn.x = p.x;
            if (p.y < mn.y) mn.y = p.y;
            if (p.z < mn.z) mn.z = p.z;
            if (p.x > mx.x) mx.x = p.x;
            if (p.y > mx.y) mx.y = p.y;
            if (p.z > mx.z) mx.z = p.z;
        }
        const double pad = 1e-4;
        bbox_ = AABB(mn - Vec3(pad, pad, pad), mx + Vec3(pad, pad, pad));
        bbox_computed_ = true;
    }

private:
    struct TriangleMeshBVHNode {
        AABB box;
        int left = -1;
        int right = -1;
        int tri_start = 0;
        int tri_count = 0;
        bool is_leaf = false;
    };

    mutable AABB bbox_;
    mutable bool bbox_computed_ = false;
    mutable std::vector<TriangleMeshBVHNode> bvh_nodes_;
    mutable std::vector<int> bvh_tri_indices_;
    mutable bool bvh_built_ = false;

    mutable std::vector<double> tri_area_cdf_;
    mutable double tri_area_total_ = 0;
    mutable bool area_cdf_built_ = false;

    bool valid_vertex_index(int idx) const {
        return idx >= 0 && static_cast<size_t>(idx) < vertices.size();
    }

    AABB triangle_bounds(int tri_idx) const {
        int i0 = indices[tri_idx*3 + 0];
        int i1 = indices[tri_idx*3 + 1];
        int i2 = indices[tri_idx*3 + 2];
        const double pad = 1e-4;
        Point3 mn(
            std::min({vertices[i0].x, vertices[i1].x, vertices[i2].x}) - pad,
            std::min({vertices[i0].y, vertices[i1].y, vertices[i2].y}) - pad,
            std::min({vertices[i0].z, vertices[i1].z, vertices[i2].z}) - pad);
        Point3 mx(
            std::max({vertices[i0].x, vertices[i1].x, vertices[i2].x}) + pad,
            std::max({vertices[i0].y, vertices[i1].y, vertices[i2].y}) + pad,
            std::max({vertices[i0].z, vertices[i1].z, vertices[i2].z}) + pad);
        return AABB(mn, mx);
    }

    Point3 triangle_centroid(int tri_idx) const {
        int i0 = indices[tri_idx*3 + 0];
        int i1 = indices[tri_idx*3 + 1];
        int i2 = indices[tri_idx*3 + 2];
        return (vertices[i0] + vertices[i1] + vertices[i2]) / 3.0;
    }

    static double axis_value(const Vec3& v, int axis) {
        if (axis == 0) return v.x;
        if (axis == 1) return v.y;
        return v.z;
    }

    void ensure_acceleration() const {
        if (bvh_built_) return;
        bvh_nodes_.clear();
        bvh_tri_indices_.clear();

        int tri_count = static_cast<int>(indices.size() / 3);
        bvh_tri_indices_.reserve(tri_count);
        for (int i = 0; i < tri_count; i++) {
            int i0 = indices[i*3 + 0];
            int i1 = indices[i*3 + 1];
            int i2 = indices[i*3 + 2];
            if (valid_vertex_index(i0) && valid_vertex_index(i1) && valid_vertex_index(i2)) {
                bvh_tri_indices_.push_back(i);
            }
        }

        if (!bvh_tri_indices_.empty()) {
            bvh_nodes_.reserve(2 * bvh_tri_indices_.size());
            build_bvh_range(0, static_cast<int>(bvh_tri_indices_.size()));
        }
        bvh_built_ = true;
    }

    double triangle_area(int tri_idx) const {
        int i0 = indices[tri_idx*3 + 0];
        int i1 = indices[tri_idx*3 + 1];
        int i2 = indices[tri_idx*3 + 2];
        return 0.5 * cross(vertices[i1] - vertices[i0],
                           vertices[i2] - vertices[i0]).length();
    }

    void ensure_area_cdf() const {
        if (area_cdf_built_) return;
        tri_area_cdf_.clear();
        tri_area_total_ = 0;
        int tri_count = static_cast<int>(indices.size() / 3);
        tri_area_cdf_.reserve(tri_count);
        for (int i = 0; i < tri_count; i++) {
            double a = triangle_area(i);
            if (a < 1e-20) a = 1e-20;
            tri_area_total_ += a;
            tri_area_cdf_.push_back(tri_area_total_);
        }
        area_cdf_built_ = true;
    }

    int build_bvh_range(int start, int end) const {
        AABB box = triangle_bounds(bvh_tri_indices_[start]);
        for (int i = start + 1; i < end; i++) {
            box = AABB::surrounding_box(box, triangle_bounds(bvh_tri_indices_[i]));
        }

        int node_idx = static_cast<int>(bvh_nodes_.size());
        bvh_nodes_.push_back(TriangleMeshBVHNode{});
        bvh_nodes_[node_idx].box = box;

        int span = end - start;
        constexpr int LEAF_SIZE = 4;
        if (span <= LEAF_SIZE) {
            bvh_nodes_[node_idx].is_leaf = true;
            bvh_nodes_[node_idx].tri_start = start;
            bvh_nodes_[node_idx].tri_count = span;
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
            Point3 c = triangle_centroid(bvh_tri_indices_[i]);
            c_min = Point3(std::min(c_min.x, c.x), std::min(c_min.y, c.y), std::min(c_min.z, c.z));
            c_max = Point3(std::max(c_max.x, c.x), std::max(c_max.y, c.y), std::max(c_max.z, c.z));
        }

        constexpr int NUM_BUCKETS = 12;
        double best_cost = std::numeric_limits<double>::infinity();
        int best_axis = -1, best_bucket = -1;

        struct Bucket { int count = 0; AABB box; bool empty = true; };

        for (int axis = 0; axis < 3; axis++) {
            double lo = axis_value(c_min, axis);
            double hi = axis_value(c_max, axis);
            double ext = hi - lo;
            if (ext <= 1e-12) continue;

            Bucket buckets[NUM_BUCKETS];
            for (int i = start; i < end; i++) {
                int ti = bvh_tri_indices_[i];
                double c = axis_value(triangle_centroid(ti), axis);
                int b = static_cast<int>((c - lo) / ext * NUM_BUCKETS);
                b = std::clamp(b, 0, NUM_BUCKETS - 1);
                AABB tb = triangle_bounds(ti);
                buckets[b].count++;
                buckets[b].box = buckets[b].empty
                    ? tb
                    : AABB::surrounding_box(buckets[b].box, tb);
                buckets[b].empty = false;
            }

            double parent_sa = box.surface_area();
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

                double cost = (left_box.surface_area() * left_count +
                               right_box.surface_area() * right_count) / parent_sa;
                if (cost < best_cost) {
                    best_cost = cost;
                    best_axis = axis;
                    best_bucket = split;
                }
            }
        }

        // No valid SAH split -> fall back to midpoint split on longest axis.
        if (best_axis < 0) {
            int axis = box.longest_axis();
            std::sort(bvh_tri_indices_.begin() + start, bvh_tri_indices_.begin() + end,
                      [&](int a, int b) {
                          return axis_value(triangle_centroid(a), axis) <
                                 axis_value(triangle_centroid(b), axis);
                      });
            int mid = start + span / 2;
            int left = build_bvh_range(start, mid);
            int right = build_bvh_range(mid, end);
            bvh_nodes_[node_idx].left = left;
            bvh_nodes_[node_idx].right = right;
            return node_idx;
        }

        // Partition triangle indices by the chosen bucket boundary on best_axis.
        double lo = axis_value(c_min, best_axis);
        double hi = axis_value(c_max, best_axis);
        double ext = hi - lo;
        auto bucket_of = [&](int ti) {
            double c = axis_value(triangle_centroid(ti), best_axis);
            int b = static_cast<int>((c - lo) / ext * NUM_BUCKETS);
            return std::clamp(b, 0, NUM_BUCKETS - 1);
        };
        int mid = static_cast<int>(std::partition(bvh_tri_indices_.begin() + start,
                                                   bvh_tri_indices_.begin() + end,
            [&](int ti) { return bucket_of(ti) < best_bucket; }
        ) - bvh_tri_indices_.begin());

        if (mid <= start || mid >= end) {
            int axis = box.longest_axis();
            std::sort(bvh_tri_indices_.begin() + start, bvh_tri_indices_.begin() + end,
                      [&](int a, int b) {
                          return axis_value(triangle_centroid(a), axis) <
                                 axis_value(triangle_centroid(b), axis);
                      });
            mid = start + span / 2;
        }

        int left = build_bvh_range(start, mid);
        int right = build_bvh_range(mid, end);
        bvh_nodes_[node_idx].left = left;
        bvh_nodes_[node_idx].right = right;
        return node_idx;
    }
};

#endif
