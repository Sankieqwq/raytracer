// Module A: math -- 4x4 row-major matrix for transforms
#ifndef RT_MAT4_H
#define RT_MAT4_H

#include "raytracer/math/vec3.h"
#include "raytracer/math/util.h"
#include <cmath>

class Mat4 {
public:
    double m[4][4];

    Mat4() {
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                m[i][j] = (i == j) ? 1.0 : 0.0;
    }

    static Mat4 identity() { return Mat4(); }

    static Mat4 translate(double tx, double ty, double tz) {
        Mat4 r;
        r.m[0][3] = tx; r.m[1][3] = ty; r.m[2][3] = tz;
        return r;
    }

    static Mat4 scale(double sx, double sy, double sz) {
        Mat4 r;
        r.m[0][0] = sx; r.m[1][1] = sy; r.m[2][2] = sz;
        return r;
    }

    static Mat4 rotate_x(double deg) {
        double c = std::cos(degrees_to_radians(deg));
        double s = std::sin(degrees_to_radians(deg));
        Mat4 r;
        r.m[1][1] = c;  r.m[1][2] = -s;
        r.m[2][1] = s;  r.m[2][2] = c;
        return r;
    }

    static Mat4 rotate_y(double deg) {
        double c = std::cos(degrees_to_radians(deg));
        double s = std::sin(degrees_to_radians(deg));
        Mat4 r;
        r.m[0][0] = c;  r.m[0][2] = s;
        r.m[2][0] = -s; r.m[2][2] = c;
        return r;
    }

    static Mat4 rotate_z(double deg) {
        double c = std::cos(degrees_to_radians(deg));
        double s = std::sin(degrees_to_radians(deg));
        Mat4 r;
        r.m[0][0] = c;  r.m[0][1] = -s;
        r.m[1][0] = s;  r.m[1][1] = c;
        return r;
    }

    // Axis-angle rotation (Rodrigues). axis is normalized internally.
    static Mat4 rotate_axis(const Vec3& axis, double deg) {
        Vec3 n = axis.normalized();
        double c = std::cos(degrees_to_radians(deg));
        double s = std::sin(degrees_to_radians(deg));
        double t = 1.0 - c;
        double x = n.x, y = n.y, z = n.z;
        Mat4 r;
        r.m[0][0] = t*x*x + c;     r.m[0][1] = t*x*y - s*z;  r.m[0][2] = t*x*z + s*y;
        r.m[1][0] = t*x*y + s*z;   r.m[1][1] = t*y*y + c;     r.m[1][2] = t*y*z - s*x;
        r.m[2][0] = t*x*z - s*y;   r.m[2][1] = t*y*z + s*x;   r.m[2][2] = t*z*z + c;
        return r;
    }

    // this * other  (apply other first, then this)
    Mat4 operator*(const Mat4& other) const {
        Mat4 r;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                double sum = 0;
                for (int k = 0; k < 4; k++)
                    sum += m[i][k] * other.m[k][j];
                r.m[i][j] = sum;
            }
        return r;
    }

    // Transform point (w=1)
    Vec3 transform_point(const Vec3& p) const {
        double x = m[0][0]*p.x + m[0][1]*p.y + m[0][2]*p.z + m[0][3];
        double y = m[1][0]*p.x + m[1][1]*p.y + m[1][2]*p.z + m[1][3];
        double z = m[2][0]*p.x + m[2][1]*p.y + m[2][2]*p.z + m[2][3];
        double w = m[3][0]*p.x + m[3][1]*p.y + m[3][2]*p.z + m[3][3];
        if (w != 0 && w != 1) { x /= w; y /= w; z /= w; }
        return Vec3(x, y, z);
    }

    // Transform direction (w=0, no translation)
    Vec3 transform_direction(const Vec3& d) const {
        double x = m[0][0]*d.x + m[0][1]*d.y + m[0][2]*d.z;
        double y = m[1][0]*d.x + m[1][1]*d.y + m[1][2]*d.z;
        double z = m[2][0]*d.x + m[2][1]*d.y + m[2][2]*d.z;
        return Vec3(x, y, z);
    }

    // Transform normal via inverse-transpose (3x3 adjugate, det scaling irrelevant)
    Vec3 transform_normal(const Vec3& n) const {
        double a = m[0][0], b = m[0][1], c = m[0][2];
        double d = m[1][0], e = m[1][1], f = m[1][2];
        double g = m[2][0], h = m[2][1], i = m[2][2];
        double A =  (e*i - f*h);
        double B = -(d*i - f*g);
        double C =  (d*h - e*g);
        double D = -(b*i - c*h);
        double E =  (a*i - c*g);
        double F = -(a*h - b*g);
        double G =  (b*f - c*e);
        double H = -(a*f - c*d);
        double I =  (a*e - b*d);
        return Vec3(A*n.x + B*n.y + C*n.z,
                    D*n.x + E*n.y + F*n.z,
                    G*n.x + H*n.y + I*n.z);
    }
};

#endif
