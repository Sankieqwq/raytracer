// Module D: scene -- OBJ loader (tinyobjloader + legacy fallback)
#ifndef RT_OBJ_H
#define RT_OBJ_H

#include "raytracer/geometry/aabb.h"
#include "raytracer/geometry/triangle.h"
#include "raytracer/geometry/triangle_mesh.h"
#include "raytracer/math/mat4.h"
#include "raytracer/math/vec2.h"
#include "raytracer/math/vec3.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

struct ObjTriangleData {
    Point3 v0, v1, v2;
    Vec3 n0, n1, n2;
    Vec2 uv0, uv1, uv2;
    bool has_normals = false;
    bool has_uvs = false;
    int material_index = -1;
};

struct LoadedTextureData {
    std::string name;
    std::string path;
    std::string mime_type;
    std::vector<unsigned char> encoded;
};

struct LoadedMaterialData {
    std::string name;
    Color albedo = Color(0.7, 0.7, 0.7);
    double metallic = 0.0;
    double roughness = 0.6;
    double alpha = 1.0;
    int base_color_texture = -1;
    int metallic_roughness_texture = -1;
    int normal_texture = -1;
    Color emissive = Color(0, 0, 0);
    int emissive_texture = -1;
    bool double_sided = false;
    Color attenuation_color = Color(1, 1, 1);
    double attenuation_distance = infinity;
    bool alpha_blend = false;       // alphaMode == "BLEND"
    double transmission = 0.0;      // KHR_materials_transmission
    double ior = 1.5;               // KHR_materials_ior (glTF default)
};

struct ObjMeshData {
    std::vector<ObjTriangleData> triangles;
    AABB bounds;
    std::vector<LoadedMaterialData> materials;
    std::vector<LoadedTextureData> textures;
};

struct ObjFaceIndex {
    int v = 0;
    int vt = 0;
    int vn = 0;
};

struct ParsedObjFace {
    std::vector<ObjFaceIndex> vertices;
    std::string material_name;
};

inline int resolve_obj_index(int index, size_t count) {
    if (index > 0) return index - 1;
    if (index < 0) return static_cast<int>(count) + index;
    throw std::runtime_error("OBJ index cannot be zero");
}

inline bool valid_resolved_index(int index, size_t count) {
    int resolved = index > 0 ? index - 1 : static_cast<int>(count) + index;
    return index != 0 && resolved >= 0 && static_cast<size_t>(resolved) < count;
}

inline ObjFaceIndex parse_obj_face_index(const std::string& token) {
    ObjFaceIndex idx;
    size_t first = token.find('/');
    if (first == std::string::npos) {
        idx.v = std::stoi(token);
        return idx;
    }
    size_t second = token.find('/', first + 1);
    idx.v = std::stoi(token.substr(0, first));
    if (second == std::string::npos) {
        std::string vt = token.substr(first + 1);
        if (!vt.empty()) idx.vt = std::stoi(vt);
        return idx;
    }
    std::string vt = token.substr(first + 1, second - first - 1);
    std::string vn = token.substr(second + 1);
    if (!vt.empty()) idx.vt = std::stoi(vt);
    if (!vn.empty()) idx.vn = std::stoi(vn);
    return idx;
}

inline Point3 transform_obj_point(const Point3& p, double scale, const Vec3& translate) {
    return scale * p + translate;
}

inline AABB compute_obj_bounds(const std::vector<Point3>& positions,
                               double scale,
                               const Vec3& translate) {
    if (positions.empty()) throw std::runtime_error("OBJ contains no vertices");

    Point3 first = transform_obj_point(positions[0], scale, translate);
    Point3 min_p = first;
    Point3 max_p = first;

    for (const Point3& p : positions) {
        Point3 t = transform_obj_point(p, scale, translate);
        min_p.x = std::min(min_p.x, t.x);
        min_p.y = std::min(min_p.y, t.y);
        min_p.z = std::min(min_p.z, t.z);
        max_p.x = std::max(max_p.x, t.x);
        max_p.y = std::max(max_p.y, t.y);
        max_p.z = std::max(max_p.z, t.z);
    }

    return AABB(min_p, max_p);
}

inline std::filesystem::path resolve_obj_relative_path(const std::filesystem::path& base_dir,
                                                       const std::string& asset_path) {
    std::filesystem::path path(asset_path);
    if (path.is_relative()) path = base_dir / path;
    return path.lexically_normal();
}

