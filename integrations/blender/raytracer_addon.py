bl_info = {
    "name": "Raytracer Renderer",
    "author": "Raytracer Course Project",
    "version": (0, 1, 0),
    "blender": (5, 1, 0),
    "location": "Render Properties > Render Engine",
    "description": "Render the current Blender scene with the course raytracer",
    "category": "Render",
}

import array
import json
import math
import os
import struct
import subprocess
import tempfile
from pathlib import Path

import bpy
from bpy.props import BoolProperty, EnumProperty, IntProperty, PointerProperty, StringProperty
from mathutils import Vector


ENGINE_ID = "RAYTRACER"
PROTOCOL_VERSION = 1
GEOMETRY_TYPES = {"MESH", "CURVE", "SURFACE", "FONT", "META"}


def rt_point(v):
    return [float(v.x), float(v.z), float(-v.y)]


def rt_vector(v):
    return [float(v.x), float(v.z), float(-v.y)]


def socket_by_name(node, names):
    for name in names:
        socket = node.inputs.get(name)
        if socket is not None:
            return socket
    return None


def socket_float(node, names, default):
    socket = socket_by_name(node, names)
    if socket is None or socket.is_linked:
        return float(default)
    value = socket.default_value
    try:
        return float(value)
    except TypeError:
        return float(default)


def socket_color(node, names, default):
    socket = socket_by_name(node, names)
    if socket is None or socket.is_linked:
        return [float(default[0]), float(default[1]), float(default[2])]
    value = socket.default_value
    return [float(value[0]), float(value[1]), float(value[2])]


def material_to_rt(material):
    if material is None:
        return {"type": "pbr", "albedo": [0.8, 0.8, 0.8], "metallic": 0.0, "roughness": 0.55}

    base = [float(material.diffuse_color[0]), float(material.diffuse_color[1]), float(material.diffuse_color[2])]
    alpha = float(material.diffuse_color[3]) if len(material.diffuse_color) > 3 else 1.0
    metallic = 0.0
    roughness = 0.55
    emission = [0.0, 0.0, 0.0]
    emission_strength = 0.0

    if material.use_nodes and material.node_tree is not None:
        principled = None
        for node in material.node_tree.nodes:
            if node.type == "BSDF_PRINCIPLED":
                principled = node
                break
        if principled is not None:
            base = socket_color(principled, ("Base Color",), base)
            alpha = socket_float(principled, ("Alpha",), alpha)
            metallic = socket_float(principled, ("Metallic",), metallic)
            roughness = socket_float(principled, ("Roughness",), roughness)
            emission = socket_color(principled, ("Emission Color", "Emission"), emission)
            emission_strength = socket_float(principled, ("Emission Strength",), emission_strength)

    if emission_strength > 0.0:
        return {
            "type": "emissive",
            "emission": [
                float(emission[0] * emission_strength),
                float(emission[1] * emission_strength),
                float(emission[2] * emission_strength),
            ],
        }

    if alpha < 0.35:
        return {"type": "dielectric", "ior": 1.5}

    return {
        "type": "pbr",
        "albedo": base,
        "metallic": float(max(0.0, min(1.0, metallic))),
        "roughness": float(max(0.001, min(1.0, roughness))),
    }


def export_camera(scene, depsgraph, width, height):
    camera = scene.camera
    if camera is None:
        raise RuntimeError("Scene has no active camera")

    camera_eval = camera.evaluated_get(depsgraph)
    matrix = camera_eval.matrix_world
    location = matrix.to_translation()
    forward = matrix.to_quaternion() @ Vector((0.0, 0.0, -1.0))
    up = matrix.to_quaternion() @ Vector((0.0, 1.0, 0.0))

    lookfrom = Vector(rt_point(location))
    lookat = lookfrom + Vector(rt_vector(forward))
    vup = Vector(rt_vector(up))
    vfov = math.degrees(camera_eval.data.angle_y)
    focus_dist = camera_eval.data.dof.focus_distance
    if focus_dist <= 0.0:
        focus_dist = 1.0

    return {
        "lookfrom": [float(lookfrom.x), float(lookfrom.y), float(lookfrom.z)],
        "lookat": [float(lookat.x), float(lookat.y), float(lookat.z)],
        "vup": [float(vup.x), float(vup.y), float(vup.z)],
        "vfov": float(vfov),
        "aperture": float(camera_eval.data.dof.aperture_fstop if camera_eval.data.dof.use_dof else 0.0),
        "focus_dist": float(focus_dist),
    }


