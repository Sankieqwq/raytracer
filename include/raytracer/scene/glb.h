#ifndef RT_GLB_H
#define RT_GLB_H

#include "raytracer/scene/json.h"
#include "raytracer/scene/obj.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

struct GlbMat4 {
    std::array<double, 16> m;

    static GlbMat4 identity() {
        GlbMat4 r{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0;
        return r;
    }

    Point3 transform_point(const Point3& p) const {
        return Point3(
            m[0] * p.x + m[4] * p.y + m[8]  * p.z + m[12],
            m[1] * p.x + m[5] * p.y + m[9]  * p.z + m[13],
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]);
    }

    Vec3 transform_vector(const Vec3& v) const {
        return Vec3(
            m[0] * v.x + m[4] * v.y + m[8]  * v.z,
            m[1] * v.x + m[5] * v.y + m[9]  * v.z,
            m[2] * v.x + m[6] * v.y + m[10] * v.z);
    }
};

inline GlbMat4 multiply_mat4(const GlbMat4& a, const GlbMat4& b) {
    GlbMat4 r{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            for (int k = 0; k < 4; k++) {
                r.m[col * 4 + row] += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
        }
    }
    return r;
}

inline uint32_t read_u32(const std::vector<unsigned char>& data, size_t offset) {
    if (offset + 4 > data.size()) throw std::runtime_error("GLB: unexpected end of file");
    return uint32_t(data[offset]) |
           (uint32_t(data[offset + 1]) << 8) |
           (uint32_t(data[offset + 2]) << 16) |
           (uint32_t(data[offset + 3]) << 24);
}

inline float read_f32(const std::vector<unsigned char>& data, size_t offset) {
    if (offset + 4 > data.size()) throw std::runtime_error("GLB: unexpected end of buffer");
    float v;
    std::memcpy(&v, data.data() + offset, 4);
    return v;
}

inline uint32_t read_index_value(const std::vector<unsigned char>& data,
                                 size_t offset,
                                 int component_type) {
    if (component_type == 5125) return read_u32(data, offset);
    if (component_type == 5123) {
        if (offset + 2 > data.size()) throw std::runtime_error("GLB: bad uint16 index");
        return uint32_t(data[offset]) | (uint32_t(data[offset + 1]) << 8);
    }
    if (component_type == 5121) {
        if (offset + 1 > data.size()) throw std::runtime_error("GLB: bad uint8 index");
        return uint32_t(data[offset]);
    }
    throw std::runtime_error("GLB: unsupported index component type");
}

inline int json_int(const JsonValue& obj, const std::string& key, int fallback = 0) {
    return obj.has(key) ? static_cast<int>(obj.at(key).numVal) : fallback;
}

inline double json_double(const JsonValue& obj, const std::string& key, double fallback = 0.0) {
    return obj.has(key) ? obj.at(key).numVal : fallback;
}

inline Vec3 glb_to_vec3(const JsonValue& arr) {
    return Vec3(arr.arrVal[0].numVal, arr.arrVal[1].numVal, arr.arrVal[2].numVal);
}

inline GlbMat4 mat4_from_node(const JsonValue& node) {
    GlbMat4 local = GlbMat4::identity();
    if (node.has("matrix")) {
        const auto& arr = node.at("matrix").arrVal;
        if (arr.size() != 16) throw std::runtime_error("GLB: node matrix must have 16 values");
        for (int i = 0; i < 16; i++) local.m[i] = arr[i].numVal;
        return local;
    }

    Vec3 t(0, 0, 0);
    Vec3 s(1, 1, 1);
    double x = 0, y = 0, z = 0, w = 1;

    if (node.has("translation")) t = glb_to_vec3(node.at("translation"));
    if (node.has("scale")) s = glb_to_vec3(node.at("scale"));
    if (node.has("rotation")) {
        const auto& q = node.at("rotation").arrVal;
        x = q[0].numVal; y = q[1].numVal; z = q[2].numVal; w = q[3].numVal;
    }

    local.m[0] = (1 - 2 * y * y - 2 * z * z) * s.x;
    local.m[1] = (2 * x * y + 2 * w * z) * s.x;
    local.m[2] = (2 * x * z - 2 * w * y) * s.x;
    local.m[4] = (2 * x * y - 2 * w * z) * s.y;
    local.m[5] = (1 - 2 * x * x - 2 * z * z) * s.y;
    local.m[6] = (2 * y * z + 2 * w * x) * s.y;
    local.m[8] = (2 * x * z + 2 * w * y) * s.z;
    local.m[9] = (2 * y * z - 2 * w * x) * s.z;
    local.m[10] = (1 - 2 * x * x - 2 * y * y) * s.z;
    local.m[12] = t.x;
    local.m[13] = t.y;
    local.m[14] = t.z;
    return local;
}

