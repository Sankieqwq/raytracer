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

inline unsigned int& random_seed_storage() {
    static unsigned int seed = std::random_device{}();
    return seed;
}

inline std::mt19937& random_generator() {
    thread_local std::mt19937 gen(random_seed_storage());
    return gen;
}

inline void set_random_seed(unsigned int seed) {
    random_seed_storage() = seed;
    random_generator().seed(seed);
}

inline double random_double() {
    thread_local std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(random_generator());
}

inline double random_double(double min, double max) {
    return min + (max - min) * random_double();
}

#endif
