#ifndef RT_TRIANGLE_H
#define RT_TRIANGLE_H

#include "raytracer/geometry/hittable.h"
#include "raytracer/math/vec3.h"
#include <algorithm>
#include <cmath>

// Möller–Trumbore ray-triangle intersection.
// On hit, writes t and barycentric coords (b1, b2) to output params.
// Barycentric weights: b0 = 1 - b1 - b2 (at v0), b1 (at v1), b2 (at v2).
// Returns false for rays parallel to triangle or hits outside [t_min, t_max].
inline bool triangle_intersect(const Ray& r, double t_min, double t_max,
                               const Point3& v0, const Point3& v1, const Point3& v2,
                               double& t, double& b1, double& b2) {
    const double eps = 1e-8;
    Vec3 edge1 = v1 - v0;
    Vec3 edge2 = v2 - v0;
    Vec3 pvec = cross(r.direction, edge2);
    double det = dot(edge1, pvec);

    if (std::fabs(det) < eps) return false;
    double inv_det = 1.0 / det;

    Vec3 tvec = r.origin - v0;
    b1 = dot(tvec, pvec) * inv_det;
    if (b1 < 0.0 || b1 > 1.0) return false;

    Vec3 qvec = cross(tvec, edge1);
    b2 = dot(r.direction, qvec) * inv_det;
    if (b2 < 0.0 || (b1 + b2) > 1.0) return false;

    t = dot(edge2, qvec) * inv_det;
    if (t < t_min || t > t_max) return false;
    return true;
}

class Triangle : public Hittable {
public:
    Point3 v0, v1, v2;
    Vec3 n0, n1, n2;
    Vec2 uv0, uv1, uv2;
    bool has_vertex_normals = false;
    bool has_uvs = false;
    Material* material;

    Triangle() : v0(), v1(), v2(), n0(), n1(), n2(), uv0(), uv1(), uv2(), material(nullptr) {}
    Triangle(const Point3& a, const Point3& b, const Point3& c, Material* material = nullptr)
        : v0(a), v1(b), v2(c), n0(), n1(), n2(), uv0(), uv1(), uv2(), material(material) {}
    Triangle(const Point3& a, const Point3& b, const Point3& c,
             const Vec3& na, const Vec3& nb, const Vec3& nc,
             Material* material = nullptr)
        : v0(a), v1(b), v2(c), n0(na), n1(nb), n2(nc),
          uv0(), uv1(), uv2(), has_vertex_normals(true), material(material) {}
    Triangle(const Point3& a, const Point3& b, const Point3& c,
             const Vec3& na, const Vec3& nb, const Vec3& nc,
             const Vec2& t0, const Vec2& t1, const Vec2& t2,
             Material* material = nullptr)
        : v0(a), v1(b), v2(c), n0(na), n1(nb), n2(nc),
          uv0(t0), uv1(t1), uv2(t2),
          has_vertex_normals(true), has_uvs(true), material(material) {}

    bool hit(const Ray& r, double t_min, double t_max,
             HitRecord& rec) const override {
        double t, b1, b2;
        if (!triangle_intersect(r, t_min, t_max, v0, v1, v2, t, b1, b2))
            return false;

        double b0 = 1.0 - b1 - b2;
        rec.t = t;
        rec.p = r.at(t);
        Vec3 outward_normal;
        if (has_vertex_normals) {
            outward_normal = (b0 * n0 + b1 * n1 + b2 * n2).normalized();
        } else {
            outward_normal = cross(v1 - v0, v2 - v0).normalized();
        }
        rec.set_face_normal(r, outward_normal);
        if (has_uvs) {
            Vec2 uv = b0 * uv0 + b1 * uv1 + b2 * uv2;
            rec.u = uv.x;
            rec.v = uv.y;
        } else {
            rec.u = b1;
            rec.v = b2;
        }
        rec.material = material;
        return true;
    }

    bool bounding_box(AABB& output_box) const override {
        const double pad = 1e-4;
        Point3 small(
            std::min({v0.x, v1.x, v2.x}) - pad,
            std::min({v0.y, v1.y, v2.y}) - pad,
            std::min({v0.z, v1.z, v2.z}) - pad);
        Point3 big(
            std::max({v0.x, v1.x, v2.x}) + pad,
            std::max({v0.y, v1.y, v2.y}) + pad,
            std::max({v0.z, v1.z, v2.z}) + pad);
        output_box = AABB(small, big);
        return true;
    }
};

#endif
