# NEXT Engine

NEXT 是一个面向“两宋项目”的自研引擎仓库。当前状态不是空壳，也还不是完整生产级引擎；更准确地说，它已经具备可构建、可测试、可启动 demo 和编辑器的工作底座。

## 当前可用能力

- 运行时：基础 ECS、`Query::ForEach`、资源加载、任务系统存档/事件驱动、世界流送主路径
- 工具：`next_assetc`、`next_editor` 的 Content / Import / live backbuffer Viewport
- 程序：`song_demo`、`next_editor`、`next_task`
- 测试：`ctest -C Debug --output-on-failure`

## 当前仍是原型或部分实现

- 高阶渲染特性（GI / RT / VXGI 等）仍有 placeholder
- 编辑器还没有独立 render-to-texture 场景视口
- 脚本系统支持 `stub` 和可选 system Lua 两态，但没有复杂绑定、热重载依赖图或完整沙箱

## 快速开始

### 配置与构建

```powershell
cmake -S . -B build -G "Visual Studio 18" -A x64
cmake --build build --config Debug
```

如需启用真实 Lua VM，并且本机已经安装 system Lua：

```powershell
cmake -S . -B build -G "Visual Studio 18" -A x64 -DUSE_SYSTEM_LUA=ON
cmake --build build --config Debug
```

如果没找到 Lua，脚本系统会自动回退到 stub mode，不会阻断构建。

### 运行

```powershell
.\build\bin\Debug\song_demo.exe
.\build\bin\Debug\next_editor.exe
```

推荐的 smoke 命令：

```powershell
.\build\bin\Debug\song_demo.exe --smoke-frames 5
.\build\bin\Debug\song_demo.exe --run-self-tests --smoke-frames 1
.\build\bin\Debug\next_editor.exe --smoke-frames 5 --no-imgui
.\build\bin\Debug\next_editor.exe --load-package assets\test_package.npkg --smoke-frames 5 --no-imgui
ctest --test-dir build -C Debug --output-on-failure
```

## 入口文档

- `docs/README.md`：当前仓库状态与文档索引
- `docs/EDITOR_QUICKSTART.md`：编辑器用法与当前限制
- `docs/ASSET_PIPELINE_QUICKSTART.md`：资产导入与打包流程
