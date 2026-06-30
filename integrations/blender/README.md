# Blender 渲染器接入说明

本目录提供 Blender RenderEngine 插件，让 Blender 可以直接使用本项目的光线追踪器渲染当前场景。

这个入口不会替代或破坏原有命令行渲染路径；它只是新增了一条 Blender 路径。Blender 插件会读取当前场景的 evaluated mesh、活动摄像机、灯光和基础材质，然后交给本项目的 `raytracer_blender_bridge` 可执行文件渲染。

## 版本

当前按 Blender 5.1.2 进行适配和验证。其他 5.x 版本大概率可用，但界面菜单名称可能略有差异。

## 构建

在项目根目录执行：

```bash
./build.sh
```

构建完成后，根目录会生成：

```text
./raytracer
./raytracer_blender_bridge
```

其中 `raytracer` 是原有命令行渲染器，`raytracer_blender_bridge` 是 Blender 本地模式使用的桥接程序。

## 安装插件

在 Blender 中文界面中：

1. 打开 `编辑 > 偏好设置`。
2. 进入 `插件`。
3. 点击右上角的下拉菜单，选择 `从磁盘安装...`。
4. 选择 `integrations/blender/raytracer_addon.py`。
5. 启用 `Raytracer Renderer`。

启用后，在 `渲染属性` 中把 `渲染引擎` 切换为 `Raytracer`。

## 本地模式渲染

在 `渲染属性 > Raytracer` 面板中确认：

- `后端` 选择 `本地`。
- `Bridge` 指向项目根目录里的 `raytracer_blender_bridge`。
- 根据需要调整 `采样数`、`最大深度`、`线程数` 和 `仅直接光照`。

然后按 `F12` 或使用 Blender 的渲染菜单即可渲染当前场景。

如果插件是直接从仓库中的 `integrations/blender/raytracer_addon.py` 安装的，通常会自动找到根目录的 bridge。若找不到，请手动把 `Bridge` 设置为绝对路径，例如：

```text
/Users/your-name/develop/raytracer/raytracer_blender_bridge
```

## 参数说明

| 设置 | 说明 |
|------|------|
| `后端` | 当前可用的是 `本地`；`远程` 只是预留接口，暂未实现网络渲染服务 |
| `Bridge` | 本地桥接程序 `raytracer_blender_bridge` 的路径 |
| `远程地址` | 为未来远程渲染服务预留；当前不会真正连接服务器 |
| `采样数` | 每像素采样次数。完整路径追踪下越高越干净，但渲染越慢 |
| `最大深度` | 光线递归反弹深度上限 |
| `线程数` | C++ 渲染端使用的 CPU 线程数 |
| `仅直接光照` | 只计算相机射线、直接光照、阴影和环境光，不做递归随机反弹；适合快速预览 |

## 当前支持

- 使用 Blender 当前活动摄像机。
- 读取 evaluated mesh，包括 mesh、curve、font、meta 等对象转出的网格。
- 支持基础 point / sun 灯光。
- area light 目前近似为点光源，不是真正的面光源采样。
- 近似映射 Blender Principled BSDF 的基础色、metallic、roughness、alpha 和 emission。
- 使用本地 subprocess 模式调用 C++ bridge。
- 渲染进度通过 bridge 的 stderr `PROGRESS` 文本消息汇报给 Blender。

## 当前限制

- 不支持完整 Blender 节点材质图。
- 不支持复杂贴图网络。
- 不支持真正的面光源采样和体光源。
- 不支持交互式 viewport path tracing。
- `远程` 后端还未实现，只是为后续远程 CUDA 渲染服务预留的配置入口。

## 降噪建议

如果画面噪点很多：

- 预览时可以开启 `仅直接光照`，画面会更干净，但不会有完整全局光照。
- 最终渲染可以提高 `采样数`，例如 128、256 或更高。
- 当前渲染器还没有 denoiser、MIS 和显式光源重要性采样，所以 8 或 32 samples 在完整路径追踪下通常仍会明显有噪点。

## 故障排查

如果 Blender 提示找不到 bridge：

1. 确认已经在项目根目录执行过 `./build.sh`。
2. 确认根目录存在 `raytracer_blender_bridge`。
3. 在 `渲染属性 > Raytracer > Bridge` 中手动选择该可执行文件。

如果切到 `远程` 后端后渲染失败，这是当前预期行为；远程通信服务还没有实现。
