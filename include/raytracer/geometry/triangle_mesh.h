// Module B: geometry -- triangle mesh (SoA layout for future GPU upload)
#ifndef RT_TRIANGLE_MESH_H
#define RT_TRIANGLE_MESH_H

#include "raytracer/geometry/hittable.h"
#include "raytracer/geometry/triangle.h"  // triangle_intersect
#include "raytracer/math/vec3.h"
#include <vector>

class TriangleMesh : public Hittable {
public:
    std::vector<Point3> vertices;
    std::vector<Vec3> normals;              // empty = no vertex normals
    std::vector<Vec2> uvs;                   // empty = no uvs
    std::vector<int> indices;               // size must be multiple of 3
    std::vector<Material*> material_per_tri;  // size == indices.size() / 3

    TriangleMesh() = default;

    // Brute-force traversal; BVH acceleration comes in Stage 10.
    bool hit(const Ray& r, double t_min, double t_max,
             HitRecord& rec) const override {
        bool hit_any = false;
        double closest = t_max;
        size_t tri_count = indices.size() / 3;
        for (size_t i = 0; i < tri_count; i++) {
            int i0 = indices[i*3 + 0];
            int i1 = indices[i*3 + 1];
            int i2 = indices[i*3 + 2];
            double t, b1, b2;
            if (!triangle_intersect(r, t_min, closest,
                                    vertices[i0], vertices[i1], vertices[i2],
                                    t, b1, b2)) continue;
            double b0 = 1.0 - b1 - b2;
            HitRecord tmp;
            tmp.t = t;
            tmp.p = r.at(t);
            Vec3 outward;
            if (!normals.empty()) {
                outward = (b0 * normals[i0] + b1 * normals[i1] + b2 * normals[i2]).normalized();
            } else {
                outward = cross(vertices[i1] - vertices[i0],
                                vertices[i2] - vertices[i0]).normalized();
            }
            tmp.set_face_normal(r, outward);
            if (!uvs.empty()) {
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
                    tmp.tangent = (inv * (dv2 * edge1 - dv1 * edge2)).normalized();
                } else {
                    tmp.tangent = cross(Vec3(0, 1, 0), outward).normalized();
                    if (tmp.tangent.length_squared() < 1e-10)
                        tmp.tangent = cross(Vec3(1, 0, 0), outward).normalized();
                }
                tmp.has_tangent = true;
            } else {
                tmp.u = b1;
                tmp.v = b2;
            }
            tmp.material = material_per_tri[i];
            rec = tmp;
            closest = t;
            hit_any = true;
        }
        return hit_any;
    }

    bool bounding_box(AABB& output_box) const override {
        if (!bbox_computed_) compute_bbox();
        output_box = bbox_;
        return true;
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
    mutable AABB bbox_;
    mutable bool bbox_computed_ = false;
};

#endif
