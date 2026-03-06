#include "next/script/script_system.h"
#include "next/runtime/world.h"
#include "next/runtime/entity.h"
#include "next/runtime/event_bus.h"
#include "next/log/log.h"

#include <fstream>
#include <sstream>
#include <chrono>
#include <cstring>

namespace Next {

// 使用统一日志系统
// 直接使用 NEXT_LOG_* 宏，不再需要中间宏

// ============================================================================
// LuaVM实现（框架版本）
// ============================================================================

LuaVM::LuaVM(const LuaVMConfig& config)
    : L_(nullptr)
    , config_(config)
{
    std::memset(&stats_, 0, sizeof(stats_));
}

LuaVM::~LuaVM() {
    Shutdown();
}

bool LuaVM::Initialize() {
    if (L_) {
        NEXT_LOG_WARN() << "LuaVM::Initialize: VM already initialized";
        return true;
    }

    // 框架实现：不创建实际的Lua State
    NEXT_LOG_INFO() << "LuaVM::Initialize: Lua VM initialized (framework mode)";
    NEXT_LOG_INFO() << "  To enable full Lua support, set USE_SYSTEM_LUA=ON in CMake";

    return true;
}

void LuaVM::Shutdown() {
    if (!L_) {
        return;
    }

    // 卸载所有脚本
    for (auto& [id, script] : scripts_) {
        NEXT_LOG_INFO() << "LuaVM::Shutdown: Unloading script '" << script.name << "'";
    }

    scripts_.clear();
    L_ = nullptr;

    NEXT_LOG_INFO() << "LuaVM::Shutdown: Lua VM shut down";
}

LuaVM::ScriptID LuaVM::LoadScript(const std::string& scriptPath) {
    std::ifstream file(scriptPath);
    if (!file.is_open()) {
        NEXT_LOG_ERROR() << "LuaVM::LoadScript: Failed to open script: " << scriptPath;
        return 0;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    return LoadScriptFromString(scriptPath, content);
}

LuaVM::ScriptID LuaVM::LoadScriptFromString(const std::string& scriptName, const std::string& scriptContent) {
    auto startTime = std::chrono::high_resolution_clock::now();

    // 框架实现：只记录脚本，不实际加载
    ScriptID id = nextScriptID_++;
    ScriptInfo info;
    info.id = id;
    info.name = scriptName;
    info.path = scriptName;
    info.content = scriptContent;
    info.isValid = true;
    scripts_[id] = info;

    auto endTime = std::chrono::high_resolution_clock::now();
    double loadTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    stats_.scriptCount++;
    stats_.activeScripts++;

    NEXT_LOG_INFO() << "LuaVM::LoadScriptFromString: Loaded script '" << scriptName
                    << "' (ID: " << id << ", Time: " << loadTimeMs << " ms)";

    return id;
}

void LuaVM::UnloadScript(ScriptID scriptID) {
    auto it = scripts_.find(scriptID);
    if (it == scripts_.end()) {
        return;
    }

    it->second.isValid = false;
    stats_.activeScripts--;

    NEXT_LOG_INFO() << "LuaVM::UnloadScript: Unloaded script '" << it->second.name
                    << "' (ID: " << scriptID << ")";

    scripts_.erase(it);
}

bool LuaVM::ReloadScript(ScriptID scriptID) {
    auto it = scripts_.find(scriptID);
    if (it == scripts_.end()) {
        return false;
    }

    const ScriptInfo& oldScript = it->second;
    UnloadScript(scriptID);
    ScriptID newID = LoadScriptFromString(oldScript.name, oldScript.content);

    NEXT_LOG_INFO() << "LuaVM::ReloadScript: Reloaded script '" << oldScript.name << "'";

    return newID != 0;
}

bool LuaVM::CallScriptFunction(ScriptID scriptID, const std::string& functionName) {
    auto it = scripts_.find(scriptID);
    if (it == scripts_.end() || !it->second.isValid) {
        return false;
    }

    // 框架实现：只记录调用
    stats_.averageExecutionTimeMs = 0.1; // 模拟执行时间

    return true;
}

void LuaVM::RegisterCFunction(const std::string& luaName, void* function) {
    NEXT_LOG_INFO() << "LuaVM::RegisterCFunction: Registered function '" << luaName << "'";
}

bool LuaVM::ExecuteString(const std::string& code) {
    NEXT_LOG_INFO() << "LuaVM::ExecuteString: Executing code snippet (" << code.length() << " chars)";
    return true;
}

bool LuaVM::ExecuteFile(const std::string& filePath) {
    ScriptID id = LoadScript(filePath);
    return id != 0;
}

void LuaVM::Update(float deltaTime) {
    // 框架实现：只更新统计
}

void LuaVM::ResetStats() {
    std::memset(&stats_, 0, sizeof(stats_));
}

// ============================================================================
// LuaBindings实现
// ============================================================================

std::function<bool(LuaAPIPermission)> LuaBindings::permissionCallback_ =
    [](LuaAPIPermission perm) {
        return perm == LuaAPIPermission::Safe || perm == LuaAPIPermission::Restricted;
    };

void LuaBindings::Initialize(void* L) {
    NEXT_LOG_INFO() << "LuaBindings::Initialize: Bindings initialized";
}

void LuaBindings::RegisterAllBindings(void* L) {
    RegisterMathAPI(L);
    RegisterLogAPI(L);
    RegisterTimeAPI(L);
    NEXT_LOG_INFO() << "LuaBindings::RegisterAllBindings: All bindings registered";
}

void LuaBindings::RegisterWorldAPI(void* L, World* world) {
    NEXT_LOG_INFO() << "LuaBindings::RegisterWorldAPI: World API registered";
}

void LuaBindings::RegisterEntityAPI(void* L) {
    NEXT_LOG_INFO() << "LuaBindings::RegisterEntityAPI: Entity API registered";
}

void LuaBindings::RegisterEventAPI(void* L, EventBus* eventBus) {
    NEXT_LOG_INFO() << "LuaBindings::RegisterEventAPI: Event API registered";
}

void LuaBindings::RegisterMathAPI(void* L) {
    NEXT_LOG_INFO() << "LuaBindings::RegisterMathAPI: Math API registered";
}

void LuaBindings::RegisterLogAPI(void* L) {
    NEXT_LOG_INFO() << "LuaBindings::RegisterLogAPI: Log API registered";
}

void LuaBindings::RegisterTimeAPI(void* L) {
    NEXT_LOG_INFO() << "LuaBindings::RegisterTimeAPI: Time API registered";
}

void LuaBindings::SetPermissionCheckCallback(std::function<bool(LuaAPIPermission)> callback) {
    permissionCallback_ = callback;
}

bool LuaBindings::CheckPermission(LuaAPIPermission permission) {
    return permissionCallback_ ? permissionCallback_(permission) : false;
}

// ============================================================================
// ScriptComponent实现
// ============================================================================

bool ScriptComponent::CallFunction(const std::string& functionName, LuaVM* vm) {
    if (!vm || !isEnabled) {
        return false;
    }

    return vm->CallScriptFunction(scriptID, functionName);
}

void ScriptComponent::SetParameter(const std::string& key, const std::string& value) {
    config.parameters[key] = value;
}

std::string ScriptComponent::GetParameter(const std::string& key) const {
    auto it = config.parameters.find(key);
    return it != config.parameters.end() ? it->second : "";
}

// ============================================================================
// ScriptSystem实现
// ============================================================================

ScriptSystem::ScriptSystem(World* world, LuaVM* vm)
    : world_(world)
    , vm_(vm)
{
    std::memset(&stats_, 0, sizeof(stats_));
}

ScriptSystem::~ScriptSystem() {
    // 清理所有脚本组件
    for (auto& [entityID, scriptMap] : entityScripts_) {
        for (auto& [scriptPath, component] : scriptMap) {
            UnloadScript(&component);
        }
    }
}

void ScriptSystem::Initialize() {
    if (!world_ || !vm_) {
        NEXT_LOG_ERROR() << "ScriptSystem::Initialize: World or Lua VM not set";
        return;
    }

    NEXT_LOG_INFO() << "ScriptSystem::Initialize: Script system initialized";
}

void ScriptSystem::Update(float deltaTime) {
    auto startTime = std::chrono::high_resolution_clock::now();

    // 更新所有启用的脚本
    for (auto& scriptRef : allScripts_) {
        ScriptComponent* script = scriptRef.component;
        if (!script || !script->isEnabled) {
            continue;
        }

        // 检查更新间隔
        if (script->config.updateInterval > 0.0f) {
            script->timeSinceLastUpdate += deltaTime;
            if (script->timeSinceLastUpdate < script->config.updateInterval) {
                continue;
            }
            script->timeSinceLastUpdate = 0.0f;
        }

        UpdateScript(script, deltaTime);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double updateTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    stats_.totalUpdateTimeMs += updateTimeMs;
    if (stats_.totalCalls > 0) {
        stats_.averageUpdateTimeMs = stats_.totalUpdateTimeMs / stats_.totalCalls;
    }
}

ScriptComponent* ScriptSystem::AddScriptComponent(Entity entity, const std::string& scriptPath,
                                                 const ScriptComponentConfig* config) {
    if (!world_ || !vm_) {
        return nullptr;
    }

    uint64_t entityID = static_cast<uint64_t>(entity);

    // 检查是否已存在
    auto entityIt = entityScripts_.find(entityID);
    if (entityIt != entityScripts_.end() && entityIt->second.find(scriptPath) != entityIt->second.end()) {
        NEXT_LOG_WARN() << "ScriptSystem::AddScriptComponent: Script '" << scriptPath
                        << "' already exists on entity";
        return &entityIt->second[scriptPath];
    }

    // 创建脚本组件
    ScriptComponent component(entity, scriptPath);

    if (config) {
        component.config = *config;
    }

    // 加载脚本
    if (!LoadScript(&component)) {
        NEXT_LOG_ERROR() << "ScriptSystem::AddScriptComponent: Failed to load script '" << scriptPath << "'";
        return nullptr;
    }

    // 添加到映射
    entityScripts_[entityID][scriptPath] = component;
    ScriptComponent* componentPtr = &entityScripts_[entityID][scriptPath];

    // 添加到扁平列表
    allScripts_.push_back({entityID, componentPtr});

    // 更新统计
    stats_.totalScripts++;
    stats_.activeScripts++;

    // 自动启动
    if (component.config.autoStart) {
        StartScript(componentPtr);
    }

    NEXT_LOG_INFO() << "ScriptSystem::AddScriptComponent: Added script '" << scriptPath
                    << "' to entity " << entityID;

    return componentPtr;
}

void ScriptSystem::RemoveScriptComponent(Entity entity, const std::string& scriptPath) {
    uint64_t entityID = static_cast<uint64_t>(entity);

    auto entityIt = entityScripts_.find(entityID);
    if (entityIt == entityScripts_.end()) {
        return;
    }

    auto scriptIt = entityIt->second.find(scriptPath);
    if (scriptIt == entityIt->second.end()) {
        return;
    }

    UnloadScript(&scriptIt->second);

    // 从扁平列表移除
    allScripts_.erase(
        std::remove_if(allScripts_.begin(), allScripts_.end(),
            [&](const ScriptRef& ref) {
                return ref.entity == entityID && ref.component->scriptPath == scriptPath;
            }),
        allScripts_.end()
    );

    entityIt->second.erase(scriptIt);
    if (entityIt->second.empty()) {
        entityScripts_.erase(entityIt);
    }

    stats_.totalScripts--;
    stats_.activeScripts--;

    NEXT_LOG_INFO() << "ScriptSystem::RemoveScriptComponent: Removed script '" << scriptPath
                    << "' from entity " << entityID;
}

ScriptComponent* ScriptSystem::GetScriptComponent(Entity entity, const std::string& scriptPath) {
    uint64_t entityID = static_cast<uint64_t>(entity);

    auto entityIt = entityScripts_.find(entityID);
    if (entityIt == entityScripts_.end()) {
        return nullptr;
    }

    auto scriptIt = entityIt->second.find(scriptPath);
    if (scriptIt == entityIt->second.end()) {
        return nullptr;
    }

    return &scriptIt->second;
}

void ScriptSystem::EnableScript(Entity entity, const std::string& scriptPath) {
    ScriptComponent* script = GetScriptComponent(entity, scriptPath);
    if (script) {
        script->isEnabled = true;
        CallScriptCallback(script, "OnEnable");
    }
}

void ScriptSystem::DisableScript(Entity entity, const std::string& scriptPath) {
    ScriptComponent* script = GetScriptComponent(entity, scriptPath);
    if (script) {
        script->isEnabled = false;
        CallScriptCallback(script, "OnDisable");
    }
}

ScriptSystem::SystemStats ScriptSystem::GetStats() const {
    return stats_;
}

void ScriptSystem::ResetStats() {
    std::memset(&stats_, 0, sizeof(stats_));
}

void ScriptSystem::StartScript(ScriptComponent* script) {
    if (!script || script->isStarted) {
        return;
    }

    CallScriptCallback(script, "OnStart");
    script->isStarted = true;
}

void ScriptSystem::UpdateScript(ScriptComponent* script, float deltaTime) {
    if (!script || !script->isEnabled) {
        return;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    CallScriptCallback(script, "OnUpdate");

    auto endTime = std::chrono::high_resolution_clock::now();
    double updateTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    script->stats.updateCount++;
    script->stats.totalUpdateTimeMs += updateTimeMs;
    script->stats.averageUpdateTimeMs =
        script->stats.totalUpdateTimeMs / script->stats.updateCount;

    stats_.totalCalls++;
}

void ScriptSystem::CallScriptCallback(ScriptComponent* script, const std::string& callbackName) {
    if (!vm_ || !script) {
        return;
    }

    vm_->CallScriptFunction(script->scriptID, callbackName);
}

bool ScriptSystem::LoadScript(ScriptComponent* script) {
    if (!vm_) {
        return false;
    }

    LuaVM::ScriptID id = vm_->LoadScript(script->scriptPath);
    if (id == 0) {
        return false;
    }

    script->scriptID = id;
    script->isStarted = false;

    return true;
}

void ScriptSystem::UnloadScript(ScriptComponent* script) {
    if (!vm_ || script->scriptID == 0) {
        return;
    }

    CallScriptCallback(script, "OnDestroy");
    vm_->UnloadScript(script->scriptID);
    script->scriptID = 0;
}

// ============================================================================
// ScriptSystemManager实现
// ============================================================================

ScriptSystemManager& ScriptSystemManager::GetInstance() {
    static ScriptSystemManager instance;
    return instance;
}

ScriptSystemManager::~ScriptSystemManager() {
    DestroyAll();
}

LuaVM* ScriptSystemManager::CreateVM(const std::string& name, const LuaVMConfig& config) {
    auto vm = std::make_unique<LuaVM>(config);
    if (!vm->Initialize()) {
        return nullptr;
    }

    LuaVM* ptr = vm.get();
    vms_[name] = std::move(vm);

    if (!defaultVM_) {
        defaultVM_ = ptr;
    }

    return ptr;
}

void ScriptSystemManager::DestroyVM(const std::string& name) {
    auto it = vms_.find(name);
    if (it != vms_.end()) {
        if (it->second.get() == defaultVM_) {
            defaultVM_ = nullptr;
        }
        vms_.erase(it);
    }
}

LuaVM* ScriptSystemManager::GetVM(const std::string& name) {
    auto it = vms_.find(name);
    return it != vms_.end() ? it->second.get() : nullptr;
}

LuaVM* ScriptSystemManager::GetDefaultVM() {
    return defaultVM_;
}

void ScriptSystemManager::DestroyAll() {
    vms_.clear();
    defaultVM_ = nullptr;
}

} // namespace Next
