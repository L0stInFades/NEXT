# Docs 索引

建议阅读顺序：
1. `docs/00-vision-principles.md`（愿景、边界、质量门槛）
2. `docs/01-engine-structure.md`（引擎分层、模块边界、运行时模型）
3. `docs/02-rendering-roadmap.md`（渲染：混合光追/非光追、运镜、材质纹理）
4. `docs/03-world-streaming-time.md`（超大世界：分区/压缩/无加载/百年时间线）
5. `docs/04-ai-systems.md`（AI/生成：NPC/建筑/植物、以及布料/肌肉/骨骼等）
6. `docs/05-scripting-quests.md`（逻辑脚本：任务、事件、灾害扰动与"不断支线"）
7. `docs/06-tools-modding-experiments.md`（工具链、Mod SDK、可持续实验体系）
8. `docs/07-milestones.md`（分阶段里程碑：从原型到首作可交付）
9. `docs/08-development-workflow.md`（程序员开发流程：先做什么、怎么并行、验收门槛）
10. `docs/ASSET_PIPELINE_QUICKSTART.md`（模型导入与打包快速上手）
11. `docs/EDITOR_QUICKSTART.md`（编辑器快速上手：UE5 风格 Docking UI + 资产浏览/导入入口）

## 开发进度

### 已完成里程碑

- `docs/CP0_COMPLETION.md` (2025-12-28)
  - CP0 完成报告：工程骨架与最小可运行样例
  - 验收：? 所有指标通过

- `docs/CP1_COMPLETION.md` (2025-12-28)
  - CP1 完成报告：可观测性优先
  - 验收：? Frame Profiler、CPU Scope、统计面板

- `docs/CP2_COMPLETION.md` (2025-12-28)
  - CP2 完成报告：Job System
  - 验收：? 任务队列/线程池/依赖/预算化/统计

### 开发交接

- `docs/HANDOVER_CP0_TO_CP1.md` (2025-12-28, 历史文档)
  - CP0 → CP1 交接文档

- `docs/HANDOVER_CP1_TO_CP2.md` (2025-12-28)
  - CP1 → CP2 交接文档
  - CP2 目标与交付物说明

- `docs/HANDOVER_CP2_TO_CP3.md` (2025-12-28)
  - CP2 → CP3 交接文档
  - CP3 目标与首迭代建议

### 待开发里程碑

- CP3: Asset Pipeline 雏形
- CP4: Runtime World (完整 ECS)
- CP5: 渲染基线 (DX12 + Render Graph)
- CP6: 手感与运镜
- CP7: 世界分区与流式
- CP8: 脚本系统
- CP9: 任务系统
- CP10: 编辑器与生产化

---

说明：
- 本目录文档以**可实现**为前提：每个系统会给出推荐路线、可选路线、以及先后顺序（Production Track / R&D Track）。
- 具体实现细节与代码落地由初级程序员完成；文档会明确需要的接口、数据流、质量指标与验收口径。
