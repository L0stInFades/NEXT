-- Player Controller Script
-- 玩家控制器示例脚本

-- @script_metadata {
--   name = "PlayerController",
--   description = "Controls player movement and interaction",
--   category = "gameplay",
--   version = "1.0.0"
-- }

-- 玩家状态
local playerState = {
    position = Vec3.new(0, 0, 0),
    rotation = Vec3.new(0, 0, 0),
    velocity = Vec3.new(0, 0, 0),
    speed = 5.0,
    isGrounded = true,
    health = 100
}

-- 输入状态
local inputState = {
    forward = false,
    backward = false,
    left = false,
    right = false,
    jump = false
}

function OnStart()
    Log.Info("PlayerController: Started")
    Log.Info("Initial health: " .. playerState.health)

    -- 设置初始位置
    Transform.SetPosition(0, 1, 0)
end

function OnUpdate()
    local deltaTime = Time.GetDeltaTime()

    -- 处理移动输入
    HandleMovement(deltaTime)

    -- 应用重力
    ApplyGravity(deltaTime)

    -- 更新位置
    UpdatePosition(deltaTime)

    -- 检查生命值
    if playerState.health <= 0 then
        OnDeath()
    end
end

function HandleMovement(deltaTime)
    local moveDir = Vec3.new(0, 0, 0)

    if inputState.forward then
        moveDir.z = moveDir.z + 1
    end
    if inputState.backward then
        moveDir.z = moveDir.z - 1
    end
    if inputState.left then
        moveDir.x = moveDir.x - 1
    end
    if inputState.right then
        moveDir.x = moveDir.x + 1
    end

    -- 归一化移动方向
    if moveDir:Length() > 0 then
        moveDir = moveDir:Normalize()
        playerState.velocity.x = moveDir.x * playerState.speed
        playerState.velocity.z = moveDir.z * playerState.speed
    else
        playerState.velocity.x = 0
        playerState.velocity.z = 0
    end

    -- 跳跃
    if inputState.jump and playerState.isGrounded then
        playerState.velocity.y = 5.0
        playerState.isGrounded = false
        Log.Info("Player jumped!")
    end
end

function ApplyGravity(deltaTime)
    if not playerState.isGrounded then
        playerState.velocity.y = playerState.velocity.y - 9.8 * deltaTime
    end
end

function UpdatePosition(deltaTime)
    -- 更新位置
    playerState.position.x = playerState.position.x + playerState.velocity.x * deltaTime
    playerState.position.y = playerState.position.y + playerState.velocity.y * deltaTime
    playerState.position.z = playerState.position.z + playerState.velocity.z * deltaTime

    -- 地面检测
    if playerState.position.y <= 0 then
        playerState.position.y = 0
        playerState.velocity.y = 0
        playerState.isGrounded = true
    end

    -- 应用到Transform
    Transform.SetPosition(
        playerState.position.x,
        playerState.position.y,
        playerState.position.z
    )
end

function OnDamage(amount)
    playerState.health = playerState.health - amount
    Log.Warning("Player took " .. amount .. " damage! Health: " .. playerState.health)
end

function OnDeath()
    Log.Error("Player died!")
    -- 可以在这里添加死亡逻辑
end

function OnCollisionEnter(other)
    Log.Info("Player collision with entity: " .. tostring(other))
end

-- 输入回调（由引擎调用）
function OnKeyDown(key)
    if key == "W" then
        inputState.forward = true
    elseif key == "S" then
        inputState.backward = true
    elseif key == "A" then
        inputState.left = true
    elseif key == "D" then
        inputState.right = true
    elseif key == "Space" then
        inputState.jump = true
    end
end

function OnKeyUp(key)
    if key == "W" then
        inputState.forward = false
    elseif key == "S" then
        inputState.backward = false
    elseif key == "A" then
        inputState.left = false
    elseif key == "D" then
        inputState.right = false
    elseif key == "Space" then
        inputState.jump = false
    end
end

-- 获取玩家状态（供其他脚本使用）
function GetPlayerState()
    return playerState
end

-- 设置玩家速度
function SetSpeed(speed)
    playerState.speed = speed
    Log.Info("Player speed set to: " .. speed)
end

return {
    GetPlayerState = GetPlayerState,
    SetSpeed = SetSpeed,
    OnDamage = OnDamage
}