def export_lights(depsgraph):
    lights = []
    seen = set()
    for instance in depsgraph.object_instances:
        obj = instance.object
        if obj.type != "LIGHT":
            continue
        key = (obj.original.as_pointer(), tuple(round(x, 8) for row in instance.matrix_world for x in row))
        if key in seen:
            continue
        seen.add(key)

        data = obj.data
        matrix = instance.matrix_world
        color = [float(data.color[0]), float(data.color[1]), float(data.color[2])]
        energy = float(getattr(data, "energy", 1.0))
        if data.type == "POINT":
            lights.append({
                "type": "point",
                "position": rt_point(matrix.to_translation()),
                "color": color,
                "intensity": max(0.0, energy / 100.0),
            })
        elif data.type == "SUN":
            direction = matrix.to_quaternion() @ Vector((0.0, 0.0, -1.0))
            lights.append({
                "type": "directional",
                "direction": rt_vector(direction),
                "color": color,
                "intensity": max(0.0, energy),
            })
        elif data.type == "AREA":
            lights.append({
                "type": "point",
                "position": rt_point(matrix.to_translation()),
                "color": color,
                "intensity": max(0.0, energy / 100.0),
            })
    return lights


def material_for_slot(obj, material_index):
    if 0 <= material_index < len(obj.material_slots):
        return obj.material_slots[material_index].material
    return None


def export_mesh_instance(instance, depsgraph):
    obj = instance.object
    if obj.type not in GEOMETRY_TYPES:
        return []
    original = getattr(obj, "original", obj)
    if getattr(original, "hide_render", False):
        return []

    mesh = obj.to_mesh(preserve_all_data_layers=True, depsgraph=depsgraph)
    if mesh is None:
        return []

    objects = []
    try:
        mesh.calc_loop_triangles()
        uv_layer = mesh.uv_layers.active.data if mesh.uv_layers.active else None
        groups = {}

        for tri in mesh.loop_triangles:
            material_index = int(tri.material_index)
            group = groups.setdefault(material_index, {
                "vertices": [],
                "indices": [],
                "uvs": [] if uv_layer is not None else None,
            })

            for vertex_index, loop_index in zip(tri.vertices, tri.loops):
                world_co = instance.matrix_world @ mesh.vertices[vertex_index].co
                group["vertices"].append(rt_point(world_co))
                group["indices"].append(len(group["indices"]))
                if uv_layer is not None:
                    uv = uv_layer[loop_index].uv
                    group["uvs"].append([float(uv.x), float(uv.y)])

        for material_index, group in groups.items():
            if not group["indices"]:
                continue
            item = {
                "type": "triangles",
                "vertices": group["vertices"],
                "indices": group["indices"],
                "material": material_to_rt(material_for_slot(obj, material_index)),
            }
            if group["uvs"] is not None:
                item["uvs"] = group["uvs"]
            objects.append(item)
    finally:
        obj.to_mesh_clear()

    return objects


