#ifndef RT_TEXTURE_H
#define RT_TEXTURE_H

#include "raytracer/math/vec3.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <memory>
#include <utility>
#include <vector>

typedef unsigned char stbi_uc;
extern stbi_uc* stbi_load(const char* filename, int* x, int* y, int* channels_in_file, int desired_channels);
extern stbi_uc* stbi_load_from_memory(const stbi_uc* buffer, int len, int* x, int* y, int* channels_in_file, int desired_channels);
extern void stbi_image_free(void* retval_from_stbi_load);

class Texture {
public:
    virtual ~Texture() = default;
    virtual Color value(double u, double v, const Point3& p) const = 0;
};

class SolidColorTexture : public Texture {
public:
    Color color;

    SolidColorTexture() : color(0, 0, 0) {}
    explicit SolidColorTexture(const Color& color) : color(color) {}

    Color value(double u, double v, const Point3& p) const override {
        (void)u;
        (void)v;
        (void)p;
        return color;
    }
};

class TintedTexture : public Texture {
public:
    std::shared_ptr<Texture> texture;
    Color tint;

    TintedTexture(std::shared_ptr<Texture> texture, const Color& tint)
        : texture(std::move(texture)), tint(tint) {}

    Color value(double u, double v, const Point3& p) const override {
        return tint * texture->value(u, v, p);
    }
};

class ImageTexture : public Texture {
public:
    int width = 0;
    int height = 0;
    std::vector<Color> pixels;

    ImageTexture() = default;
    explicit ImageTexture(const std::string& path) { load(path); }
    ImageTexture(const std::vector<unsigned char>& encoded, const std::string& mime_type) {
        load(encoded, mime_type);
    }

    Color value(double u, double v, const Point3& p) const override {
        (void)p;
        if (pixels.empty() || width <= 0 || height <= 0) return Color(1, 0, 1);

        u = u - std::floor(u);
        v = v - std::floor(v);
        if (u < 0) u += 1.0;
        if (v < 0) v += 1.0;

        int i = static_cast<int>(u * width);
        int j = static_cast<int>((1.0 - v) * height);
        if (i < 0) i = 0;
        if (j < 0) j = 0;
        if (i >= width) i = width - 1;
        if (j >= height) j = height - 1;
        return pixels[j * width + i];
    }

private:
    static std::string lower_ext(const std::string& path) {
        std::string ext = std::filesystem::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext;
    }

    static std::string shell_quote(const std::string& s) {
        std::string out = "'";
        for (char c : s) {
            if (c == '\'') out += "'\\''";
            else out += c;
        }
        out += "'";
        return out;
    }

    static std::string temp_path(const std::string& suffix) {
        static int counter = 0;
        std::filesystem::path base = std::filesystem::temp_directory_path();
        return (base / ("raytracer_texture_" + std::to_string(std::time(nullptr)) +
                        "_" + std::to_string(counter++) + suffix)).string();
    }

    static std::string extension_for_mime(const std::string& mime_type) {
        if (mime_type == "image/png") return ".png";
        if (mime_type == "image/jpeg" || mime_type == "image/jpg") return ".jpg";
        if (mime_type == "image/x-portable-pixmap") return ".ppm";
        return ".img";
    }

    static void skip_comments(std::istream& in) {
        while (true) {
            in >> std::ws;
            if (in.peek() != '#') return;
            std::string line;
            std::getline(in, line);
        }
    }

    void load_ppm(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("Cannot open texture image: " + path);

        std::string magic;
        in >> magic;
        if (magic != "P3" && magic != "P6") {
            throw std::runtime_error("Unsupported texture image format: " + path);
        }

        skip_comments(in);
        in >> width;
        skip_comments(in);
        in >> height;
        skip_comments(in);
        int max_value = 255;
        in >> max_value;
        in.get();

        if (width <= 0 || height <= 0 || max_value <= 0) {
            throw std::runtime_error("Invalid PPM texture: " + path);
        }

        pixels.clear();
        pixels.reserve(static_cast<size_t>(width) * static_cast<size_t>(height));

        if (magic == "P6") {
            std::vector<unsigned char> raw(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
            in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
            if (in.gcount() != static_cast<std::streamsize>(raw.size())) {
                throw std::runtime_error("Truncated PPM texture: " + path);
            }
            for (size_t i = 0; i < raw.size(); i += 3) {
                pixels.push_back(Color(raw[i] / double(max_value),
                                       raw[i + 1] / double(max_value),
                                       raw[i + 2] / double(max_value)));
            }
        } else {
            for (int i = 0; i < width * height; i++) {
                int r, g, b;
                in >> r >> g >> b;
                pixels.push_back(Color(r / double(max_value),
                                       g / double(max_value),
                                       b / double(max_value)));
            }
        }
    }

    void load_stb_pixels(stbi_uc* data, int w, int h, const std::string& source) {
        if (!data) throw std::runtime_error("Failed to decode texture image: " + source);
        width = w;
        height = h;
        pixels.clear();
        pixels.reserve(static_cast<size_t>(width) * static_cast<size_t>(height));
        for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height) * 3; i += 3) {
            pixels.push_back(Color(data[i] / 255.0, data[i + 1] / 255.0, data[i + 2] / 255.0));
        }
        stbi_image_free(data);
    }

    void load_with_stb(const std::string& path) {
        int w = 0, h = 0, channels = 0;
        stbi_uc* data = stbi_load(path.c_str(), &w, &h, &channels, 3);
        load_stb_pixels(data, w, h, path);
    }

    void load_with_stb(const std::vector<unsigned char>& encoded, const std::string& mime_type) {
        int w = 0, h = 0, channels = 0;
        stbi_uc* data = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &w, &h, &channels, 3);
        load_stb_pixels(data, w, h, mime_type);
    }

    void load(const std::string& path) {
        std::string ext = lower_ext(path);
        if (ext == ".ppm" || ext == ".pnm") load_ppm(path);
        else load_with_stb(path);
    }

    void load(const std::vector<unsigned char>& encoded, const std::string& mime_type) {
        if (mime_type != "image/x-portable-pixmap") {
            load_with_stb(encoded, mime_type);
            return;
        }

        std::string source = temp_path(extension_for_mime(mime_type));
        {
            std::ofstream out(source, std::ios::binary);
            out.write(reinterpret_cast<const char*>(encoded.data()),
                      static_cast<std::streamsize>(encoded.size()));
        }

        try {
            load_ppm(source);
            std::filesystem::remove(source);
        } catch (...) {
            std::filesystem::remove(source);
            throw;
        }
    }
};

#endif