inline void load_obj_mtl_file(const std::filesystem::path& obj_dir,
                              const std::string& mtl_name,
                              ObjMeshData& mesh,
                              std::unordered_map<std::string, int>& material_indices) {
    std::filesystem::path mtl_path = resolve_obj_relative_path(obj_dir, mtl_name);
    std::ifstream in(mtl_path);
    if (!in) return;

    std::filesystem::path mtl_dir = mtl_path.parent_path();
    LoadedMaterialData* current = nullptr;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string tag;
        iss >> tag;
        if (tag == "newmtl") {
            std::string name;
            iss >> name;
            if (name.empty()) {
                current = nullptr;
                continue;
            }

            LoadedMaterialData data;
            data.name = name;
            material_indices[name] = static_cast<int>(mesh.materials.size());
            mesh.materials.push_back(data);
            current = &mesh.materials.back();
        } else if (tag == "Kd" && current) {
            double r, g, b;
            if (iss >> r >> g >> b) {
                current->albedo = Color(r, g, b);
            }
        } else if (tag == "map_Kd" && current) {
            std::string texture_name;
            iss >> texture_name;
            if (!texture_name.empty()) {
                LoadedTextureData texture;
                texture.name = texture_name;
                texture.path = resolve_obj_relative_path(mtl_dir, texture_name).string();
                current->base_color_texture = static_cast<int>(mesh.textures.size());
                mesh.textures.push_back(texture);
            }
        }
    }
}

inline Vec3 generated_obj_normal(const std::vector<Vec3>& smooth_normals,
                                 const ObjFaceIndex& idx) {
    int vertex = resolve_obj_index(idx.v, smooth_normals.size());
    Vec3 normal = smooth_normals.at(static_cast<size_t>(vertex));
    if (normal.length_squared() < 1e-12) return Vec3(0, 1, 0);
    return normal.normalized();
}