def export_scene_package(scene, depsgraph, settings):
    scale = scene.render.resolution_percentage / 100.0
    width = max(1, int(scene.render.resolution_x * scale))
    height = max(1, int(scene.render.resolution_y * scale))

    objects = []
    for instance in depsgraph.object_instances:
        objects.extend(export_mesh_instance(instance, depsgraph))

    world_color = [0.04, 0.04, 0.04]
    if scene.world is not None:
        world_color = [
            float(scene.world.color[0]) * 0.04,
            float(scene.world.color[1]) * 0.04,
            float(scene.world.color[2]) * 0.04,
        ]

    return {
        "protocol": PROTOCOL_VERSION,
        "source": "blender",
        "image": {
            "width": width,
            "height": height,
            "samples": int(settings.samples),
            "max_depth": int(settings.max_depth),
            "output": "blender.ppm",
        },
        "camera": export_camera(scene, depsgraph, width, height),
        "lighting": {"ambient": world_color},
        "lights": export_lights(depsgraph),
        "objects": objects,
    }


class RenderPixels:
    def __init__(self, width, height, pixels):
        self.width = width
        self.height = height
        self.pixels = pixels

    def blender_rect(self):
        rect = []
        row_stride = self.width * 4
        for y in range(self.height - 1, -1, -1):
            row = y * row_stride
            for x in range(self.width):
                i = row + x * 4
                rect.append((self.pixels[i], self.pixels[i + 1], self.pixels[i + 2], self.pixels[i + 3]))
        return rect


class RendererClient:
    def render(self, package, engine, settings):
        raise NotImplementedError


class LocalSubprocessRenderer(RendererClient):
    def __init__(self, bridge_path):
        self.bridge_path = bridge_path

    def render(self, package, engine, settings):
        if not self.bridge_path:
            raise RuntimeError("Raytracer bridge path is empty")
        if not os.path.exists(self.bridge_path):
            raise RuntimeError("Raytracer bridge not found: " + self.bridge_path)

        with tempfile.TemporaryDirectory(prefix="raytracer_blender_") as tmp:
            scene_path = os.path.join(tmp, "scene.rt.json")
            result_path = os.path.join(tmp, "result.rgba32f")
            with open(scene_path, "w", encoding="utf-8") as f:
                json.dump(package, f, separators=(",", ":"))

            cmd = [
                self.bridge_path,
                "--scene", scene_path,
                "--out-float", result_path,
                "--threads", str(max(1, int(settings.threads))),
            ]
            if settings.direct_only:
                cmd.append("--direct-only")

            process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            stderr_lines = []
            while True:
                line = process.stderr.readline()
                if line:
                    stderr_lines.append(line.rstrip())
                    if line.startswith("PROGRESS "):
                        try:
                            engine.update_progress(float(line.split()[1]))
                        except (IndexError, ValueError):
                            pass
                    if engine.test_break():
                        process.terminate()
                        raise RuntimeError("Render cancelled")
                elif process.poll() is not None:
                    break

            stdout = process.stdout.read() if process.stdout else ""
            returncode = process.wait()
            if returncode != 0:
                details = "\n".join(stderr_lines[-12:])
                if stdout.strip():
                    details += "\n" + stdout.strip()
                raise RuntimeError("Raytracer bridge failed\n" + details)

            return read_rgba32f(result_path)


class RemoteHttpRenderer(RendererClient):
    def __init__(self, endpoint):
        self.endpoint = endpoint

    def render(self, package, engine, settings):
        raise RuntimeError("Remote renderer transport is reserved by the interface but not implemented yet")


def read_rgba32f(path):
    with open(path, "rb") as f:
        magic = f.read(8)
        if magic != b"RTRGBAF1":
            raise RuntimeError("Invalid raytracer result file")
        width, height = struct.unpack("<II", f.read(8))
        pixels = array.array("f")
        pixels.fromfile(f, width * height * 4)
        if len(pixels) != width * height * 4:
            raise RuntimeError("Truncated raytracer result file")
        return RenderPixels(width, height, pixels)


