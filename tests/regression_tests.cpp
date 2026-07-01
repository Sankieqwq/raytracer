#define TINYOBJLOADER_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "raytracer/scene/scene.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

bool near(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps;
}

bool near_vec(const Vec3& a, const Vec3& b, double eps = 1e-9) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

bool finite_vec(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool positive_color(const Color& c) {
    return c.x > 0 && c.y > 0 && c.z > 0;
}

JsonValue number(double value) {
    JsonValue v;
    v.type = JsonValue::Number;
    v.numVal = value;
    return v;
}

JsonValue string_value(const std::string& value) {
    JsonValue v;
    v.type = JsonValue::String;
    v.strVal = value;
    return v;
}

JsonValue array3(double x, double y, double z) {
    JsonValue v;
    v.type = JsonValue::Array;
    v.arrVal = {number(x), number(y), number(z)};
    return v;
}

template <typename T, typename = void>
struct has_acceleration_node_count : std::false_type {};

template <typename T>
struct has_acceleration_node_count<T, std::void_t<decltype(std::declval<const T&>().acceleration_node_count())>>
    : std::true_type {};

template <typename Mesh>
void check_acceleration_node_count(const Mesh& mesh) {
    if constexpr (has_acceleration_node_count<Mesh>::value) {
        check(mesh.acceleration_node_count() > 1, "TriangleMesh should build internal triangle-level acceleration");
    } else {
        check(false, "TriangleMesh should expose internal acceleration node count for regression coverage");
    }
}

void test_parse_transform_uses_srt_order() {
    JsonValue obj;
    obj.type = JsonValue::Object;
    JsonValue transform;
    transform.type = JsonValue::Object;
    transform.objVal["scale"] = number(2.0);
    transform.objVal["translate"] = array3(10.0, 0.0, 0.0);
    obj.objVal["transform"] = transform;

    Vec3 p = parse_transform(obj).transform_point(Vec3(1, 0, 0));
    check(near_vec(p, Vec3(12, 0, 0)), "transform block must apply scale before translate without scaling translation");
}

void test_transform_normal_matches_rotation_direction() {
    Mat4 r = Mat4::rotate_z(90);
    Vec3 normal = r.transform_normal(Vec3(1, 0, 0)).normalized();
    check(near_vec(normal, Vec3(0, 1, 0), 1e-9), "rotate_z(90) must rotate normals in the same direction as directions");
}

void test_sphere_pole_tangent_is_finite() {
    Sphere sphere(Point3(0, 0, 0), 1.0, nullptr);
    Ray ray(Point3(0, 2, 0), Vec3(0, -1, 0));
    HitRecord rec;
    check(sphere.hit(ray, 0.001, infinity, rec), "ray should hit sphere pole");
    check(rec.has_tangent, "sphere hit should provide tangent");
    check(finite_vec(rec.tangent) && rec.tangent.length_squared() > 0, "sphere pole tangent must be finite and non-zero");
}

void test_degenerate_triangle_uv_tangent_is_finite() {
    Triangle tri(Point3(0, 0, 0), Point3(1, 0, 0), Point3(0, 0, -1),
                 Vec3(0, 1, 0), Vec3(0, 1, 0), Vec3(0, 1, 0),
                 Vec2(0, 0), Vec2(0, 0), Vec2(0, 0), nullptr);
    Ray ray(Point3(0.25, 1, -0.25), Vec3(0, -1, 0));
    HitRecord rec;
    check(tri.hit(ray, 0.001, infinity, rec), "ray should hit degenerate-UV triangle");
    check(rec.has_tangent, "triangle with UVs should provide tangent");
    check(finite_vec(rec.tangent) && rec.tangent.length_squared() > 0, "degenerate triangle UV tangent must be finite and non-zero");
}

void test_degenerate_mesh_uv_tangent_is_finite() {
    TriangleMesh mesh;
    mesh.vertices = {Point3(0, 0, 0), Point3(1, 0, 0), Point3(0, 0, -1)};
    mesh.indices = {0, 1, 2};
    mesh.normals = {Vec3(0, 1, 0), Vec3(0, 1, 0), Vec3(0, 1, 0)};
    mesh.uvs = {Vec2(0, 0), Vec2(0, 0), Vec2(0, 0)};
    mesh.material_per_tri = {nullptr};

    Ray ray(Point3(0.25, 1, -0.25), Vec3(0, -1, 0));
    HitRecord rec;
    check(mesh.hit(ray, 0.001, infinity, rec), "ray should hit degenerate-UV mesh");
    check(rec.has_tangent, "mesh with UVs should provide tangent");
    check(finite_vec(rec.tangent) && rec.tangent.length_squared() > 0, "degenerate mesh UV tangent must be finite and non-zero");
}

void test_obj_loader_handles_mixed_missing_attributes() {
    std::filesystem::path path = std::filesystem::temp_directory_path() / "rt_mixed_attributes.obj";
    {
        std::ofstream out(path);
        out << "v 0 0 0\n"
            << "v 1 0 0\n"
            << "v 0 1 0\n"
            << "v 1 1 0\n"
            << "vt 0 0\n"
            << "vn 0 0 1\n"
            << "f 1/1/1 2/1/1 3/1/1\n"
            << "f 2 4 3\n";
    }

    TriangleMesh mesh = load_obj_mesh(path.string(), Mat4::identity());
    std::filesystem::remove(path);

    check(mesh.indices.size() == 6, "mixed-attribute OBJ should load two triangles");
    check(mesh.vertices.size() == 6, "OBJ loader should expand mixed-attribute vertices per triangle");
    check(mesh.normals.size() == mesh.vertices.size(), "OBJ loader should provide safe normal data for every expanded vertex");
    check(mesh.uvs.empty() || mesh.uvs.size() == mesh.vertices.size(), "OBJ loader UV data must be empty or complete");
}

void test_obj_loader_reads_mtl_diffuse_and_texture() {
    ObjMeshData mesh = load_model_mesh("models/obj/mtl_test.obj");
    check(mesh.materials.size() == 1, "OBJ MTL fixture should load one material");
    if (!mesh.materials.empty()) {
        check(near_vec(mesh.materials[0].albedo, Color(0.25, 0.5, 0.75)),
              "Kd should map to loaded material albedo");
        check(mesh.materials[0].base_color_texture == 0,
              "map_Kd should map to base color texture index");
        check(!mesh.textures.empty(), "map_Kd should create a loaded texture entry");
    }
}

const LoadedMaterialData* find_loaded_material(const ObjMeshData& mesh, const std::string& name) {
    for (const LoadedMaterialData& material : mesh.materials) {
        if (material.name == name) return &material;
    }
    return nullptr;
}

void test_obj_loader_reads_extended_mtl_fields() {
    ObjMeshData mesh = load_model_mesh("models/obj/mtl_extended.obj");
    check(mesh.materials.size() == 3, "extended OBJ MTL fixture should load three materials");

    const LoadedMaterialData* glow = find_loaded_material(mesh, "glow");
    check(glow != nullptr, "extended OBJ MTL should load emissive material");
    if (glow) {
        check(near_vec(glow->emissive, Color(2.0, 1.0, 0.5)),
              "OBJ MTL Ke should map to emissive color");
        check(glow->emissive_texture >= 0,
              "OBJ MTL map_Ke should map to emissive texture, including map options");
    }

    const LoadedMaterialData* glassy = find_loaded_material(mesh, "glassy");
    check(glassy != nullptr, "extended OBJ MTL should load transparent material");
    if (glassy) {
        check(glassy->alpha_blend, "OBJ MTL d less than one should enable alpha blend");
        check(near(glassy->alpha, 0.35), "OBJ MTL d should map to alpha");
        check(near(glassy->ior, 1.45), "OBJ MTL Ni should map to IOR");
    }

    const LoadedMaterialData* shiny = find_loaded_material(mesh, "shiny");
    check(shiny != nullptr, "extended OBJ MTL should load specular material");
    if (shiny) {
        check(shiny->metallic > 0.85, "OBJ MTL Ks should map strong specular materials toward metal");
        check(shiny->roughness < 0.1, "OBJ MTL Ns should map high shininess to low roughness");
    }
}

void test_obj_loader_ignores_missing_mtl_file() {
    try {
        ObjMeshData mesh = load_model_mesh("models/obj/mark.obj");
        check(!mesh.triangles.empty(), "OBJ with missing MTL should still load geometry");
    } catch (const std::exception& e) {
        check(false, std::string("OBJ with missing MTL should not fail: ") + e.what());
    }
}

void test_triangle_mesh_exposes_internal_acceleration() {
    TriangleMesh mesh;
    for (int i = 0; i < 8; ++i) {
        double x = static_cast<double>(i) * 2.0;
        mesh.vertices.push_back(Point3(x, 0, 0));
        mesh.vertices.push_back(Point3(x + 1, 0, 0));
        mesh.vertices.push_back(Point3(x, 1, 0));
        mesh.indices.push_back(i * 3 + 0);
        mesh.indices.push_back(i * 3 + 1);
        mesh.indices.push_back(i * 3 + 2);
        mesh.material_per_tri.push_back(nullptr);
    }

    Ray ray(Point3(0.25, 0.25, 1), Vec3(0, 0, -1));
    HitRecord rec;
    check(mesh.hit(ray, 0.001, infinity, rec), "ray should hit accelerated mesh");

    check_acceleration_node_count(mesh);
}

void test_pbr_exposes_brdf_and_pdf_for_direct_lighting() {
    auto albedo = std::make_shared<SolidColorTexture>(Color(0.8, 0.7, 0.6));
    PBR pbr(albedo, 0.0, 0.45);

    HitRecord rec;
    rec.p = Point3(0, 0, 0);
    rec.normal = Vec3(0, 1, 0);
    rec.tangent = Vec3(1, 0, 0);
    rec.has_tangent = true;
    rec.u = 0.5;
    rec.v = 0.5;

    Ray incoming(Point3(0, 1, 1), Vec3(0, -1, -1).normalized());
    Ray outgoing(rec.p, Vec3(0, 1, 1).normalized());

    Color brdf = pbr.f(incoming, outgoing, rec);
    double pdf = pbr.pdf(incoming, outgoing, rec);

    check(!pbr.is_specular(), "PBR should use the non-delta BRDF path for direct light and MIS");
    check(pdf > 0 && std::isfinite(pdf), "PBR pdf should be positive for a valid outgoing direction");
    check(finite_vec(brdf) && positive_color(brdf), "PBR BRDF should be finite and positive for a lit direction");
}

void test_display_color_exposure_and_tone_mapping() {
    ImageOutputOptions opts;
    opts.exposure = 2.0;
    opts.tone_map = ToneMapMode::Reinhard;
    Color out = to_display_color(Color(1.0, 0.5, 0.0), 1, opts);

    check(out.x > out.y, "tone mapped red channel should remain brighter than green");
    check(out.x < 1.0 && out.y < 1.0, "tone mapped display color should stay below one");
    check(out.z == 0.0, "zero input channel should stay zero");
}

void test_output_format_detection() {
    check(output_format_for_path("image.ppm") == ImageOutputFormat::PPM,
          "ppm extension should select PPM output");
    check(output_format_for_path("image.png") == ImageOutputFormat::PNG,
          "png extension should select PNG output");
    check(output_format_for_path("image.unknown") == ImageOutputFormat::PPM,
          "unknown extension should preserve old PPM behavior");
}

void test_loaded_material_emissive_routes_to_emissive() {
    Scene scene;
    ObjMeshData mesh;
    LoadedMaterialData data;
    data.emissive = Color(2, 3, 4);

    Material* mat = add_loaded_material(mesh, data, scene);
    check(mat->is_emissive(), "GLB emissiveFactor should route to Emissive material");
}

void test_loaded_glb_pbr_uses_metallic_roughness_and_normal_textures() {
    Scene scene;
    ObjMeshData mesh;
    LoadedTextureData mr_texture;
    mr_texture.path = "textures/mtl_test.ppm";
    mesh.textures.push_back(mr_texture);
    LoadedTextureData normal_texture;
    normal_texture.path = "textures/mtl_test.ppm";
    mesh.textures.push_back(normal_texture);

    LoadedMaterialData data;
    data.use_pbr = true;
    data.albedo = Color(0.8, 0.7, 0.6);
    data.metallic = 0.1;
    data.roughness = 0.2;
    data.metallic_roughness_texture = 0;
    data.normal_texture = 1;

    Material* mat = add_loaded_material(mesh, data, scene);
    auto* pbr = dynamic_cast<PBR*>(mat);
    check(pbr != nullptr, "GLB PBR material should route to PBR material");
    if (pbr) {
        HitRecord rec;
        rec.u = 0.0;
        rec.v = 0.0;
        rec.p = Point3(0, 0, 0);
        check(near(pbr->roughness->value(rec.u, rec.v, rec.p).x, 128.0 / 255.0, 1e-6),
              "GLB metallicRoughnessTexture green channel should drive roughness");
        check(near(pbr->metallic->value(rec.u, rec.v, rec.p).x, 192.0 / 255.0, 1e-6),
              "GLB metallicRoughnessTexture blue channel should drive metallic");
        check(pbr->has_normal_map, "GLB normalTexture should enable PBR normal map");
    }
}

void test_loaded_glb_pbr_emissive_texture_preserves_surface_shading() {
    Scene scene;
    ObjMeshData mesh;
    LoadedTextureData texture;
    texture.path = "textures/mtl_test.ppm";
    mesh.textures.push_back(texture);

    LoadedMaterialData data;
    data.use_pbr = true;
    data.albedo = Color(0.8, 0.7, 0.6);
    data.metallic = 0.0;
    data.roughness = 0.5;
    data.emissive = Color(2, 3, 4);
    data.emissive_texture = 0;

    Material* mat = add_loaded_material(mesh, data, scene);
    auto* pbr = dynamic_cast<PBR*>(mat);
    check(pbr != nullptr, "GLB PBR material with emissiveTexture should preserve PBR shading");
    check(!mat->is_emissive(), "GLB PBR material with emissiveTexture should not become a pure area light");
    if (pbr) {
        HitRecord rec;
        rec.u = 0.0;
        rec.v = 0.0;
        rec.p = Point3(0, 0, 0);
        check(near_vec(pbr->base_color(rec), Color(0.8, 0.7, 0.6), 1e-6),
              "GLB PBR material should keep its base color while adding emission");
        check(near_vec(pbr->emitted(rec),
                       Color(2 * 64.0 / 255.0, 3 * 128.0 / 255.0, 4 * 192.0 / 255.0), 1e-6),
              "GLB emissiveTexture should modulate emissiveFactor");
    }
}

void test_loaded_glb_volume_attenuation_reaches_dielectric() {
    Scene scene;
    ObjMeshData mesh;
    LoadedMaterialData data;
    data.transmission = 1.0;
    data.albedo = Color(1, 1, 1);
    data.ior = 1.4;
    data.attenuation_color = Color(0.25, 0.5, 1.0);
    data.attenuation_distance = 2.0;

    Material* mat = add_loaded_material(mesh, data, scene);
    auto* dielectric = dynamic_cast<Dielectric*>(mat);
    check(dielectric != nullptr, "GLB transmission material should route to Dielectric");
    if (dielectric) {
        HitRecord rec;
        rec.p = Point3(0, 0, 0);
        rec.normal = Vec3(0, 1, 0);
        rec.front_face = false;
        rec.t = 2.0;
        Color attenuation, emission;
        Ray scattered;
        dielectric->scatter(Ray(Point3(0, 0, 0), Vec3(0, 1, 0)), rec, attenuation, scattered, emission);
        check(near_vec(attenuation, Color(0.25, 0.5, 1.0), 1e-6),
              "GLB KHR_materials_volume attenuation should affect dielectric attenuation inside the medium");
    }
}

void test_json_dielectric_accepts_volume_attenuation() {
    Scene scene;
    JsonValue material;
    material.type = JsonValue::Object;
    material.objVal["type"] = string_value("dielectric");
    material.objVal["ior"] = number(1.333);
    material.objVal["albedo"] = array3(1.0, 1.0, 1.0);
    material.objVal["attenuation_color"] = array3(0.4, 0.7, 1.0);
    material.objVal["attenuation_distance"] = number(3.0);

    Material* mat = parse_material(material, scene);
    auto* dielectric = dynamic_cast<Dielectric*>(mat);
    check(dielectric != nullptr, "JSON dielectric material should parse attenuation fields");
    if (dielectric) {
        HitRecord rec;
        rec.p = Point3(0, 0, 0);
        rec.normal = Vec3(0, 1, 0);
        rec.front_face = false;
        rec.t = 3.0;
        Color attenuation, emission;
        Ray scattered;
        dielectric->scatter(Ray(Point3(0, 0, 0), Vec3(0, 1, 0)), rec, attenuation, scattered, emission);
        check(near_vec(attenuation, Color(0.4, 0.7, 1.0), 1e-6),
              "JSON dielectric attenuation should tint rays exiting the medium");
    }
}

void test_random_seed_repeats_sequence() {
    set_random_seed(1234);
    double a = random_double();
    double b = random_double();
    set_random_seed(1234);
    check(near(a, random_double()), "first random sample should repeat after seed reset");
    check(near(b, random_double()), "second random sample should repeat after seed reset");
}

void test_random_double_stays_in_unit_interval() {
    set_random_seed(4321);
    for (int i = 0; i < 1000; i++) {
        double v = random_double();
        check(v >= 0.0 && v < 1.0, "random_double should stay in [0, 1)");
    }
}

void test_collect_emissive_objects_uses_only_emissive_mesh_triangles() {
    Scene scene;

    auto diffuse = std::make_unique<Lambertian>(Color(0.2, 0.2, 0.2));
    Material* diffuse_ptr = diffuse.get();
    scene.materials.push_back(std::move(diffuse));

    auto emissive = std::make_unique<Emissive>(Color(4, 3, 2));
    Material* emissive_ptr = emissive.get();
    scene.materials.push_back(std::move(emissive));

    auto mesh = std::make_unique<TriangleMesh>();
    mesh->vertices = {
        Point3(0, 0, 0), Point3(1, 0, 0), Point3(0, 1, 0),
        Point3(0, 0, 1), Point3(0, 2, 1), Point3(2, 0, 1)
    };
    mesh->indices = {0, 1, 2, 3, 4, 5};
    mesh->material_per_tri = {diffuse_ptr, emissive_ptr};

    scene.primitives.add(mesh.get());
    scene.objects.push_back(std::move(mesh));

    collect_emissive_objects(scene);

    check(scene.emissive_objects.size() == 1, "mixed mesh should create one emissive sampling subset");
    if (!scene.emissive_objects.empty()) {
        check(near(scene.emissive_objects[0].geometry->area(), 2.0),
              "emissive mesh sampling area should exclude non-emissive triangles");
        check(near_vec(scene.emissive_objects[0].emission, Color(4, 3, 2)),
              "emissive mesh subset should keep the emissive material color");
        check(near(scene.emissive_total_area, 2.0),
              "scene should precompute total emissive sampling area");
    }
}

void test_mirror_glass_water_acceptance_scene_loads() {
    Scene scene;
    load_scene("scenes/mirror_glass_water.json", scene);

    check(scene.width == 640 && scene.height == 360,
          "mirror/glass/water acceptance scene should use preview-sized image settings");
    check(scene.primitive_count >= 7,
          "mirror/glass/water acceptance scene should load several primitives");
    check(!scene.emissive_objects.empty(),
          "mirror/glass/water acceptance scene should include at least one emissive area light");
}

}  // namespace

