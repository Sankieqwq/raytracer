// Module A: math foundation -- constants & random utilities
#ifndef RT_UTIL_H
#define RT_UTIL_H

#include <limits>
#include <random>

inline constexpr double infinity = std::numeric_limits<double>::infinity();
inline constexpr double pi = 3.1415926535897932385;

inline double degrees_to_radians(double degrees) {
    return degrees * pi / 180.0;
}

inline double random_double() {
    thread_local std::uniform_real_distribution<double> dist(0.0, 1.0);
    thread_local std::mt19937 gen(std::random_device{}());
    return dist(gen);
}

inline double random_double(double min, double max) {
    return min + (max - min) * random_double();
}

#endif
