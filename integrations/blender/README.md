# Blender 渲染器接入说明

本目录提供 Blender RenderEngine 插件，让 Blender 可以直接使用本项目的光线追踪器渲染当前场景。

这个入口不会替代或破坏原有命令行渲染路径；它只是新增了一条 Blender 路径。Blender 插件会读取当前场景的 evaluated mesh、活动摄像机、灯光和基础材质，然后交给本项目的本地 bridge 或远程 HTTP server 渲染。

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
./raytracer_server
```

其中 `raytracer` 是原有命令行渲染器，`raytracer_blender_bridge` 是 Blender 本地模式使用的桥接程序，`raytracer_server` 是远程模式使用的 HTTP 渲染服务。

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

## 远程模式渲染

远程模式需要先在渲染机器上启动 `raytracer_server`。启动参数 `--default-threads` 表示默认线程数：只有当客户端请求没有指定线程数时，server 才会使用这个值。`--max-request-threads` 表示 server 接受的单次请求最大线程数；如果 Blender 请求更高的线程数，server 会按这个上限渲染。例如在服务器上执行：

```bash
./raytracer_server --host 0.0.0.0 --port 8080 --default-threads 16 --max-request-threads 16
```

如果只在同一台机器上测试，也可以监听本地地址：

```bash
./raytracer_server --host 127.0.0.1 --port 8080 --default-threads 8 --max-request-threads 8
```

然后在 Blender 的 `渲染属性 > Raytracer` 面板中：

- `后端` 选择 `远程`。
- `远程地址` 填写服务器地址，例如 `http://192.168.1.10:8080`。
- 根据需要调整 `采样数`、`最大深度`、`线程数` 和 `仅直接光照`。这里的 `线程数` 是本次渲染请求使用的线程数；如果客户端不传，server 才会使用启动时的 `--default-threads`；如果超过 server 的 `--max-request-threads`，server 会按最大值执行。

插件会把当前 Blender 场景导出为内部 JSON 包，通过 `POST /jobs` 创建远程渲染任务；随后轮询任务进度，渲染完成后下载 RGBA32F 图像并显示。如果在 Blender 中取消渲染，插件会向 server 发送取消请求，server 会尽快停止对应任务。

也可以用命令行快速检查服务是否可用：

```bash
curl http://127.0.0.1:8080/health
```

远程渲染接口：

```text
POST /jobs?threads=8&direct_only=0
Content-Type: application/json

<Blender 插件导出的内部场景 JSON>
```

成功时返回：

```json
{"job_id":"..."}
```

随后可查询和控制任务：

```text
GET  /jobs/<job_id>/progress
GET  /jobs/<job_id>/result
POST /jobs/<job_id>/cancel
```

`/result` 在任务完成后返回 `application/octet-stream`，内容是本项目的 `RTRGBAF1` RGBA32F 二进制图像。`POST /render` 作为同步调试接口仍保留，但 Blender 插件默认使用任务式接口。

## 参数说明

| 设置 | 说明 |
|------|------|
| `后端` | `本地` 使用本机 bridge 子进程；`远程` 通过 HTTP 连接 `raytracer_server` |
| `Bridge` | 本地桥接程序 `raytracer_blender_bridge` 的路径 |
| `远程地址` | 远程 `raytracer_server` 的 HTTP 地址 |
| `远程超时` | 等待远程渲染完成的超时时间，单位为秒 |
| `采样数` | 每像素采样次数。完整路径追踪下越高越干净，但渲染越慢 |
| `最大深度` | 光线递归反弹深度上限 |
| `线程数` | 本次渲染请求让 C++ 渲染端使用的 CPU 线程数；远程模式下会随请求发给 server |
| `仅直接光照` | 只计算相机射线、直接光照、阴影和环境光，不做递归随机反弹；适合快速预览 |

## 当前支持

- 使用 Blender 当前活动摄像机。
- 读取 evaluated mesh，包括 mesh、curve、font、meta 等对象转出的网格。
- 支持基础 point / sun 灯光。
- area light 目前近似为点光源，不是真正的面光源采样。
- 近似映射 Blender Principled BSDF 的基础色、metallic、roughness、alpha 和 emission。
- 使用本地 subprocess 模式调用 C++ bridge。
- 使用远程 HTTP 模式调用 `raytracer_server`。
- 本地渲染进度通过 bridge 的 stderr `PROGRESS` 文本消息汇报给 Blender。
- 远程渲染进度通过 job progress 接口汇报给 Blender，并支持取消远程任务。

## 当前限制

- 不支持完整 Blender 节点材质图。
- 不支持复杂贴图网络。
- 不支持真正的面光源采样和体光源。
- 不支持交互式 viewport path tracing。
- 远程模式当前使用轮询查询进度，不是 WebSocket/SSE 流式推送。
- 远程任务当前存储在 server 内存中，没有持久化任务队列或鉴权。
- `raytracer_server` 当前仍调用 CPU 渲染核心；CUDA/GPU 后端尚未实现。

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

如果切到 `远程` 后端后渲染失败：

1. 确认服务器上已经启动 `raytracer_server`。
2. 用 `curl http://server:port/health` 检查网络是否可达。
3. 确认 Blender 中的 `远程地址` 包含协议和端口，例如 `http://192.168.1.10:8080`。
4. 如果场景很大，可以适当调大 `远程超时` 和 server 的 `--max-body-mb`。
