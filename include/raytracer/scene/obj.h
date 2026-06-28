// Module D: scene -- OBJ loader (tinyobjloader + legacy fallback)
#ifndef RT_OBJ_H
#define RT_OBJ_H

#include "raytracer/geometry/triangle_mesh.h"
#include "raytracer/math/mat4.h"
#include "raytracer/math/vec3.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// ============================================================
// Legacy hand-written parser (fallback when tinyobjloader fails)
// ============================================================

struct ObjTriangleData {
    Point3 v0, v1, v2;
    Vec3 n0, n1, n2;
    bool has_normals = false;
};

struct ObjFaceIndex {
    int v = 0;
    int vt = 0;
    int vn = 0;
};

inline int resolve_obj_index(int index, size_t count) {
    if (index > 0) return index - 1;
    if (index < 0) return static_cast<int>(count) + index;
    throw std::runtime_error("OBJ index cannot be zero");
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

inline std::vector<ObjTriangleData> load_obj_triangles(const std::string& path,
                                                       double scale = 1.0,
                                                       const Vec3& translate = Vec3(0, 0, 0)) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot open OBJ file: " + path);

    std::vector<Point3> positions;
    std::vector<Vec3> normals;
    std::vector<ObjTriangleData> triangles;
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
            positions.push_back(scale * Point3(x, y, z) + translate);
        } else if (tag == "vn") {
            double x, y, z;
            iss >> x >> y >> z;
            normals.push_back(Vec3(x, y, z).normalized());
        } else if (tag == "f") {
            std::vector<ObjFaceIndex> face;
            std::string token;
            while (iss >> token) face.push_back(parse_obj_face_index(token));
            if (face.size() < 3) continue;
            for (size_t i = 1; i + 1 < face.size(); i++) {
                ObjTriangleData tri;
                const ObjFaceIndex idx0 = face[0];
                const ObjFaceIndex idx1 = face[i];
                const ObjFaceIndex idx2 = face[i + 1];
                tri.v0 = positions.at(resolve_obj_index(idx0.v, positions.size()));
                tri.v1 = positions.at(resolve_obj_index(idx1.v, positions.size()));
                tri.v2 = positions.at(resolve_obj_index(idx2.v, positions.size()));
                if (idx0.vn != 0 && idx1.vn != 0 && idx2.vn != 0 && !normals.empty()) {
                    tri.n0 = normals.at(resolve_obj_index(idx0.vn, normals.size()));
                    tri.n1 = normals.at(resolve_obj_index(idx1.vn, normals.size()));
                    tri.n2 = normals.at(resolve_obj_index(idx2.vn, normals.size()));
                    tri.has_normals = true;
                }
                triangles.push_back(tri);
            }
        }
    }
    if (triangles.empty()) throw std::runtime_error("OBJ contains no faces: " + path);
    return triangles;
}

// ============================================================
// New: tinyobjloader-based loader -> TriangleMesh (SoA, baked)
// ============================================================

#include "tiny_obj_loader.h"

// Load OBJ via tinyobjloader, bake `transform` into vertices/normals.
// Computes area-weighted smooth normals when OBJ lacks vn.
// Throws std::runtime_error on failure.
inline TriangleMesh load_obj_mesh(const std::string& path, const Mat4& transform) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str())) {
        throw std::runtime_error("tinyobjloader failed: " + err + " " + warn);
    }

    const bool has_vn = !attrib.normals.empty();
    const bool has_vt = !attrib.texcoords.empty();

    // Smooth normals accumulator (only if missing vn)
    std::vector<Vec3> smooth_normals;
    if (!has_vn) {
        smooth_normals.assign(attrib.vertices.size() / 3, Vec3(0, 0, 0));
        // First pass: accumulate area-weighted face normals per vertex
        for (const auto& shape : shapes) {
            size_t idx_offset = 0;
            for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
                int fv = shape.mesh.num_face_vertices[f];
                if (fv < 3) { idx_offset += fv; continue; }
                tinyobj::index_t i0 = shape.mesh.indices[idx_offset + 0];
                tinyobj::index_t i1 = shape.mesh.indices[idx_offset + 1];
                tinyobj::index_t i2 = shape.mesh.indices[idx_offset + 2];
                Vec3 p0(attrib.vertices[3*i0.vertex_index + 0],
                        attrib.vertices[3*i0.vertex_index + 1],
                        attrib.vertices[3*i0.vertex_index + 2]);
                Vec3 p1(attrib.vertices[3*i1.vertex_index + 0],
                        attrib.vertices[3*i1.vertex_index + 1],
                        attrib.vertices[3*i1.vertex_index + 2]);
                Vec3 p2(attrib.vertices[3*i2.vertex_index + 0],
                        attrib.vertices[3*i2.vertex_index + 1],
                        attrib.vertices[3*i2.vertex_index + 2]);
                Vec3 face_n = cross(p1 - p0, p2 - p0);
                double area = 0.5 * face_n.length();
                face_n = face_n.normalized();
                smooth_normals[i0.vertex_index] += area * face_n;
                smooth_normals[i1.vertex_index] += area * face_n;
                smooth_normals[i2.vertex_index] += area * face_n;
                idx_offset += fv;
            }
        }
        for (auto& n : smooth_normals) {
            if (n.length_squared() < 1e-12) n = Vec3(0, 1, 0);
            else n = n.normalized();
        }
    }

    TriangleMesh mesh;

    auto get_pos = [&](const tinyobj::index_t& idx) {
        return Vec3(attrib.vertices[3*idx.vertex_index + 0],
                    attrib.vertices[3*idx.vertex_index + 1],
                    attrib.vertices[3*idx.vertex_index + 2]);
    };
    auto get_normal = [&](const tinyobj::index_t& idx) {
        return Vec3(attrib.normals[3*idx.normal_index + 0],
                    attrib.normals[3*idx.normal_index + 1],
                    attrib.normals[3*idx.normal_index + 2]);
    };
    auto get_uv = [&](const tinyobj::index_t& idx) {
        return Vec2(attrib.texcoords[2*idx.texcoord_index + 0],
                    attrib.texcoords[2*idx.texcoord_index + 1]);
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

                if (has_vn) {
                    mesh.normals.push_back(transform.transform_normal(get_normal(idx0)).normalized());
                    mesh.normals.push_back(transform.transform_normal(get_normal(idxk)).normalized());
                    mesh.normals.push_back(transform.transform_normal(get_normal(idxk1)).normalized());
                } else {
                    mesh.normals.push_back(transform.transform_normal(smooth_normals[idx0.vertex_index]).normalized());
                    mesh.normals.push_back(transform.transform_normal(smooth_normals[idxk.vertex_index]).normalized());
                    mesh.normals.push_back(transform.transform_normal(smooth_normals[idxk1.vertex_index]).normalized());
                }

                if (has_vt) {
                    mesh.uvs.push_back(get_uv(idx0));
                    mesh.uvs.push_back(get_uv(idxk));
                    mesh.uvs.push_back(get_uv(idxk1));
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
