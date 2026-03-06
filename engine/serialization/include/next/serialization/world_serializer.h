#pragma once

#include "next/serialization/serialization.h"
#include "next/runtime/world.h"
#include "next/runtime/entity.h"
#include "next/runtime/component.h"
#include "next/runtime/transform.h"

namespace Next {

/**
 * @brief World 序列化器
 * 
 * 支持序列化和反序列化整个 ECS World 状态
 */
class WorldSerializer {
public:
    /**
     * @brief 序列化 World 到文件
     * @param world 要序列化的 World
     * @param filePath 输出文件路径
     * @param format 序列化格式
     * @return 序列化结果
     */
    static SerializationResult SaveWorld(const World& world, 
                                          const std::string& filePath,
                                          SerializationFormat format = SerializationFormat::JSON);
    
    /**
     * @brief 从文件加载 World
     * @param world 要加载到的 World
     * @param filePath 输入文件路径
     * @param format 序列化格式
     * @return 序列化结果
     */
    static SerializationResult LoadWorld(World& world, 
                                          const std::string& filePath,
                                          SerializationFormat format = SerializationFormat::JSON);
    
    /**
     * @brief 序列化 World 到字符串
     * @param world 要序列化的 World
     * @param format 序列化格式
     * @return JSON 字符串
     */
    static std::string WorldToString(const World& world,
                                     SerializationFormat format = SerializationFormat::JSON);

private:
    // 序列化实体
    static void SerializeEntity(Serializer* serializer, const World& world, Entity entity);
    
    // 反序列化实体
    static Entity DeserializeEntity(Deserializer* deserializer, World& world);
    
    // 序列化组件
    static void SerializeComponent(Serializer* serializer, const std::string& typeName,
                                   const World& world, Entity entity);
    
    // 组件类型注册
    static void RegisterComponentTypes();
    
    // 获取组件类型名称
    static std::string GetComponentTypeName(ComponentTypeID typeID);
    
    // 通过名称获取组件类型ID
    static ComponentTypeID GetComponentTypeID(const std::string& name);
};

/**
 * @brief TransformComponent 序列化辅助类
 */
struct TransformComponentSerializer : public ISerializable {
    TransformComponent data;
    
    explicit TransformComponentSerializer(const TransformComponent& transform = {}) 
        : data(transform) {}
    
    void Serialize(Serializer* serializer) const override;
    void Deserialize(Deserializer* deserializer) override;
    uint32_t GetVersion() const override { return 1; }
    const char* GetTypeName() const override { return "TransformComponent"; }
};

/**
 * @brief NameComponent 序列化辅助类
 */
struct NameComponentSerializer : public ISerializable {
    std::string name;
    
    explicit NameComponentSerializer(const std::string& n = "") 
        : name(n) {}
    
    void Serialize(Serializer* serializer) const override;
    void Deserialize(Deserializer* deserializer) override;
    uint32_t GetVersion() const override { return 1; }
    const char* GetTypeName() const override { return "NameComponent"; }
};

} // namespace Next
