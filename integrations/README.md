# Integrations

这里统一放置面向外部编辑器、DCC 工具或宿主软件的接入代码。

每个子目录对应一种接入点，例如：

- `blender/`：Blender RenderEngine 插件和使用说明。

这些接入点应复用项目的核心渲染能力，而不是复制一份独立渲染逻辑。原有命令行入口仍保留在项目根目录和 `src/main.cpp` 路径中。
