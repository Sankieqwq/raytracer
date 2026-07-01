// RGBA32F result format shared by Blender bridge and remote server.
#ifndef RT_RGBA32F_H
#define RT_RGBA32F_H

#include "raytracer/render/renderer.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

inline void append_u32_le(std::vector<unsigned char>& bytes, uint32_t value) {
    bytes.push_back(static_cast<unsigned char>(value & 0xffu));
    bytes.push_back(static_cast<unsigned char>((value >> 8) & 0xffu));
    bytes.push_back(static_cast<unsigned char>((value >> 16) & 0xffu));
    bytes.push_back(static_cast<unsigned char>((value >> 24) & 0xffu));
}

inline float finite_channel(double v) {
    if (!std::isfinite(v)) return 0.0f;
    return static_cast<float>(std::max(0.0, v));
}

inline void append_float_le(std::vector<unsigned char>& bytes, float value) {
    const unsigned char* raw = reinterpret_cast<const unsigned char*>(&value);
    bytes.insert(bytes.end(), raw, raw + sizeof(float));
}

inline std::vector<unsigned char> encode_rgba32f(const RenderOutput& output) {
    std::vector<unsigned char> bytes;
    bytes.reserve(16 + static_cast<size_t>(output.width) * static_cast<size_t>(output.height) * 16);

    const char magic[8] = {'R', 'T', 'R', 'G', 'B', 'A', 'F', '1'};
    bytes.insert(bytes.end(), magic, magic + 8);
    append_u32_le(bytes, static_cast<uint32_t>(output.width));
    append_u32_le(bytes, static_cast<uint32_t>(output.height));

    double scale = output.samples > 0 ? 1.0 / output.samples : 1.0;
    for (const Color& raw : output.pixels) {
        append_float_le(bytes, finite_channel(raw.x * scale));
        append_float_le(bytes, finite_channel(raw.y * scale));
        append_float_le(bytes, finite_channel(raw.z * scale));
        append_float_le(bytes, 1.0f);
    }
    return bytes;
}

inline void write_rgba32f(const std::string& path, const RenderOutput& output) {
    std::filesystem::path out_path(path);
    if (out_path.has_parent_path()) {
        std::filesystem::create_directories(out_path.parent_path());
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot open RGBA output: " + path);

    std::vector<unsigned char> bytes = encode_rgba32f(output);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

#endif