struct GlbContext {
    JsonValue root;
    std::vector<unsigned char> bin;
};

inline std::vector<unsigned char> read_binary_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open GLB file: " + path);
    return std::vector<unsigned char>(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}

inline GlbContext read_glb_context(const std::string& path) {
    std::vector<unsigned char> file = read_binary_file(path);
    if (file.size() < 20) throw std::runtime_error("GLB: file too small");
    if (read_u32(file, 0) != 0x46546c67) throw std::runtime_error("GLB: bad magic");
    if (read_u32(file, 4) != 2) throw std::runtime_error("GLB: only version 2 is supported");

    size_t offset = 12;
    std::string json_chunk;
    std::vector<unsigned char> bin_chunk;
    while (offset + 8 <= file.size()) {
        uint32_t chunk_length = read_u32(file, offset);
        uint32_t chunk_type = read_u32(file, offset + 4);
        offset += 8;
        if (offset + chunk_length > file.size()) throw std::runtime_error("GLB: bad chunk length");

        if (chunk_type == 0x4e4f534a) {
            json_chunk.assign(reinterpret_cast<const char*>(file.data() + offset), chunk_length);
        } else if (chunk_type == 0x004e4942) {
            bin_chunk.assign(file.begin() + offset, file.begin() + offset + chunk_length);
        }
        offset += chunk_length;
    }

    if (json_chunk.empty()) throw std::runtime_error("GLB: missing JSON chunk");
    if (bin_chunk.empty()) throw std::runtime_error("GLB: missing BIN chunk");
    return GlbContext{parse_json(json_chunk), bin_chunk};
}

inline size_t accessor_offset(const JsonValue& root, int accessor_index, size_t element_size) {
    const JsonValue& accessor = root.at("accessors").arrVal.at(accessor_index);
    int view_index = json_int(accessor, "bufferView", -1);
    if (view_index < 0) throw std::runtime_error("GLB: accessor missing bufferView");
    const JsonValue& view = root.at("bufferViews").arrVal.at(view_index);
    size_t view_offset = static_cast<size_t>(json_int(view, "byteOffset", 0));
    size_t accessor_byte_offset = static_cast<size_t>(json_int(accessor, "byteOffset", 0));
    (void)element_size;
    return view_offset + accessor_byte_offset;
}

inline size_t accessor_stride(const JsonValue& root, int accessor_index, size_t element_size) {
    const JsonValue& accessor = root.at("accessors").arrVal.at(accessor_index);
    int view_index = json_int(accessor, "bufferView", -1);
    const JsonValue& view = root.at("bufferViews").arrVal.at(view_index);
    return static_cast<size_t>(json_int(view, "byteStride", static_cast<int>(element_size)));
}

inline std::vector<Vec3> read_vec3_accessor(const GlbContext& glb, int accessor_index) {
    const JsonValue& accessor = glb.root.at("accessors").arrVal.at(accessor_index);
    if (accessor.at("type").strVal != "VEC3" || json_int(accessor, "componentType") != 5126) {
        throw std::runtime_error("GLB: expected float VEC3 accessor");
    }

    int count = json_int(accessor, "count");
    size_t offset = accessor_offset(glb.root, accessor_index, 12);
    size_t stride = accessor_stride(glb.root, accessor_index, 12);

    std::vector<Vec3> out;
    out.reserve(count);
    for (int i = 0; i < count; i++) {
        size_t p = offset + size_t(i) * stride;
        out.push_back(Vec3(read_f32(glb.bin, p), read_f32(glb.bin, p + 4), read_f32(glb.bin, p + 8)));
    }
    return out;
}

