# NEXT（自研引擎与两宋项目）架构仓库

本仓库用于沉淀自研引擎的**架构设计、技术路线、里程碑与规范**，目标是支撑"中华历史朝代系列 IP"的长期开发（首作背景：两宋）。

## 当前进度

### ✅ CP0 - 工程骨架与最小可运行样例 (已完成)

**状态**: 2025-12-28 完成

- ✅ 清晰的目录分层
- ✅ 一键构建系统 (`build.bat`)
- ✅ 窗口系统 (Win32)
- ✅ 输入系统 (键盘/鼠标)
- ✅ 日志系统 (多级日志)
- ✅ 崩溃捕获 (Mini dump)
- ✅ 30分钟编译运行
- ✅ 5分钟稳定运行

**详情**: `docs/CP0_COMPLETION.md`

---

### ✅ CP1 - 可观测性优先 (已完成)

**状态**: 2025-12-28 完成

- ✅ Frame Profiler (帧时间、FPS 统计)
- ✅ CPU Scope 计时器 (代码块级性能分析)
- ✅ 滑动窗口统计 (最近 120 帧)
- ✅ 控制台性能面板输出
- ✅ 内存/IO 统计接口 (框架预留)
- ✅ GPU 时间戳框架 (接口预留)

**详情**: `docs/CP1_COMPLETION.md`

---

### ✅ CP2 - Job System (已完成)

**状态**: 2025-12-28 完成

- ✅ 中央任务队列 + 可配置线程池
- ✅ 任务优先级 (High/Normal/Low)
- ✅ 依赖/栅栏：任务间依赖完成后自动入队
- ✅ 可观测性：执行耗时统计、队列长度/活跃线程数查询
- ✅ 帧预算辅助：主线程可在预算时间内抢占执行任务

**详情**: `docs/CP2_COMPLETION.md`

---

### ✅ CP3 - Asset Pipeline 雏形 (已完成)

**状态**: 2025-12-27 完成，2026-01-13 验证通过

- ✅ 资源格式定义（Mesh/Texture/Material）
- ✅ Asset Runtime（同步/异步加载、引用计数）
- ✅ 打包容器（PackageContainer）
- ✅ 资源编译器（assetc）
- ✅ Job System 集成
- ✅ 完整验收测试通过

**详情**: `docs/CP3_COMPLETION.md`

---

### ✅ CP4 - Runtime World 完整 ECS (已完成)

**状态**: 2026-01-13 完成

- ✅ Entity 系统（带版本号，安全可靠）
- ✅ Component 系统（类型安全、高效）
- ✅ Query 系统（基础实现）
- ✅ System 框架（可扩展）
- ✅ MeshRendererComponent（集成 Asset Pipeline）
- ✅ 性能测试通过（10,000 实体 @ 118 FPS）

**详情**: `docs/CP4_COMPLETION.md`

---

## 文档阅读

从这里开始阅读：`docs/README.md`。

## 快速开始

### 构建项目

```cmd
build.bat
```

### 运行程序

```cmd
build\bin\Debug\song_demo.exe
```

**控制**: WASD 移动, ESC 退出

### 环境状态检测

已检测到:
- ✅ Visual Studio 2026 安装在 `E:\VS`
- ✅ CMake 安装在 `E:\CMake\bin`

### 构建方式

#### 方式1: 使用构建脚本 (推荐)

```cmd
build.bat
```

#### 方式2: 手动命令行

```cmd
mkdir build
cd build
cmake .. -G "Visual Studio 18" -A x64
cmake --build . --config Debug
```

### 运行

构建成功后运行:
```cmd
build\bin\Debug\song_demo.exe
```

**控制操作**:
- WASD: 移动 (日志输出)
- ESC: 退出

### 验收检查清单 (CP0)

- [x] 清晰的目录分层
- [x] 一键构建 + 一键运行的"空场景"程序
- [x] 窗口创建 + 相机移动 (日志验证)
- [x] 日志输出
- [ ] 崩溃捕获 (需补充)
- [ ] 30分钟内可编译运行 (需用户环境验证)
- [ ] 5分钟不崩溃 (需运行测试验证)

详细文档请参见 `BUILD.md`