inline ObjMeshData load_obj_mesh(const std::string& path,
                                 double scale = 1.0,
                                 const Vec3& translate = Vec3(0, 0, 0)) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot open OBJ file: " + path);

    std::vector<Point3> positions;
    std::vector<Vec2> texcoords;
    std::vector<Vec3> normals;
    std::vector<ParsedObjFace> faces;
    std::vector<std::string> mtllibs;
    std::string current_material;
    std::string line;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string tag;
        iss >> tag;
        if (tag == "v") {
            double x, y, z;
            iss >> x >> y >> z;
            positions.push_back(Point3(x, y, z));
        } else if (tag == "vn") {
            double x, y, z;
            iss >> x >> y >> z;
            normals.push_back(Vec3(x, y, z).normalized());
        } else if (tag == "vt") {
            double u, v;
            iss >> u >> v;
            texcoords.push_back(Vec2(u, v));
        } else if (tag == "mtllib") {
            std::string mtl_name;
            while (iss >> mtl_name) mtllibs.push_back(mtl_name);
        } else if (tag == "usemtl") {
            iss >> current_material;
        } else if (tag == "f") {
            ParsedObjFace face;
            face.material_name = current_material;
            std::string token;
            while (iss >> token) face.vertices.push_back(parse_obj_face_index(token));
            if (face.vertices.size() >= 3) faces.push_back(face);
        }
    }

    ObjMeshData mesh;
    std::unordered_map<std::string, int> material_indices;
    std::filesystem::path obj_dir = std::filesystem::absolute(path).parent_path();
    for (const std::string& mtl_name : mtllibs) {
        load_obj_mtl_file(obj_dir, mtl_name, mesh, material_indices);
    }

    mesh.bounds = compute_obj_bounds(positions, scale, translate);

    std::vector<Vec3> smooth_normals(positions.size(), Vec3(0, 0, 0));
    for (const ParsedObjFace& face : faces) {
        for (size_t i = 1; i + 1 < face.vertices.size(); i++) {
            const ObjFaceIndex& idx0 = face.vertices[0];
            const ObjFaceIndex& idx1 = face.vertices[i];
            const ObjFaceIndex& idx2 = face.vertices[i + 1];
            int i0 = resolve_obj_index(idx0.v, positions.size());
            int i1 = resolve_obj_index(idx1.v, positions.size());
            int i2 = resolve_obj_index(idx2.v, positions.size());
            Vec3 face_n = cross(positions[i1] - positions[i0], positions[i2] - positions[i0]);
            if (face_n.length_squared() > 1e-20) {
                smooth_normals[static_cast<size_t>(i0)] += face_n;
                smooth_normals[static_cast<size_t>(i1)] += face_n;
                smooth_normals[static_cast<size_t>(i2)] += face_n;
            }
        }
    }
    for (Vec3& n : smooth_normals) {
        if (n.length_squared() > 1e-12) n = n.normalized();
    }

    for (const ParsedObjFace& face : faces) {
        for (size_t i = 1; i + 1 < face.vertices.size(); i++) {
            ObjTriangleData tri;
            const ObjFaceIndex idx0 = face.vertices[0];
            const ObjFaceIndex idx1 = face.vertices[i];
            const ObjFaceIndex idx2 = face.vertices[i + 1];
            auto material_it = material_indices.find(face.material_name);
            if (material_it != material_indices.end()) {
                tri.material_index = material_it->second;
            }

            tri.v0 = transform_obj_point(
                positions.at(resolve_obj_index(idx0.v, positions.size())), scale, translate);
            tri.v1 = transform_obj_point(
                positions.at(resolve_obj_index(idx1.v, positions.size())), scale, translate);
            tri.v2 = transform_obj_point(
                positions.at(resolve_obj_index(idx2.v, positions.size())), scale, translate);

            bool has_all_normals =
                idx0.vn != 0 && idx1.vn != 0 && idx2.vn != 0 &&
                valid_resolved_index(idx0.vn, normals.size()) &&
                valid_resolved_index(idx1.vn, normals.size()) &&
                valid_resolved_index(idx2.vn, normals.size());
            if (has_all_normals) {
                tri.n0 = normals.at(resolve_obj_index(idx0.vn, normals.size()));
                tri.n1 = normals.at(resolve_obj_index(idx1.vn, normals.size()));
                tri.n2 = normals.at(resolve_obj_index(idx2.vn, normals.size()));
            } else {
                tri.n0 = generated_obj_normal(smooth_normals, idx0);
                tri.n1 = generated_obj_normal(smooth_normals, idx1);
                tri.n2 = generated_obj_normal(smooth_normals, idx2);
            }
            tri.has_normals = true;

            bool has_all_uvs =
                idx0.vt != 0 && idx1.vt != 0 && idx2.vt != 0 &&
                valid_resolved_index(idx0.vt, texcoords.size()) &&
                valid_resolved_index(idx1.vt, texcoords.size()) &&
                valid_resolved_index(idx2.vt, texcoords.size());
            if (has_all_uvs) {
                tri.uv0 = texcoords.at(resolve_obj_index(idx0.vt, texcoords.size()));
                tri.uv1 = texcoords.at(resolve_obj_index(idx1.vt, texcoords.size()));
                tri.uv2 = texcoords.at(resolve_obj_index(idx2.vt, texcoords.size()));
                tri.has_uvs = true;
            }

            mesh.triangles.push_back(tri);
        }
    }

    if (mesh.triangles.empty()) {
        throw std::runtime_error("OBJ contains no faces: " + path);
    }

    return mesh;
}

inline std::vector<ObjTriangleData> load_obj_triangles(const std::string& path,
                                                       double scale = 1.0,
                                                       const Vec3& translate = Vec3(0, 0, 0)) {
    return load_obj_mesh(path, scale, translate).triangles;
}

#include "tiny_obj_loader.h"