inline std::vector<Vec2> read_vec2_accessor(const GlbContext& glb, int accessor_index) {
    const JsonValue& accessor = glb.root.at("accessors").arrVal.at(accessor_index);
    if (accessor.at("type").strVal != "VEC2" || json_int(accessor, "componentType") != 5126) {
        throw std::runtime_error("GLB: expected float VEC2 accessor");
    }

    int count = json_int(accessor, "count");
    size_t offset = accessor_offset(glb.root, accessor_index, 8);
    size_t stride = accessor_stride(glb.root, accessor_index, 8);

    std::vector<Vec2> out;
    out.reserve(count);
    for (int i = 0; i < count; i++) {
        size_t p = offset + size_t(i) * stride;
        out.push_back(Vec2(read_f32(glb.bin, p), read_f32(glb.bin, p + 4)));
    }
    return out;
}

inline std::vector<uint32_t> read_index_accessor(const GlbContext& glb, int accessor_index) {
    const JsonValue& accessor = glb.root.at("accessors").arrVal.at(accessor_index);
    if (accessor.at("type").strVal != "SCALAR") throw std::runtime_error("GLB: expected scalar indices");

    int component_type = json_int(accessor, "componentType");
    size_t component_size = component_type == 5125 ? 4 : (component_type == 5123 ? 2 : 1);
    int count = json_int(accessor, "count");
    size_t offset = accessor_offset(glb.root, accessor_index, component_size);
    size_t stride = accessor_stride(glb.root, accessor_index, component_size);

    std::vector<uint32_t> out;
    out.reserve(count);
    for (int i = 0; i < count; i++) {
        out.push_back(read_index_value(glb.bin, offset + size_t(i) * stride, component_type));
    }
    return out;
}

inline std::vector<unsigned char> read_buffer_view_bytes(const GlbContext& glb, int view_index) {
    const JsonValue& view = glb.root.at("bufferViews").arrVal.at(view_index);
    size_t offset = static_cast<size_t>(json_int(view, "byteOffset", 0));
    size_t length = static_cast<size_t>(json_int(view, "byteLength", 0));
    if (offset + length > glb.bin.size()) throw std::runtime_error("GLB: image bufferView out of range");
    return std::vector<unsigned char>(glb.bin.begin() + offset, glb.bin.begin() + offset + length);
}

inline LoadedTextureData glb_texture_data(const GlbContext& glb,
                                          const std::filesystem::path& base_dir,
                                          int texture_index) {
    LoadedTextureData texture;
    const JsonValue& gltf_texture = glb.root.at("textures").arrVal.at(texture_index);
    int source_index = json_int(gltf_texture, "source", -1);
    if (source_index < 0) return texture;

    const JsonValue& image = glb.root.at("images").arrVal.at(source_index);
    if (image.has("name")) texture.name = image.at("name").strVal;
    if (image.has("mimeType")) texture.mime_type = image.at("mimeType").strVal;

    if (image.has("bufferView")) {
        texture.encoded = read_buffer_view_bytes(glb, static_cast<int>(image.at("bufferView").numVal));
    } else if (image.has("uri")) {
        std::filesystem::path uri(image.at("uri").strVal);
        if (uri.is_relative()) uri = base_dir / uri;
        texture.path = uri.lexically_normal().string();
    }

    return texture;
}

