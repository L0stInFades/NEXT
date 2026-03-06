// CP9: Task System - 使用示例
//
// 这个文件展示了如何使用任务系统的核心 API

#include "next/task/task_system.h"
#include "next/runtime/world.h"
#include "next/runtime/event_bus.h"
#include <iostream>

using namespace Next;

// ============================================================================
// 示例 1: 创建并启动简单任务
// ============================================================================

void Example1_SimpleTask(World* world, EventBus* eventBus, TaskExecutor* executor) {
    std::cout << "\n=== 示例 1: 创建并启动简单任务 ===\n";

    // 创建任务定义
    TaskDefinition collectTask;
    collectTask.id = "collect_herbs";
    collectTask.name = "采集草药";
    collectTask.description = "从森林中采集 5 株草药";
    collectTask.priority = TaskPriority::Normal;
    collectTask.autoAccept = true;
    collectTask.replannable = false;

    // 添加主目标
    TaskGoal goal;
    goal.id = "goal_collect_5_herbs";
    goal.description = "采集 5 株草药";
    goal.type = GoalType::Primary;
    goal.achievementConditions.logicOp = ConditionSet::All;
    Condition herbCondition;
    herbCondition.type = ConditionType::Item;
    herbCondition.target = "player.inventory";
    herbCondition.op = ConditionOperator::Contains;
    herbCondition.value = int64_t(5);  // 数量
    goal.achievementConditions.conditions.push_back(herbCondition);
    collectTask.goals.push_back(goal);

    // 添加步骤
    TaskStep step1;
    step1.id = "step_go_to_forest";
    step1.description = "前往森林";
    step1.completionCriteria.logicOp = ConditionSet::All;
    Condition locCondition;
    locCondition.type = ConditionType::Location;
    locCondition.target = "player.location";
    locCondition.op = ConditionOperator::Equal;
    locCondition.value = std::string("forest");
    step1.completionCriteria.conditions.push_back(locCondition);
    collectTask.steps.push_back(step1);

    TaskStep step2;
    step2.id = "step_collect_herbs";
    step2.description = "采集草药";
    step2.prerequisites.logicOp = ConditionSet::All;
    Condition atForestCondition;
    atForestCondition.type = ConditionType::Location;
    atForestCondition.target = "player.location";
    atForestCondition.op = ConditionOperator::Equal;
    atForestCondition.value = std::string("forest");
    step2.prerequisites.conditions.push_back(atForestCondition);
    step2.completionCriteria.logicOp = ConditionSet::All;
    step2.completionCriteria.conditions.push_back(herbCondition);
    collectTask.steps.push_back(step2);

    collectTask.firstStepId = "step_go_to_forest";

    // 注册任务定义
    executor->RegisterTaskDefinition(collectTask);

    // 启动任务
    TaskInstance* instance = executor->StartTask(&collectTask);
    if (instance) {
        std::cout << "任务已启动: " << instance->instanceId << "\n";
    }
}

// ============================================================================
// 示例 2: 带条件系统的复杂任务
// ============================================================================

void Example2_ConditionSystem(World* world, EventBus* eventBus, TaskExecutor* executor) {
    std::cout << "\n=== 示例 2: 带条件系统的复杂任务 ===\n";

    TaskDefinition escortTask;
    escortTask.id = "escort_merchant";
    escortTask.name = "护送商人";
    escortTask.description = "护送商人安全到达临安府";
    escortTask.priority = TaskPriority::Main;
    escortTask.replannable = true;  // 可重规划（应对灾害）

    // 时间窗口条件：只能在白天执行
    Condition timeCondition;
    timeCondition.type = ConditionType::Time;
    timeCondition.target = "world.time_of_day";
    timeCondition.op = ConditionOperator::InRange;
    timeCondition.range.min = int64_t(6);   // 6:00
    timeCondition.range.max = int64_t(18);  // 18:00

    // 地点条件
    Condition locationCondition;
    locationCondition.type = ConditionType::Location;
    locationCondition.target = "player.location";
    locationCondition.op = ConditionOperator::Equal;
    locationCondition.value = std::string("linan_prefecture");

    // NPC 关系条件
    Condition relationCondition;
    relationCondition.type = ConditionType::Relationship;
    relationCondition.target = "npc.merchant.attitude";
    relationCondition.op = ConditionOperator::GreaterEqual;
    relationCondition.value = int64_t(50);  // 好感度 >= 50

    // 设置接取条件
    escortTask.acceptanceConditions.logicOp = ConditionSet::All;
    escortTask.acceptanceConditions.conditions.push_back(timeCondition);
    escortTask.acceptanceConditions.conditions.push_back(relationCondition);

    // 设置完成条件（复合条件）
    TaskGoal mainGoal;
    mainGoal.id = "goal_reach_destination";
    mainGoal.description = "护送商人到达目的地";
    mainGoal.type = GoalType::Primary;
    mainGoal.achievementConditions.logicOp = ConditionSet::All;
    mainGoal.achievementConditions.conditions.push_back(locationCondition);

    // 商人存活条件
    Condition merchantAliveCondition;
    merchantAliveCondition.type = ConditionType::Custom;
    merchantAliveCondition.target = "npc.merchant.is_alive";
    merchantAliveCondition.op = ConditionOperator::Equal;
    merchantAliveCondition.value = true;
    mainGoal.achievementConditions.conditions.push_back(merchantAliveCondition);

    escortTask.goals.push_back(mainGoal);

    // 订阅相关事件
    escortTask.subscribedEvents.push_back("world.disaster.bandit_attack");
    escortTask.subscribedEvents.push_back("npc.merchant.died");
    escortTask.subscribedEvents.push_back("world.time.night_falls");

    executor->RegisterTaskDefinition(escortTask);
}

