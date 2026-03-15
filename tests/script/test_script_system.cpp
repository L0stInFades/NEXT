#include <gtest/gtest.h>

#include "next/runtime/world.h"
#include "next/script/script_system.h"

#include <filesystem>
#include <fstream>

namespace Next {
namespace testing {

namespace {

std::filesystem::path WriteTempScript(const std::string& name, const std::string& content) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << content;
    file.close();
    return path;
}

void RemoveTempScript(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // namespace

class ScriptSystemTest : public ::testing::Test {
protected:
};

TEST_F(ScriptSystemTest, LifecycleStartsOnFirstUpdateAndTracksStats) {
    World world;
    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());

    ScriptSystem system(&world, &vm);
    system.Initialize();

    const auto scriptPath = WriteTempScript(
        "next_script_lifecycle.lua",
        "function OnStart() end\n"
        "function OnUpdate() end\n");

    ScriptComponentConfig config;
    config.autoStart = true;
    config.parameters["mode"] = "test";

    const Entity entity = world.CreateEntity();
    ScriptComponent* script = system.AddScriptComponent(entity, scriptPath.string(), &config);
    ASSERT_NE(script, nullptr);
    EXPECT_FALSE(script->isStarted);
    EXPECT_EQ(script->GetParameter("mode"), "test");

    system.Update(0.016f);

    EXPECT_TRUE(script->isStarted);
    EXPECT_EQ(script->stats.updateCount, 1u);
    EXPECT_TRUE(script->isEnabled);

    ScriptSystem::SystemStats stats = system.GetStats();
    EXPECT_EQ(stats.totalScripts, 1u);
    EXPECT_EQ(stats.activeScripts, 1u);
    EXPECT_EQ(stats.totalCalls, 1u);

    system.RemoveScriptComponent(entity, scriptPath.string());
    EXPECT_EQ(system.GetScriptComponent(entity, scriptPath.string()), nullptr);

    vm.Shutdown();
    RemoveTempScript(scriptPath);
}

#ifndef NEXT_WITH_LUA
TEST_F(ScriptSystemTest, StubModeKeepsScriptingSurfaceAvailable) {
    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());
    EXPECT_TRUE(vm.IsStubMode());
    EXPECT_TRUE(vm.ExecuteString("this is ignored in stub mode"));

    const auto scriptPath = WriteTempScript("next_script_stub_exec.lua", "return 0\n");
    EXPECT_TRUE(vm.ExecuteFile(scriptPath.string()));

    vm.Shutdown();
    RemoveTempScript(scriptPath);
}
#endif

#ifdef NEXT_WITH_LUA
TEST_F(ScriptSystemTest, LuaModeUsesParamsAndDeltaTime) {
    World world;
    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());
    EXPECT_FALSE(vm.IsStubMode());

    ScriptSystem system(&world, &vm);
    system.Initialize();

    const auto scriptPath = WriteTempScript(
        "next_script_params.lua",
        "function OnStart()\n"
        "  if params.mode ~= 'battle' then error('bad params') end\n"
        "end\n"
        "function OnUpdate()\n"
        "  if Time.DeltaTime <= 0 then error('missing delta') end\n"
        "end\n");

    ScriptComponentConfig config;
    config.autoStart = true;
    config.parameters["mode"] = "battle";

    ScriptComponent* script = system.AddScriptComponent(world.CreateEntity(), scriptPath.string(), &config);
    ASSERT_NE(script, nullptr);

    system.Update(0.016f);
    EXPECT_TRUE(script->isStarted);
    EXPECT_TRUE(script->isEnabled);
    EXPECT_EQ(script->stats.updateCount, 1u);

    system.Update(0.032f);
    EXPECT_EQ(script->stats.updateCount, 2u);

    vm.Shutdown();
    RemoveTempScript(scriptPath);
}

TEST_F(ScriptSystemTest, LuaRuntimeErrorDisablesOnlyFailingScript) {
    World world;
    LuaVM vm;
    ASSERT_TRUE(vm.Initialize());

    ScriptSystem system(&world, &vm);
    system.Initialize();

    const auto goodPath = WriteTempScript(
        "next_script_good.lua",
        "function OnStart() end\n"
        "function OnUpdate()\n"
        "  if params.kind ~= 'good' then error('bad params') end\n"
        "end\n");
    const auto badPath = WriteTempScript(
        "next_script_bad.lua",
        "function OnUpdate()\n"
        "  error('boom')\n"
        "end\n");

    ScriptComponentConfig goodConfig;
    goodConfig.parameters["kind"] = "good";

    ScriptComponent* good = system.AddScriptComponent(world.CreateEntity(), goodPath.string(), &goodConfig);
    ScriptComponent* bad = system.AddScriptComponent(world.CreateEntity(), badPath.string(), nullptr);
    ASSERT_NE(good, nullptr);
    ASSERT_NE(bad, nullptr);

    system.Update(0.016f);

    EXPECT_TRUE(good->isEnabled);
    EXPECT_TRUE(good->isStarted);
    EXPECT_EQ(good->stats.updateCount, 1u);

    EXPECT_FALSE(bad->isEnabled);
    EXPECT_EQ(bad->stats.updateCount, 0u);

    system.Update(0.016f);
    EXPECT_EQ(good->stats.updateCount, 2u);
    EXPECT_EQ(bad->stats.updateCount, 0u);

    vm.Shutdown();
    RemoveTempScript(goodPath);
    RemoveTempScript(badPath);
}
#endif

} // namespace testing
} // namespace Next