inline LoadedMaterialData glb_material_data(const JsonValue& material) {
    LoadedMaterialData data;
    data.use_pbr = true;
    data.albedo = Color(1.0, 1.0, 1.0);
    data.metallic = 1.0;
    data.roughness = 1.0;
    if (material.has("name")) data.name = material.at("name").strVal;

    // Parse a KHR_texture_transform block attached to a texture reference.
    auto parse_transform = [](const JsonValue& tex_ref, TextureTransform& out) {
        if (!tex_ref.has("extensions")) return;
        const JsonValue& exts = tex_ref.at("extensions");
        if (!exts.has("KHR_texture_transform")) return;
        const JsonValue& tr = exts.at("KHR_texture_transform");
        if (tr.has("scale")) {
            const auto& s = tr.at("scale").arrVal;
            if (s.size() >= 2) {
                out.scale = Vec2(s[0].numVal, s[1].numVal);
                out.active = true;
            }
        }
        if (tr.has("offset")) {
            const auto& o = tr.at("offset").arrVal;
            if (o.size() >= 2) {
                out.offset = Vec2(o[0].numVal, o[1].numVal);
                out.active = true;
            }
        }
    };

    if (material.has("pbrMetallicRoughness")) {
        const JsonValue& pbr = material.at("pbrMetallicRoughness");
        if (pbr.has("baseColorFactor")) {
            const auto& color = pbr.at("baseColorFactor").arrVal;
            if (color.size() >= 3) {
                data.albedo = Color(color[0].numVal, color[1].numVal, color[2].numVal);
            }
            if (color.size() >= 4) data.alpha = color[3].numVal;
        }
        data.metallic = json_double(pbr, "metallicFactor", 1.0);
        data.roughness = json_double(pbr, "roughnessFactor", 1.0);
        if (pbr.has("baseColorTexture")) {
            data.base_color_texture = json_int(pbr.at("baseColorTexture"), "index", -1);
            parse_transform(pbr.at("baseColorTexture"), data.base_color_transform);
        }
        if (pbr.has("metallicRoughnessTexture")) {
            data.metallic_roughness_texture = json_int(pbr.at("metallicRoughnessTexture"), "index", -1);
            parse_transform(pbr.at("metallicRoughnessTexture"), data.metallic_roughness_transform);
        }
    }

    if (material.has("normalTexture")) {
        data.normal_texture = json_int(material.at("normalTexture"), "index", -1);
        parse_transform(material.at("normalTexture"), data.normal_transform);
    }
    if (material.has("emissiveFactor")) {
        data.emissive = glb_to_vec3(material.at("emissiveFactor"));
    }
    if (material.has("emissiveTexture")) {
        data.emissive_texture = json_int(material.at("emissiveTexture"), "index", -1);
        parse_transform(material.at("emissiveTexture"), data.emissive_transform);
    }
    if (material.has("doubleSided")) {
        data.double_sided = material.at("doubleSided").boolVal;
    }

    if (material.has("alphaMode")) {
        const std::string& alpha_mode = material.at("alphaMode").strVal;
        if (alpha_mode == "BLEND") {
            data.alpha = data.alpha < 1.0 ? data.alpha : 0.5;
            data.alpha_blend = true;
        } else if (alpha_mode == "MASK") {
            data.alpha_mask = true;
            data.alpha_cutoff = json_double(material, "alphaCutoff", 0.5);
        }
    }

    if (material.has("extensions")) {
        const JsonValue& exts = material.at("extensions");
        if (exts.has("KHR_materials_transmission")) {
            const JsonValue& tr = exts.at("KHR_materials_transmission");
            data.transmission = json_double(tr, "transmissionFactor", 0.0);
            if (tr.has("transmissionTexture")) {
                data.transmission_texture = json_int(tr.at("transmissionTexture"), "index", -1);
                parse_transform(tr.at("transmissionTexture"), data.transmission_transform);
            }
        }
        if (exts.has("KHR_materials_ior")) {
            const JsonValue& ior_ext = exts.at("KHR_materials_ior");
            data.ior = json_double(ior_ext, "ior", 1.5);
        }
        if (exts.has("KHR_materials_volume")) {
            const JsonValue& volume = exts.at("KHR_materials_volume");
            if (volume.has("attenuationColor")) data.attenuation_color = glb_to_vec3(volume.at("attenuationColor"));
            if (volume.has("attenuationDistance")) data.attenuation_distance = volume.at("attenuationDistance").numVal;
            data.thickness_factor = json_double(volume, "thicknessFactor", 0.0);
            if (volume.has("thicknessTexture")) {
                data.thickness_texture = json_int(volume.at("thicknessTexture"), "index", -1);
                parse_transform(volume.at("thicknessTexture"), data.thickness_transform);
            }
        }
    }

    return data;
}