inline TriangleMesh load_obj_mesh(const std::string& path, const Mat4& transform) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str())) {
        throw std::runtime_error("tinyobjloader failed: " + err + " " + warn);
    }

    TriangleMesh mesh;

    auto get_pos = [&](const tinyobj::index_t& idx) {
        if (idx.vertex_index < 0 ||
            static_cast<size_t>(3 * idx.vertex_index + 2) >= attrib.vertices.size()) {
            throw std::runtime_error("OBJ vertex index out of range: " + path);
        }
        return Vec3(attrib.vertices[3 * idx.vertex_index + 0],
                    attrib.vertices[3 * idx.vertex_index + 1],
                    attrib.vertices[3 * idx.vertex_index + 2]);
    };
    auto has_normal = [&](const tinyobj::index_t& idx) {
        return idx.normal_index >= 0 &&
               static_cast<size_t>(3 * idx.normal_index + 2) < attrib.normals.size();
    };
    auto get_normal = [&](const tinyobj::index_t& idx) {
        return Vec3(attrib.normals[3 * idx.normal_index + 0],
                    attrib.normals[3 * idx.normal_index + 1],
                    attrib.normals[3 * idx.normal_index + 2]);
    };
    auto has_uv = [&](const tinyobj::index_t& idx) {
        return idx.texcoord_index >= 0 &&
               static_cast<size_t>(2 * idx.texcoord_index + 1) < attrib.texcoords.size();
    };
    auto get_uv = [&](const tinyobj::index_t& idx) {
        return Vec2(attrib.texcoords[2 * idx.texcoord_index + 0],
                    attrib.texcoords[2 * idx.texcoord_index + 1]);
    };

    std::vector<Vec3> smooth_normals(attrib.vertices.size() / 3, Vec3(0, 0, 0));
    bool any_uv = false;
    for (const auto& shape : shapes) {
        size_t idx_offset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            int fv = shape.mesh.num_face_vertices[f];
            for (int k = 1; k + 1 < fv; k++) {
                tinyobj::index_t idx0 = shape.mesh.indices[idx_offset + 0];
                tinyobj::index_t idxk = shape.mesh.indices[idx_offset + k];
                tinyobj::index_t idxk1 = shape.mesh.indices[idx_offset + k + 1];

                Vec3 p0 = get_pos(idx0);
                Vec3 p1 = get_pos(idxk);
                Vec3 p2 = get_pos(idxk1);
                Vec3 face_n = cross(p1 - p0, p2 - p0);
                if (face_n.length_squared() > 1e-20) {
                    smooth_normals[static_cast<size_t>(idx0.vertex_index)] += face_n;
                    smooth_normals[static_cast<size_t>(idxk.vertex_index)] += face_n;
                    smooth_normals[static_cast<size_t>(idxk1.vertex_index)] += face_n;
                }
                any_uv = any_uv || has_uv(idx0) || has_uv(idxk) || has_uv(idxk1);
            }
            idx_offset += fv;
        }
    }
    for (Vec3& n : smooth_normals) {
        if (n.length_squared() < 1e-12) n = Vec3(0, 1, 0);
        else n = n.normalized();
    }

    auto generated_normal = [&](const tinyobj::index_t& idx) {
        if (idx.vertex_index >= 0 &&
            static_cast<size_t>(idx.vertex_index) < smooth_normals.size()) {
            return smooth_normals[static_cast<size_t>(idx.vertex_index)];
        }
        return Vec3(0, 1, 0);
    };
    auto baked_normal = [&](const tinyobj::index_t& idx) {
        Vec3 n = has_normal(idx) ? get_normal(idx) : generated_normal(idx);
        Vec3 transformed = transform.transform_normal(n);
        if (transformed.length_squared() < 1e-12) return Vec3(0, 1, 0);
        return transformed.normalized();
    };
    auto safe_uv = [&](const tinyobj::index_t& idx) {
        return has_uv(idx) ? get_uv(idx) : Vec2(0, 0);
    };

    for (const auto& shape : shapes) {
        size_t idx_offset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            int fv = shape.mesh.num_face_vertices[f];
            for (int k = 1; k + 1 < fv; k++) {
                tinyobj::index_t idx0 = shape.mesh.indices[idx_offset + 0];
                tinyobj::index_t idxk = shape.mesh.indices[idx_offset + k];
                tinyobj::index_t idxk1 = shape.mesh.indices[idx_offset + k + 1];

                mesh.vertices.push_back(transform.transform_point(get_pos(idx0)));
                mesh.vertices.push_back(transform.transform_point(get_pos(idxk)));
                mesh.vertices.push_back(transform.transform_point(get_pos(idxk1)));
                mesh.indices.push_back(static_cast<int>(mesh.vertices.size()) - 3);
                mesh.indices.push_back(static_cast<int>(mesh.vertices.size()) - 2);
                mesh.indices.push_back(static_cast<int>(mesh.vertices.size()) - 1);

                mesh.normals.push_back(baked_normal(idx0));
                mesh.normals.push_back(baked_normal(idxk));
                mesh.normals.push_back(baked_normal(idxk1));

                if (any_uv) {
                    mesh.uvs.push_back(safe_uv(idx0));
                    mesh.uvs.push_back(safe_uv(idxk));
                    mesh.uvs.push_back(safe_uv(idxk1));
                }
            }
            idx_offset += fv;
        }
    }

    if (mesh.vertices.empty()) {
        throw std::runtime_error("OBJ contains no triangles: " + path);
    }
    return mesh;
}

#endif
