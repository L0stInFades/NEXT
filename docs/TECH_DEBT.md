# 技术债务清偿记录（当前阶段）

本次修复仅面向“已存在的问题”清理，不涉及新功能。

## 已修复
- CMake 环节循环依赖：移除 `next_foundation` 对 `next_platform` 的反向依赖，解除构建死循环。
- 事件总线编译错误：补齐头文件依赖，修复类型拼写错误（`EventTypeID`）。
- Game 生命周期：`Song::Game` 现在只创建一套 Window/Renderer/Input，并负责初始化与销毁，避免未初始化对象、重复创建和泄漏。
- 构建脚本健壮性：`build.bat` 不再硬编码 CMake/VS 路径，支持 PATH 自动探测和可选 `CMAKE_GENERATOR` 覆盖，默认用本机可用的 Visual Studio。
- Job System：新增多线程任务队列、依赖、预算化接口，主循环集成自测。

## 已知待办（未改动）
- 渲染器仍为 Dummy，占位等待 CP5。
- Profiler CPU Scope 仍基于毫秒级 `GetTickCount64`，多线程安全未加；未覆盖 GPU/内存统计。
- ECS/World 仍是最小实现，缺少系统调度与组件池；Asset Pipeline 尚未落地（CP3）。
- Job System 已上线但仍需：无锁队列优化、任务分组/更细粒度统计、更细的预算策略。
- 缺少单元测试与自动化回归；资源/句柄仍靠裸指针管理。
- 崩溃捕获/日志尚未落盘，未来需补文件/远程日志管线。

## 快速验证
```
build.bat
build\bin\Debug\song_demo.exe
```
- 窗口能正常创建，WASD 有日志响应，ESC 退出。
- 无 CMake 循环依赖错误，事件总线头文件可编译。
