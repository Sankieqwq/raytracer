// Module D: image -- image output, tone mapping, and display conversion
#ifndef RT_IMAGE_H
#define RT_IMAGE_H

#include "raytracer/math/vec3.h"
#include "stb_image_write.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

enum class ToneMapMode { None, Reinhard, ACES };
enum class ImageOutputFormat { PPM, PNG };

struct ImageOutputOptions {
    double exposure = 1.0;
    ToneMapMode tone_map = ToneMapMode::ACES;
};

inline std::string image_output_extension(std::string path) {
    std::string ext;
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos) ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

inline ImageOutputFormat output_format_for_path(const std::string& path) {
    return image_output_extension(path) == ".png" ? ImageOutputFormat::PNG : ImageOutputFormat::PPM;
}

inline ToneMapMode parse_tone_map_mode(const std::string& value) {
    if (value == "none") return ToneMapMode::None;
    if (value == "reinhard") return ToneMapMode::Reinhard;
    if (value == "aces") return ToneMapMode::ACES;
    throw std::runtime_error("Unknown tone map mode: " + value);
}

inline Color aces_tonemap(const Color& c) {
    auto aces = [](double x) {
        double a = 2.51;
        double b = 0.03;
        double cc = 2.43;
        double d = 0.59;
        double e = 0.14;
        return std::clamp((x * (a * x + b)) / (x * (cc * x + d) + e), 0.0, 1.0);
    };
    return Color(aces(c.x), aces(c.y), aces(c.z));
}

inline Color apply_tone_map(const Color& c, ToneMapMode mode) {
    if (mode == ToneMapMode::None) return c;
    if (mode == ToneMapMode::Reinhard) {
        return Color(c.x / (1.0 + c.x), c.y / (1.0 + c.y), c.z / (1.0 + c.z));
    }
    return aces_tonemap(c);
}

inline Color to_display_color(const Color& raw, int samples_per_pixel,
                              const ImageOutputOptions& options) {
    double scale = 1.0 / std::max(1, samples_per_pixel);
    Color c = raw * scale * options.exposure;
    c = apply_tone_map(c, options.tone_map);
    c = Color(std::max(0.0, c.x), std::max(0.0, c.y), std::max(0.0, c.z));
    return gamma2(c);
}

inline unsigned char display_byte(double v) {
    double clamped = std::clamp(v, 0.0, 0.999);
    return static_cast<unsigned char>(256 * clamped);
}

inline void write_ppm(const std::string& path, int w, int h,
                      const std::vector<Color>& pixels,
                      int samples_per_pixel,
                      const ImageOutputOptions& options = ImageOutputOptions()) {
    std::ofstream out(path);
    out << "P3\n" << w << ' ' << h << "\n255\n";
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            Color c = to_display_color(pixels[j * w + i], samples_per_pixel, options);
            out << int(display_byte(c.x)) << ' '
                << int(display_byte(c.y)) << ' '
                << int(display_byte(c.z)) << '\n';
        }
    }
}

inline void write_png(const std::string& path, int w, int h,
                      const std::vector<Color>& pixels,
                      int samples_per_pixel,
                      const ImageOutputOptions& options = ImageOutputOptions()) {
    std::vector<unsigned char> bytes(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            Color c = to_display_color(pixels[j * w + i], samples_per_pixel, options);
            size_t idx = (static_cast<size_t>(j) * static_cast<size_t>(w) + static_cast<size_t>(i)) * 3;
            bytes[idx + 0] = display_byte(c.x);
            bytes[idx + 1] = display_byte(c.y);
            bytes[idx + 2] = display_byte(c.z);
        }
    }
    if (!stbi_write_png(path.c_str(), w, h, 3, bytes.data(), w * 3)) {
        throw std::runtime_error("Failed to write PNG: " + path);
    }
}

inline void write_image(const std::string& path, int w, int h,
                        const std::vector<Color>& pixels,
                        int samples_per_pixel,
                        const ImageOutputOptions& options = ImageOutputOptions()) {
    if (output_format_for_path(path) == ImageOutputFormat::PNG) {
        write_png(path, w, h, pixels, samples_per_pixel, options);
    } else {
        write_ppm(path, w, h, pixels, samples_per_pixel, options);
    }
}

#endif
