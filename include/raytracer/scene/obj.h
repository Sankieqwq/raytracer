#ifndef RT_OBJ_H
#define RT_OBJ_H

#include "raytracer/geometry/aabb.h"
#include "raytracer/geometry/triangle.h"
#include "raytracer/math/vec2.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
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

inline ObjMeshData load_obj_mesh(const std::string& path,
                                 double scale = 1.0,
                                 const Vec3& translate = Vec3(0, 0, 0)) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot open OBJ file: " + path);

    std::vector<Point3> positions;
    std::vector<Vec2> texcoords;
    std::vector<Vec3> normals;
    std::vector<std::vector<ObjFaceIndex>> faces;
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
        } else if (tag == "f") {
            std::vector<ObjFaceIndex> face;
            std::string token;
            while (iss >> token) {
                face.push_back(parse_obj_face_index(token));
            }
            if (face.size() < 3) continue;
            faces.push_back(face);
        }
    }

    ObjMeshData mesh;
    mesh.bounds = compute_obj_bounds(positions, scale, translate);

    for (const std::vector<ObjFaceIndex>& face : faces) {
        for (size_t i = 1; i + 1 < face.size(); i++) {
            ObjTriangleData tri;
            const ObjFaceIndex idx0 = face[0];
            const ObjFaceIndex idx1 = face[i];
            const ObjFaceIndex idx2 = face[i + 1];

            tri.v0 = transform_obj_point(
                positions.at(resolve_obj_index(idx0.v, positions.size())), scale, translate);
            tri.v1 = transform_obj_point(
                positions.at(resolve_obj_index(idx1.v, positions.size())), scale, translate);
            tri.v2 = transform_obj_point(
                positions.at(resolve_obj_index(idx2.v, positions.size())), scale, translate);

            if (idx0.vn != 0 && idx1.vn != 0 && idx2.vn != 0 && !normals.empty()) {
                tri.n0 = normals.at(resolve_obj_index(idx0.vn, normals.size()));
                tri.n1 = normals.at(resolve_obj_index(idx1.vn, normals.size()));
                tri.n2 = normals.at(resolve_obj_index(idx2.vn, normals.size()));
                tri.has_normals = true;
            }

            if (idx0.vt != 0 && idx1.vt != 0 && idx2.vt != 0 && !texcoords.empty()) {
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

#endif
