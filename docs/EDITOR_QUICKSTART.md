# Editor Quickstart

当前提供一个可运行的编辑器骨架：ImGui Docking UI + DX12 Overlay，目标是尽快把“UE5 水平的开发体验”底座搭起来（面板、内容浏览、导入入口），再逐步深化成完整编辑器。

## 构建

使用现有 `build` 目录即可：

```powershell
cmake --build build --config Debug --target next_editor
```

## 运行

渲染器会按相对路径加载着色器与资源，因此运行时工作目录建议是仓库根目录 `E:\NEXT`。

```powershell
cd E:\NEXT
.\build\bin\Debug\next_editor.exe
```

编辑器菜单栏：
- `Window` 可以重新打开 `Content/Import/Viewport/ImGui Demo` 面板
- `Layout -> Reset Layout` 可一键恢复默认布局（面板卡住/收不回去时用）

当前 Content/Import 处于“工具链连通”阶段：
- `Content` 会显示 `assets/*.npkg`，并标注 `Loaded/Not loaded`
- `Inspect` 后能看到包内资产列表；`Load Asset (Sync)` 会把资产加载到运行时 AssetManager（下一步再接 viewport 预览/拖拽进场景）
- `Import` 会校验源文件是否存在，导入成功后会自动刷新 Content（可选自动 Load）

可选参数（用于自动化/排查问题）：

```powershell
# 只跑渲染器（不初始化 ImGui）
.\build\bin\Debug\next_editor.exe --no-imgui

# 跑 N 帧后自动退出（CI/smoke test）
.\build\bin\Debug\next_editor.exe --smoke-frames 300

# 跑 N 秒后自动退出
.\build\bin\Debug\next_editor.exe --smoke-seconds 3
```

日志级别可通过环境变量控制（默认 Debug/Info 以上，Trace 需要显式开启）：

```powershell
$env:NEXT_LOG_LEVEL = "info"   # trace/debug/info/warn/error/fatal
```

如果你用 Visual Studio 调试，已为 `next_editor` 设置 `VS_DEBUGGER_WORKING_DIRECTORY = repo root`。

## 资产导入

编辑器里有 `Import` 面板，当前调用 `next_assetc import` 来执行导入（先支持 OBJ mesh-only）。

也支持把文件直接拖进编辑器窗口：会自动把 `Import` 面板里的 Source/Output 路径填好（默认输出到 `assets/<name>.npkg`）。

也可以直接命令行：

```powershell
.\build\bin\Debug\next_assetc.exe import SourceAssets\tri.obj assets\tri_import.npkg
```

导入后 `Content` 面板会自动扫描 `assets/*.npkg`，点击 `Load` 会加载包到运行时 AssetManager（后续会补：列出包内资产、预览、拖拽到场景等）。