// ============================================================================
// 示例 3: 任务重规划（灾害扰动）
// ============================================================================

void Example3_TaskReplanning(World* world, EventBus* eventBus, TaskExecutor* executor, TaskReplanner* replanner) {
    std::cout << "\n=== 示例 3: 任务重规划（灾害扰动） ===\n";

    // 创建可重规划的任务
    TaskDefinition deliveryTask;
    deliveryTask.id = "deliver_message";
    deliveryTask.name = "传递消息";
    deliveryTask.description = "将消息传送到目标地点";
    deliveryTask.priority = TaskPriority::Normal;
    deliveryTask.replannable = true;

    // 添加步骤
    TaskStep mainRoute;
    mainRoute.id = "step_main_route";
    mainRoute.description = "走大路";
    mainRoute.replannable = true;
    mainRoute.alternativeStepIds.push_back("step_alternative_route");

    TaskStep altRoute;
    altRoute.id = "step_alternative_route";
    altRoute.description = "走小路（备用）";
    altRoute.replannable = true;

    deliveryTask.steps.push_back(mainRoute);
    deliveryTask.steps.push_back(altRoute);
    deliveryTask.firstStepId = "step_main_route";

    executor->RegisterTaskDefinition(deliveryTask);

    // 注册重规划规则
    ReplanRule disasterRule;
    disasterRule.triggerCondition.type = ConditionType::WorldEvent;
    disasterRule.triggerCondition.target = "world.disaster.road_blocked";
    disasterRule.triggerCondition.op = ConditionOperator::Equal;
    disasterRule.triggerCondition.value = true;
    disasterRule.strategy = ReplanStrategy::ReplaceStep;

    // 添加重规划时的通知动作
    Action notifyAction;
    notifyAction.type = ActionType::ShowNotification;
    notifyAction.params.Set("message", std::string("道路被阻断，正在寻找替代路线..."));
    disasterRule.actions.push_back(notifyAction);

    replanner->RegisterReplanRule("deliver_message", disasterRule);

    std::cout << "重规划规则已注册：道路被阻断时切换到替代路线\n";
}

// ============================================================================
// 示例 4: 事件驱动的任务触发
// ============================================================================

void Example4_EventDrivenTasks(World* world, EventBus* eventBus, TaskExecutor* executor, TaskScheduler* scheduler) {
    std::cout << "\n=== 示例 4: 事件驱动的任务触发 ===\n";

    // 创建灾害响应任务
    TaskDefinition disasterTask;
    disasterTask.id = "flood_response";
    disasterTask.name = "洪水救援";
    disasterTask.description = "洪水发生后的紧急救援任务";
    disasterTask.priority = TaskPriority::Critical;
    disasterTask.autoAccept = true;
    disasterTask.replannable = true;

    // 设置接取条件：洪水事件发生
    Condition floodCondition;
    floodCondition.type = ConditionType::WorldEvent;
    floodCondition.target = "world.disaster.flood";
    floodCondition.op = ConditionOperator::Equal;
    floodCondition.value = true;

    disasterTask.acceptanceConditions.logicOp = ConditionSet::All;
    disasterTask.acceptanceConditions.conditions.push_back(floodCondition);

    // 订阅洪水事件
    disasterTask.subscribedEvents.push_back("world.disaster.flood");

    executor->RegisterTaskDefinition(disasterTask);

    std::cout << "灾害响应任务已注册：将在洪水事件发生时自动触发\n";
}

// ============================================================================
// 示例 5: 任务序列
// ============================================================================

