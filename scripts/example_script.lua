-- CP8 示例脚本
-- 这是一个简单的Lua脚本示例，展示如何使用引擎API

-- @script_metadata {
--   name = "ExampleScript",
--   description = "A simple example script",
--   version = "1.0.0",
--   author = "Engine Team"
-- }

-- 脚本本地变量
local counter = 0
local message = "Hello from Lua!"

-- OnStart: 脚本启动时调用一次
function OnStart()
    Log.Info("ExampleScript started!")
    Log.Info(message)

    -- 创建一个向量
    local position = Vec3.new(0, 1, 2)
    Log.Info("Position: " .. position.x .. ", " .. position.y .. ", " .. position.z)
end

-- OnUpdate: 每帧调用
function OnUpdate()
    local deltaTime = Time.GetDeltaTime()
    local currentTime = Time.GetTime()

    counter = counter + 1

    if counter % 60 == 0 then
        Log.Info("Update: frame " .. counter .. ", time: " .. currentTime)
    end
end

-- OnEnable: 组件启用时调用
function OnEnable()
    Log.Info("ExampleScript enabled")
end

-- OnDisable: 组件禁用时调用
function OnDisable()
    Log.Info("ExampleScript disabled")
end

-- OnDestroy: 组件销毁时调用
function OnDestroy()
    Log.Info("ExampleScript destroyed! Total updates: " .. counter)
end

-- 自定义函数：移动实体
function MoveEntity(dx, dy, dz)
    local transform = Transform.GetPosition()
    if transform then
        Log.Info("Moving entity by: " .. dx .. ", " .. dy .. ", " .. dz)
        -- Transform.SetPosition(transform.x + dx, transform.y + dy, transform.z + dz)
    end
end

-- 自定义函数：计算距离
function DistanceTo(x1, y1, z1, x2, y2, z2)
    local v1 = Vec3.new(x1, y1, z1)
    local v2 = Vec3.new(x2, y2, z2)
    local diff = v2 - v1
    return diff:Length()
end

-- 测试数学库
function TestMath()
    local v1 = Vec3.new(1, 2, 3)
    local v2 = Vec3.new(4, 5, 6)

    local v3 = v1 + v2
    local dot = v1:Dot(v2)
    local len = v1:Length()

    Log.Info("Vector add: " .. v3.x .. ", " .. v3.y .. ", " .. v3.z)
    Log.Info("Dot product: " .. dot)
    Log.Info("Length: " .. len)
end

-- 返回公共API表（可选）
return {
    MoveEntity = MoveEntity,
    DistanceTo = DistanceTo,
    TestMath = TestMath
}
