#include <gtest/gtest.h>

#include "next/task/task_system.h"
#include "next/runtime/world.h"
#include "next/runtime/event_bus.h"

#include <filesystem>

TEST(TaskSystem, SaveLoadStateJson) {
    using namespace Next;

    World world;
    auto& bus = EventBus::GetInstance();

    TaskExecutorConfig cfg;
    cfg.enableEventProcessing = false;
    cfg.enableAutoSave = false;
    cfg.enableReplay = true;

    TaskExecutor exec(&world, &bus, cfg);
    exec.Initialize();

    // Register a minimal task definition.
    TaskDefinition def("task_1");
    def.name = "Test Task";
    def.firstStepId = "step_1";
    def.steps.push_back(TaskStep("step_1", "Do thing"));
    def.goals.push_back(TaskGoal("goal_1", "Win", GoalType::Primary));
    exec.RegisterTaskDefinition(def);

    const TaskDefinition* storedDef = exec.GetTaskDefinition("task_1");
    ASSERT_NE(storedDef, nullptr);

    TaskInstance* inst = exec.StartTask(storedDef);
    ASSERT_NE(inst, nullptr);

    inst->variables.Set("i", int64_t{42});
    inst->variables.Set("f", 3.5);
    inst->variables.Set("s", std::string("hello"));
    inst->variables.Set("b", true);
    inst->SetStepStatus("step_1", StepStatus::Completed);
    inst->SetGoalAchieved("goal_1", true);
    inst->eventHistory.push_back(TaskInstance::EventRecord{"evt", 123.0, "desc"});

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_task_state_test.json";
    std::filesystem::remove(tmp);

    ASSERT_TRUE(exec.SaveState(tmp.string()));

    // Load into a fresh executor.
    TaskExecutor exec2(&world, &bus, cfg);
    exec2.Initialize();
    exec2.RegisterTaskDefinition(def);

    ASSERT_TRUE(exec2.LoadState(tmp.string()));

    auto tasks = exec2.GetAllTasks();
    ASSERT_EQ(tasks.size(), 1u);
    const TaskInstance* loaded = tasks[0];
    ASSERT_NE(loaded, nullptr);
    ASSERT_NE(loaded->definition, nullptr);

    EXPECT_EQ(loaded->definition->id, "task_1");
    EXPECT_EQ(loaded->GetStepStatus("step_1"), StepStatus::Completed);
    EXPECT_TRUE(loaded->IsGoalAchieved("goal_1"));
    EXPECT_EQ(loaded->variables.ints.at("i"), 42);
    EXPECT_NEAR(loaded->variables.floats.at("f"), 3.5, 1e-6);
    EXPECT_EQ(loaded->variables.strings.at("s"), "hello");
    EXPECT_EQ(loaded->variables.bools.at("b"), true);
    ASSERT_EQ(loaded->eventHistory.size(), 1u);
    EXPECT_EQ(loaded->eventHistory[0].eventId, "evt");

    exec2.Shutdown();
    exec.Shutdown();

    std::filesystem::remove(tmp);
}

TEST(TaskSystem, AutoSaveOnShutdownWritesState) {
    using namespace Next;

    World world;
    auto& bus = EventBus::GetInstance();

    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "next_task_autosave_test.json";
    std::filesystem::remove(tmp);

    TaskExecutorConfig cfg;
    cfg.enableEventProcessing = false;
    cfg.enableAutoSave = true;
    cfg.autoSaveOnStateChange = true;
    cfg.enableReplay = false;
    cfg.autoSavePath = tmp.string();

    {
        TaskExecutor exec(&world, &bus, cfg);
        exec.Initialize();

        TaskDefinition def("task_autosave");
        def.name = "Auto Save Task";
        def.firstStepId = "step_1";
        def.steps.push_back(TaskStep("step_1", "Persist me"));
        exec.RegisterTaskDefinition(def);

        const TaskDefinition* storedDef = exec.GetTaskDefinition("task_autosave");
        ASSERT_NE(storedDef, nullptr);
        ASSERT_NE(exec.StartTask(storedDef), nullptr);
        exec.Shutdown();
    }

    ASSERT_TRUE(std::filesystem::exists(tmp));

    TaskExecutorConfig loadCfg = cfg;
    loadCfg.enableAutoSave = false;
    TaskExecutor exec2(&world, &bus, loadCfg);
    exec2.Initialize();

    TaskDefinition def("task_autosave");
    def.name = "Auto Save Task";
    def.firstStepId = "step_1";
    def.steps.push_back(TaskStep("step_1", "Persist me"));
    exec2.RegisterTaskDefinition(def);

    ASSERT_TRUE(exec2.LoadState(tmp.string()));
    EXPECT_EQ(exec2.GetAllTasks().size(), 1u);

    exec2.Shutdown();
    std::filesystem::remove(tmp);
}

TEST(TaskSystem, SchedulerAutoAcceptsOnEvent) {
    using namespace Next;

    World world;
    auto& bus = EventBus::GetInstance();

    TaskExecutorConfig execCfg;
    execCfg.enableEventProcessing = true;
    execCfg.enableAutoSave = false;
    execCfg.enableReplay = false;

    TaskExecutor exec(&world, &bus, execCfg);
    exec.Initialize();

    TaskDefinition def("task_event");
    def.name = "Event Task";
    def.autoAccept = true;
    def.subscribedEvents.push_back("quest.triggered");
    def.firstStepId = "step_1";
    def.steps.push_back(TaskStep("step_1", "Wait"));
    exec.RegisterTaskDefinition(def);

    TaskScheduler scheduler(&exec);
    scheduler.Initialize();
    scheduler.ProcessEvent("quest.triggered", nullptr);

    auto tasks = exec.GetAllTasks();
    ASSERT_EQ(tasks.size(), 1u);
    ASSERT_NE(tasks[0], nullptr);
    EXPECT_EQ(tasks[0]->definition->id, "task_event");

    scheduler.Shutdown();
    exec.Shutdown();
}
