// Module D: image -- PPM file writer (real, used from stage 0)
#ifndef RT_IMAGE_H
#define RT_IMAGE_H

#include "raytracer/math/vec3.h"
#include <fstream>
#include <vector>
#include <string>

inline void write_ppm(const std::string& path, int w, int h,
                      const std::vector<Color>& pixels,
                      int samples_per_pixel) {
    std::ofstream out(path);
    out << "P3\n" << w << ' ' << h << "\n255\n";

    double scale = 1.0 / samples_per_pixel;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            const Color& raw = pixels[j * w + i];
            Color c = gamma2(raw * scale);
            auto clamp = [](double v) {
                return v < 0 ? 0 : (v > 0.999 ? 0.999 : v);
            };
            int r = int(256 * clamp(c.x));
            int g = int(256 * clamp(c.y));
            int b = int(256 * clamp(c.z));
            out << r << ' ' << g << ' ' << b << '\n';
        }
    }
}

#endif
