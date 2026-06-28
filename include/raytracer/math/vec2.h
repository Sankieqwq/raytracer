#ifndef RT_VEC2_H
#define RT_VEC2_H

class Vec2 {
public:
    double x, y;

    Vec2() : x(0), y(0) {}
    Vec2(double x, double y) : x(x), y(y) {}
};

inline Vec2 operator+(const Vec2& a, const Vec2& b) { return Vec2(a.x + b.x, a.y + b.y); }
inline Vec2 operator-(const Vec2& a, const Vec2& b) { return Vec2(a.x - b.x, a.y - b.y); }
inline Vec2 operator*(double t, const Vec2& a) { return Vec2(t * a.x, t * a.y); }
inline Vec2 operator*(const Vec2& a, double t) { return t * a; }

#endif