void Example5_TaskSequence(World* world, EventBus* eventBus, TaskExecutor* executor) {
    std::cout << "\n=== 示例 5: 任务序列 ===\n";

    // 创建一系列相关任务

    // 任务 1：准备物资
    TaskDefinition task1;
    task1.id = "prepare_supplies";
    task1.name = "准备物资";
    task1.description = "为旅程准备必要的物资";
    task1.priority = TaskPriority::Main;
    task1.autoAccept = true;

    executor->RegisterTaskDefinition(task1);

    // 任务 2：启程
    TaskDefinition task2;
    task2.id = "start_journey";
    task2.name = "启程";
    task2.description = "开始你的旅程";
    task2.priority = TaskPriority::Main;
    task2.autoAccept = true;

    // 设置前置任务
    task2.prerequisiteTaskIds.push_back("prepare_supplies");

    executor->RegisterTaskDefinition(task2);

    // 任务 3：到达目的地
    TaskDefinition task3;
    task3.id = "reach_destination";
    task3.name = "到达目的地";
    task3.description = "到达你的目的地";
    task3.priority = TaskPriority::Main;
    task3.autoAccept = true;

    task3.prerequisiteTaskIds.push_back("start_journey");

    executor->RegisterTaskDefinition(task3);

    // 设置任务链
    task1.nextTaskIds.push_back("start_journey");
    task2.nextTaskIds.push_back("reach_destination");

    // 更新注册
    executor->RegisterTaskDefinition(task1);
    executor->RegisterTaskDefinition(task2);
    executor->RegisterTaskDefinition(task3);

    std::cout << "任务序列已创建：准备物资 -> 启程 -> 到达目的地\n";
}

// ============================================================================
// 示例 6: 任务状态查询和统计
// ============================================================================

void Example6_TaskStatistics(World* world, EventBus* eventBus, TaskExecutor* executor) {
    std::cout << "\n=== 示例 6: 任务状态查询和统计 ===\n";

    // 获取所有进行中的任务
    auto activeTasks = executor->GetTasksByStatus(TaskStatus::InProgress);
    std::cout << "当前进行中的任务数量: " << activeTasks.size() << "\n";

    for (auto* task : activeTasks) {
        if (task && task->definition) {
            std::cout << "  - " << task->definition->name << " (" << task->instanceId << ")\n";
            std::cout << "    当前步骤: " << task->currentStepId << "\n";
            std::cout << "    已完成步骤: " << task->stepsCompleted << "\n";
            std::cout << "    已失败步骤: " << task->stepsFailed << "\n";
        }
    }

    // 获取统计信息
    auto stats = executor->GetStatistics();
    std::cout << "\n任务统计:\n";
    std::cout << "  总启动任务数: " << stats.totalTasksStarted << "\n";
    std::cout << "  总完成任务数: " << stats.totalTasksCompleted << "\n";
    std::cout << "  总失败任务数: " << stats.totalTasksFailed << "\n";
    std::cout << "  当前活动任务: " << stats.activeTasks << "\n";
    std::cout << "  平均完成时间: " << stats.averageCompletionTime << " 秒\n";
}

// ============================================================================
// 主函数：运行所有示例
// ============================================================================

int main() {
    std::cout << "CP9: Task System - 使用示例\n";
    std::cout << "===========================\n";

    // 初始化（在实际使用中，这些会由引擎初始化）
    World* world = nullptr;  // 实际使用时会有真实的 World 实例
    EventBus* eventBus = nullptr;  // 实际使用时会有真实的 EventBus 实例

    // 获取任务系统管理器
    auto& taskManager = TaskSystemManager::GetInstance();
    taskManager.Initialize(world, eventBus);

    TaskExecutor* executor = taskManager.GetExecutor();
    TaskScheduler* scheduler = taskManager.GetScheduler();
    TaskReplanner* replanner = taskManager.GetReplanner();

    // 运行所有示例
    Example1_SimpleTask(world, eventBus, executor);
    Example2_ConditionSystem(world, eventBus, executor);
    Example3_TaskReplanning(world, eventBus, executor, replanner);
    Example4_EventDrivenTasks(world, eventBus, executor, scheduler);
    Example5_TaskSequence(world, eventBus, executor);

    // 模拟游戏循环更新
    std::cout << "\n=== 模拟游戏循环 ===\n";
    for (int i = 0; i < 10; ++i) {
        taskManager.Update(0.016f);  // 60 FPS
    }

    // 显示统计信息
    Example6_TaskStatistics(world, eventBus, executor);

    // 清理
    taskManager.Shutdown();

    std::cout << "\n所有示例运行完成！\n";

    return 0;
}
