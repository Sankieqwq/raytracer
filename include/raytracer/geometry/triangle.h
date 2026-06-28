#ifndef RT_TRIANGLE_H
#define RT_TRIANGLE_H

#include "raytracer/geometry/hittable.h"
#include "raytracer/math/vec2.h"
#include <algorithm>
#include <cmath>

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
        : v0(a), v1(b), v2(c), n0(na), n1(nb), n2(nc), uv0(), uv1(), uv2(),
          has_vertex_normals(true), material(material) {}
    Triangle(const Point3& a, const Point3& b, const Point3& c,
             const Vec3& na, const Vec3& nb, const Vec3& nc,
             const Vec2& ta, const Vec2& tb, const Vec2& tc,
             bool has_normals, bool has_texcoords,
             Material* material = nullptr)
        : v0(a), v1(b), v2(c), n0(na), n1(nb), n2(nc), uv0(ta), uv1(tb), uv2(tc),
          has_vertex_normals(has_normals), has_uvs(has_texcoords), material(material) {}

    bool hit(const Ray& r, double t_min, double t_max,
             HitRecord& rec) const override {
        const double eps = 1e-8;
        Vec3 edge1 = v1 - v0;
        Vec3 edge2 = v2 - v0;
        Vec3 pvec = cross(r.direction, edge2);
        double det = dot(edge1, pvec);

        if (std::fabs(det) < eps) return false;
        double inv_det = 1.0 / det;

        Vec3 tvec = r.origin - v0;
        double bary_u = dot(tvec, pvec) * inv_det;
        if (bary_u < 0.0 || bary_u > 1.0) return false;

        Vec3 qvec = cross(tvec, edge1);
        double bary_v = dot(r.direction, qvec) * inv_det;
        if (bary_v < 0.0 || (bary_u + bary_v) > 1.0) return false;

        double t = dot(edge2, qvec) * inv_det;
        if (t < t_min || t > t_max) return false;

        rec.t = t;
        rec.p = r.at(t);
        Vec3 outward_normal = cross(edge1, edge2).normalized();
        double w = 1.0 - bary_u - bary_v;
        if (has_vertex_normals) {
            outward_normal = (w * n0 + bary_u * n1 + bary_v * n2).normalized();
        }
        if (has_uvs) {
            Vec2 uv = w * uv0 + bary_u * uv1 + bary_v * uv2;
            rec.u = uv.x;
            rec.v = uv.y;
        }
        rec.set_face_normal(r, outward_normal);
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
