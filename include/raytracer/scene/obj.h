#ifndef RT_OBJ_H
#define RT_OBJ_H

#include "raytracer/geometry/triangle.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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
            while (iss >> token) {
                face.push_back(parse_obj_face_index(token));
            }
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

    if (triangles.empty()) {
        throw std::runtime_error("OBJ contains no faces: " + path);
    }

    return triangles;
}

#endif
