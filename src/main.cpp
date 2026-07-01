// Module D: main -- CLI + JSON scene loading + render loop
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "raytracer/render/image.h"
#include "raytracer/render/renderer.h"
#include "raytracer/scene/scene.h"
#include <filesystem>
#include <iostream>
#include <string>

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "  --scene <path>     scene JSON file (default: scenes/default.json)\n"
              << "  --model <path>     OBJ/GLB model path (uses scenes/obj.json if --scene is omitted)\n"
              << "  --obj <path>       alias of --model\n"
              << "  --out <path>       output PPM file (overrides scene)\n"
              << "  --samples <n>      samples per pixel (overrides scene)\n"
              << "  --threads <n>      render worker threads (default: hardware concurrency)\n"
              << "  --direct-only      disable recursive random bounces, use direct light + shadows only\n"
              << "  --preview          fast preview mode: direct-only and samples=1 unless overridden\n";
}

int main(int argc, char* argv[]) {
    std::string scene_path = "scenes/default.json";
    bool scene_specified = false;
    std::string out_override;
    std::string model_override;
    int samples_override = -1;
    RenderOptions render_options;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--scene" && i + 1 < argc) {
            scene_path = argv[++i];
            scene_specified = true;
        } else if ((arg == "--model" || arg == "--obj") && i + 1 < argc) {
            model_override = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            out_override = argv[++i];
        } else if (arg == "--samples" && i + 1 < argc) {
            samples_override = std::stoi(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            render_options.threads = std::stoi(argv[++i]);
            if (render_options.threads <= 0) {
                std::cerr << "--threads must be greater than 0\n";
                return 1;
            }
        } else if (arg == "--direct-only") {
            render_options.direct_only = true;
        } else if (arg == "--preview") {
            render_options.preview = true;
            render_options.direct_only = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!model_override.empty() && !scene_specified) {
        scene_path = "scenes/obj.json";
    }

    SceneLoadOptions load_options;
    if (!model_override.empty()) {
        std::filesystem::path model_path(model_override);
        if (model_path.is_relative()) {
            model_path = std::filesystem::absolute(model_path);
        }
        load_options.model_override = model_path.lexically_normal().string();
    }

    Scene scene;
    try {
        load_scene(scene_path, scene, load_options);
    } catch (const std::exception& e) {
        std::cerr << "Error loading scene: " << e.what() << "\n";
        return 1;
    }

    if (!out_override.empty()) scene.output = out_override;
    if (samples_override > 0)  {
        scene.samples = samples_override;
    } else if (render_options.preview) {
        scene.samples = 1;
    }

    int thread_count = resolve_thread_count(scene, render_options);

    std::cout << "Scene: " << scene_path << "\n"
              << "Model: " << (model_override.empty() ? "(scene)" : load_options.model_override) << "\n"
              << "Image: " << scene.width << "x" << scene.height
              << ", samples=" << scene.samples
              << ", depth=" << scene.max_depth
              << ", mode=" << (render_options.direct_only ? "direct-only" : "path-tracing")
              << ", threads=" << thread_count << "\n"
              << "Primitives: " << scene.primitive_count << "\n";

    RenderCallbacks callbacks;
    callbacks.progress = [](double progress) {
        int pct = static_cast<int>(progress * 100.0);
        std::cerr << "\rProgress: " << pct << "%" << std::flush;
    };
    RenderOutput output = render_scene(scene, render_options, callbacks);
    std::cerr << "\rProgress: 100%\n";

    write_ppm(scene.output, output.width, output.height, output.pixels, output.samples);
    std::cout << "Wrote " << scene.output << "\n";
    return 0;
}
