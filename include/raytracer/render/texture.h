// Module D: render -- texture (POD tag dispatch, GPU-friendly)
#ifndef RT_TEXTURE_H
#define RT_TEXTURE_H

#include "raytracer/math/vec3.h"
#include <vector>
#include <string>
#include <stdexcept>
#include <cmath>
#include "stb_image.h"

enum class TextureType { Solid, Image };

struct Texture {
    TextureType type = TextureType::Solid;
    Color solid_color;              // Solid
    std::vector<float> pixels;      // Image: RGB float row-major, [0,1]
    int width = 0;
    int height = 0;

    Texture() = default;
    explicit Texture(const Color& c) : type(TextureType::Solid), solid_color(c) {}
};

// Sample with repeat wrapping. Returns RGB Color in [0,1].
inline Color texture_sample(const Texture& tex, double u, double v) {
    if (tex.type == TextureType::Solid) return tex.solid_color;
    if (tex.width == 0 || tex.height == 0) return Color(0, 0, 0);
    double uu = u - std::floor(u);
    double vv = v - std::floor(v);
    int x = static_cast<int>(uu * tex.width);
    int y = static_cast<int>(vv * tex.height);
    if (x < 0) x = 0; else if (x >= tex.width) x = tex.width - 1;
    if (y < 0) y = 0; else if (y >= tex.height) y = tex.height - 1;
    int idx = (y * tex.width + x) * 3;
    return Color(tex.pixels[idx], tex.pixels[idx + 1], tex.pixels[idx + 2]);
}

// Load image as RGB float texture. stbi_load forces 3 channels.
inline Texture load_image_texture(const std::string& path) {
    Texture tex;
    tex.type = TextureType::Image;
    int w, h, ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 3);
    if (!data) throw std::runtime_error("Failed to load image: " + path);
    tex.width = w;
    tex.height = h;
    tex.pixels.resize(static_cast<size_t>(w) * h * 3);
    for (size_t i = 0; i < tex.pixels.size(); i++) {
        tex.pixels[i] = static_cast<float>(data[i]) / 255.0f;
    }
    stbi_image_free(data);
    return tex;
}

#endif