def default_bridge_path():
    addon_path = Path(__file__).resolve()
    fallback = addon_path.parent / "raytracer_blender_bridge"
    for root in addon_path.parents:
        candidate = root / "raytracer_blender_bridge"
        if candidate.exists():
            return str(candidate)
        if (root / "CMakeLists.txt").exists() and (root / "src").exists():
            fallback = candidate
    return str(fallback)


def make_client(settings):
    if settings.backend == "LOCAL":
        path = settings.bridge_path.strip() or default_bridge_path()
        return LocalSubprocessRenderer(path)
    return RemoteHttpRenderer(settings.remote_endpoint.strip())


class RaytracerSettings(bpy.types.PropertyGroup):
    backend: EnumProperty(
        name="后端",
        items=[
            ("LOCAL", "本地", "使用本机 raytracer_blender_bridge 可执行文件"),
            ("REMOTE", "远程", "预留网络渲染器客户端接口"),
        ],
        default="LOCAL",
    )
    bridge_path: StringProperty(
        name="桥接程序",
        subtype="FILE_PATH",
        default="",
        description="raytracer_blender_bridge 的路径；留空时使用仓库内默认路径",
    )
    remote_endpoint: StringProperty(
        name="远程地址",
        default="http://localhost:8080",
        description="为未来远程渲染器预留的服务地址",
    )
    samples: IntProperty(name="采样数", default=16, min=1, max=4096)
    max_depth: IntProperty(name="最大深度", default=16, min=1, max=256)
    threads: IntProperty(name="线程数", default=8, min=1, max=128)
    direct_only: BoolProperty(name="仅直接光照", default=False)


class RaytracerRenderEngine(bpy.types.RenderEngine):
    bl_idname = ENGINE_ID
    bl_label = "Raytracer"
    bl_use_preview = False

    def render(self, depsgraph):
        scene = depsgraph.scene
        settings = scene.raytracer_settings
        try:
            package = export_scene_package(scene, depsgraph, settings)
            client = make_client(settings)
            pixels = client.render(package, self, settings)
            if self.test_break():
                return

            result = self.begin_result(0, 0, pixels.width, pixels.height)
            layer = result.layers[0].passes["Combined"]
            layer.rect = pixels.blender_rect()
            self.end_result(result)
            self.update_progress(1.0)
        except Exception as exc:
            message = str(exc)
            self.error_set(message)
            self.report({"ERROR"}, message)


class RENDER_PT_raytracer_settings(bpy.types.Panel):
    bl_label = "Raytracer"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "render"
    COMPAT_ENGINES = {ENGINE_ID}

    def draw(self, context):
        settings = context.scene.raytracer_settings
        layout = self.layout
        layout.prop(settings, "backend")
        if settings.backend == "LOCAL":
            layout.prop(settings, "bridge_path")
        else:
            layout.prop(settings, "remote_endpoint")
        layout.prop(settings, "samples")
        layout.prop(settings, "max_depth")
        layout.prop(settings, "threads")
        layout.prop(settings, "direct_only")


def compatible_panels():
    exclude = {
        "VIEWLAYER_PT_filter",
        "VIEWLAYER_PT_layer_passes",
    }
    panels = []
    for panel in bpy.types.Panel.__subclasses__():
        if hasattr(panel, "COMPAT_ENGINES") and "BLENDER_RENDER" in panel.COMPAT_ENGINES:
            if panel.__name__ not in exclude:
                panels.append(panel)
    return panels


classes = (
    RaytracerSettings,
    RaytracerRenderEngine,
    RENDER_PT_raytracer_settings,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.types.Scene.raytracer_settings = PointerProperty(type=RaytracerSettings)
    for panel in compatible_panels():
        panel.COMPAT_ENGINES.add(ENGINE_ID)


def unregister():
    for panel in compatible_panels():
        if ENGINE_ID in panel.COMPAT_ENGINES:
            panel.COMPAT_ENGINES.remove(ENGINE_ID)
    del bpy.types.Scene.raytracer_settings
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)


if __name__ == "__main__":
    register()
