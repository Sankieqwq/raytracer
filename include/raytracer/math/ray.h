// Module A: math foundation -- ray (origin + direction)
#ifndef RT_RAY_H
#define RT_RAY_H

#include "raytracer/math/vec3.h"

class Ray {
public:
    Point3 origin;
    Vec3 direction;
    // Medium the ray is currently traveling through, for Beer-Lambert volume
    // absorption.  Defaults to vacuum/air: attenuation_color = (1,1,1) and
    // attenuation_distance = infinity, so no absorption is applied.  When a
    // ray refracts into a Dielectric, the scattered ray carries that medium's
    // attenuation parameters; when it refracts back to air, they reset to the
    // vacuum defaults.  Reflections (TIR or Fresnel) keep the incoming medium.
    Color medium_color = Color(1, 1, 1);
    double medium_attenuation_distance = infinity;

    Ray() {}
    Ray(const Point3& origin, const Vec3& direction)
        : origin(origin), direction(direction) {}

    Point3 at(double t) const { return origin + t * direction; }
};

#endif