int main() {
    test_parse_transform_uses_srt_order();
    test_transform_normal_matches_rotation_direction();
    test_sphere_pole_tangent_is_finite();
    test_degenerate_triangle_uv_tangent_is_finite();
    test_degenerate_mesh_uv_tangent_is_finite();
    test_obj_loader_handles_mixed_missing_attributes();
    test_obj_loader_reads_mtl_diffuse_and_texture();
    test_obj_loader_reads_extended_mtl_fields();
    test_obj_loader_ignores_missing_mtl_file();
    test_triangle_mesh_exposes_internal_acceleration();
    test_pbr_exposes_brdf_and_pdf_for_direct_lighting();
    test_display_color_exposure_and_tone_mapping();
    test_output_format_detection();
    test_loaded_material_emissive_routes_to_emissive();
    test_loaded_glb_pbr_uses_metallic_roughness_and_normal_textures();
    test_loaded_glb_pbr_emissive_texture_preserves_surface_shading();
    test_loaded_glb_volume_attenuation_reaches_dielectric();
    test_json_dielectric_accepts_volume_attenuation();
    test_random_seed_repeats_sequence();
    test_random_double_stays_in_unit_interval();
    test_collect_emissive_objects_uses_only_emissive_mesh_triangles();
    test_mirror_glass_water_acceptance_scene_loads();

    if (failures != 0) {
        std::cerr << failures << " regression test(s) failed\n";
        return 1;
    }
    std::cout << "All regression tests passed\n";
    return 0;
}
