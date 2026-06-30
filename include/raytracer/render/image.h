// Module D: image -- PPM file writer with ACES filmic tonemapping
#ifndef RT_IMAGE_H
#define RT_IMAGE_H

#include "raytracer/math/vec3.h"
#include <algorithm>
#include <fstream>
#include <vector>
#include <string>

// Filmic tonemapping (Uncharted 2 / Hable 2010)
inline Color filmic_tonemap(Color x) {
    double A = 0.22, B = 0.30, C = 0.10, D = 0.20, E = 0.01, F = 0.30;
    auto f = [&](double v) {
        return ((v * (A * v + C * B) + D * E) / (v * (A * v + B) + D * F)) - E / F;
    };
    return Color(f(x.x), f(x.y), f(x.z));
}

inline void write_ppm(const std::string& path, int w, int h,
                      const std::vector<Color>& pixels,
                      int samples_per_pixel, double exposure = 1.0) {
    std::ofstream out(path);
    out << "P3\n" << w << ' ' << h << "\n255\n";

    double scale = 1.0 / samples_per_pixel;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            const Color& raw = pixels[j * w + i];
            Color c = filmic_tonemap(raw * scale * exposure);
            c = gamma2(c);
            auto clamp01 = [](double v) {
                return v < 0 ? 0.0 : (v > 0.999 ? 0.999 : v);
            };
            int r = int(256 * clamp01(c.x));
            int g = int(256 * clamp01(c.y));
            int b = int(256 * clamp01(c.z));
            out << r << ' ' << g << ' ' << b << '\n';
        }
    }
}

#endif