inline void add_glb_mesh_node(const GlbContext& glb,
                              int node_index,
                              const GlbMat4& parent_transform,
                              ObjMeshData& mesh) {
    const JsonValue& node = glb.root.at("nodes").arrVal.at(node_index);
    GlbMat4 transform = multiply_mat4(parent_transform, mat4_from_node(node));

    if (node.has("mesh")) {
        int mesh_index = static_cast<int>(node.at("mesh").numVal);
        const JsonValue& gltf_mesh = glb.root.at("meshes").arrVal.at(mesh_index);
        for (const JsonValue& prim : gltf_mesh.at("primitives").arrVal) {
            if (json_int(prim, "mode", 4) != 4) continue;

            const JsonValue& attrs = prim.at("attributes");
            int pos_accessor = json_int(attrs, "POSITION", -1);
            if (pos_accessor < 0) throw std::runtime_error("GLB: primitive missing POSITION");

            int normal_accessor = attrs.has("NORMAL") ? json_int(attrs, "NORMAL") : -1;
            int texcoord_accessor = attrs.has("TEXCOORD_0") ? json_int(attrs, "TEXCOORD_0") : -1;
            int material_index = json_int(prim, "material", -1);
            std::vector<Vec3> positions = read_vec3_accessor(glb, pos_accessor);
            std::vector<Vec3> normals;
            if (normal_accessor >= 0) normals = read_vec3_accessor(glb, normal_accessor);
            std::vector<Vec2> texcoords;
            if (texcoord_accessor >= 0) texcoords = read_vec2_accessor(glb, texcoord_accessor);

            std::vector<uint32_t> indices;
            if (prim.has("indices")) {
                indices = read_index_accessor(glb, static_cast<int>(prim.at("indices").numVal));
            } else {
                for (uint32_t i = 0; i < positions.size(); i++) indices.push_back(i);
            }

            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
                ObjTriangleData tri;
                tri.v0 = transform.transform_point(positions.at(i0));
                tri.v1 = transform.transform_point(positions.at(i1));
                tri.v2 = transform.transform_point(positions.at(i2));
                tri.material_index = material_index;

                if (!normals.empty()) {
                    tri.n0 = transform.transform_vector(normals.at(i0)).normalized();
                    tri.n1 = transform.transform_vector(normals.at(i1)).normalized();
                    tri.n2 = transform.transform_vector(normals.at(i2)).normalized();
                    tri.has_normals = true;
                }
                if (!texcoords.empty()) {
                    tri.uv0 = texcoords.at(i0);
                    tri.uv1 = texcoords.at(i1);
                    tri.uv2 = texcoords.at(i2);
                    tri.has_uvs = true;
                }
                mesh.triangles.push_back(tri);
            }
        }
    }

    if (node.has("children")) {
        for (const JsonValue& child : node.at("children").arrVal) {
            add_glb_mesh_node(glb, static_cast<int>(child.numVal), transform, mesh);
        }
    }
}

inline ObjMeshData load_glb_mesh(const std::string& path) {
    GlbContext glb = read_glb_context(path);
    ObjMeshData mesh;
    std::filesystem::path base_dir = std::filesystem::absolute(path).parent_path();

    if (glb.root.has("textures") && glb.root.has("images")) {
        for (size_t i = 0; i < glb.root.at("textures").arrVal.size(); i++) {
            mesh.textures.push_back(glb_texture_data(glb, base_dir, static_cast<int>(i)));
        }
    }

    if (glb.root.has("materials")) {
        for (const JsonValue& mat : glb.root.at("materials").arrVal) {
            mesh.materials.push_back(glb_material_data(mat));
        }
    }

    int scene_index = json_int(glb.root, "scene", 0);
    const JsonValue& scene = glb.root.at("scenes").arrVal.at(scene_index);
    for (const JsonValue& node : scene.at("nodes").arrVal) {
        add_glb_mesh_node(glb, static_cast<int>(node.numVal), GlbMat4::identity(), mesh);
    }

    if (mesh.triangles.empty()) throw std::runtime_error("GLB contains no triangle primitives: " + path);

    bool first = true;
    AABB box;
    for (const ObjTriangleData& tri : mesh.triangles) {
        AABB tri_box;
        Triangle(tri.v0, tri.v1, tri.v2).bounding_box(tri_box);
        box = first ? tri_box : AABB::surrounding_box(box, tri_box);
        first = false;
    }
    mesh.bounds = box;
    return mesh;
}

#endif
