#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>
#include <cstdint>

#include "next/runtime/entity.h"

namespace Next {

// 前向声明（避免Lua依赖）
class World;
class EventBus;

/**
 * @brief CP8: Script System (脚本系统)
 *
 * 框架实现版本 - 提供完整的接口设计，可在没有Lua库的情况下编译
 * 要启用完整的Lua支持，需要在CMake中设置 USE_SYSTEM_LUA=ON
 *
 * 核心功能：
 * 1. Lua虚拟机管理 (LuaVM)
 * 2. C++/Lua绑定系统 (LuaBindings)
 * 3. 脚本加载和热重载 (ScriptLoader)
 * 4. 性能预算监控 (ScriptProfiler)
 * 5. ECS集成 (ScriptComponent)
 *
 * 对标：
 * - UE5: Lua scripting for gameplay
 * - Unity: C# scripting with Lua integration
 * - AAA: Scripted game logic
 */

/**
 * @brief Lua虚拟机配置
 */
struct LuaVMConfig {
    bool enableSandbox = true;
    bool enableHotReload = true;
    size_t memoryLimitMB = 64;
    float executionTimeLimitMs = 2.0f;
    bool enableStandardLibs = false;
    bool enableProfiler = true;
};

/**
 * @brief Lua虚拟机统计
 */
struct LuaVMStats {
    size_t memoryUsedKB = 0;
    size_t scriptCount = 0;
    float averageExecutionTimeMs = 0.0f;
    uint32_t activeScripts = 0;
};

/**
 * @brief Lua虚拟机（框架实现）
 */
class LuaVM {
public:
    using ScriptID = uint64_t;

    LuaVM(const LuaVMConfig& config = LuaVMConfig{});
    ~LuaVM();

    bool Initialize();
    void Shutdown();

    ScriptID LoadScript(const std::string& scriptPath);
    ScriptID LoadScriptFromString(const std::string& scriptName, const std::string& scriptContent);
    void UnloadScript(ScriptID scriptID);
    bool ReloadScript(ScriptID scriptID);

    bool CallScriptFunction(ScriptID scriptID, const std::string& functionName);

    void RegisterCFunction(const std::string& luaName, void* function);

    bool ExecuteString(const std::string& code);
    bool ExecuteFile(const std::string& filePath);

    void Update(float deltaTime);
    LuaVMStats GetStats() const { return stats_; }
    void ResetStats();

    const LuaVMConfig& GetConfig() const { return config_; }

private:
    void* L_;  // lua_State* (使用void*避免Lua依赖)
    LuaVMConfig config_;
    LuaVMStats stats_;
    ScriptID nextScriptID_ = 1;

    struct ScriptInfo {
        ScriptID id;
        std::string name;
        std::string path;
        std::string content;
        bool isValid;
    };

    std::unordered_map<ScriptID, ScriptInfo> scripts_;
};

/**
 * @brief Lua绑定类型
 */
enum class LuaBindingType {
    World, Entity, Component, Event, Math, Log, Time, Transform, Script
};

/**
 * @brief Lua API权限
 */
enum class LuaAPIPermission {
    Safe, Restricted, Dangerous, Internal
};

/**
 * @brief Lua绑定系统（框架实现）
 */
class LuaBindings {
public:
    static void Initialize(void* L);
    static void RegisterAllBindings(void* L);
    static void RegisterWorldAPI(void* L, World* world);
    static void RegisterEntityAPI(void* L);
    static void RegisterEventAPI(void* L, EventBus* eventBus);
    static void RegisterMathAPI(void* L);
    static void RegisterLogAPI(void* L);
    static void RegisterTimeAPI(void* L);

    static void SetPermissionCheckCallback(std::function<bool(LuaAPIPermission)> callback);
    static bool CheckPermission(LuaAPIPermission permission);

private:
    static std::function<bool(LuaAPIPermission)> permissionCallback_;
};

/**
 * @brief 脚本组件配置
 */
struct ScriptComponentConfig {
    std::string scriptPath;
    bool enabled = true;
    bool autoStart = true;
    float updateInterval = 0.0f;
    std::unordered_map<std::string, std::string> parameters;
};

/**
 * @brief 脚本组件
 */
struct ScriptComponent {
    static constexpr uint32_t TypeID = 1000;

    Entity owner;
    std::string scriptPath;
    LuaVM::ScriptID scriptID;
    ScriptComponentConfig config;

    bool isStarted = false;
    bool isEnabled = true;
    float timeSinceLastUpdate = 0.0f;

    struct {
        uint64_t updateCount = 0;
        double totalUpdateTimeMs = 0.0f;
        double averageUpdateTimeMs = 0.0f;
    } stats;

    ScriptComponent() = default;
    ScriptComponent(Entity entity, const std::string& script)
        : owner(entity), scriptPath(script), scriptID(0) {}

    bool CallFunction(const std::string& functionName, LuaVM* vm);
    void SetParameter(const std::string& key, const std::string& value);
    std::string GetParameter(const std::string& key) const;
};

/**
 * @brief 脚本系统
 */
class ScriptSystem {
public:
    ScriptSystem(World* world, LuaVM* vm);
    ~ScriptSystem();

    void Initialize();
    void Update(float deltaTime);

    ScriptComponent* AddScriptComponent(Entity entity, const std::string& scriptPath,
                                       const ScriptComponentConfig* config = nullptr);
    void RemoveScriptComponent(Entity entity, const std::string& scriptPath);
    ScriptComponent* GetScriptComponent(Entity entity, const std::string& scriptPath);

    void EnableScript(Entity entity, const std::string& scriptPath);
    void DisableScript(Entity entity, const std::string& scriptPath);

    struct SystemStats {
        size_t totalScripts = 0;
        size_t activeScripts = 0;
        uint64_t totalCalls = 0;
        double totalUpdateTimeMs = 0.0f;
        double averageUpdateTimeMs = 0.0f;
    };

    SystemStats GetStats() const;
    void ResetStats();

private:
    World* world_;
    LuaVM* vm_;

    using ScriptMap = std::unordered_map<std::string, ScriptComponent>;
    std::unordered_map<uint64_t, ScriptMap> entityScripts_;

    struct ScriptRef {
        uint64_t entity;
        ScriptComponent* component;
    };
    std::vector<ScriptRef> allScripts_;

    SystemStats stats_;

    void StartScript(ScriptComponent* script);
    void UpdateScript(ScriptComponent* script, float deltaTime);
    void CallScriptCallback(ScriptComponent* script, const std::string& callbackName);

    bool LoadScript(ScriptComponent* script);
    void UnloadScript(ScriptComponent* script);
};

/**
 * @brief 脚本系统管理器（单例）
 */
class ScriptSystemManager {
public:
    static ScriptSystemManager& GetInstance();

    LuaVM* CreateVM(const std::string& name, const LuaVMConfig& config = LuaVMConfig{});
    void DestroyVM(const std::string& name);
    LuaVM* GetVM(const std::string& name);
    LuaVM* GetDefaultVM();

    void DestroyAll();

private:
    ScriptSystemManager() = default;
    ~ScriptSystemManager();

    std::unordered_map<std::string, std::unique_ptr<LuaVM>> vms_;
    LuaVM* defaultVM_ = nullptr;
};

} // namespace Next
