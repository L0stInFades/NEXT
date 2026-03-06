#pragma once

#include <cstdint>
#include <atomic>

namespace Next {

// Forward declarations
class AssetData;

// Asset handle - opaque reference to loaded asset
class AssetHandle {
public:
    AssetHandle() : id_(0), data_(nullptr) {}
    explicit AssetHandle(uint64_t id, AssetData* data) : id_(id), data_(data) {}
    
    bool IsValid() const { return id_ != 0 && data_ != nullptr; }
    uint64_t GetID() const { return id_; }
    
    // Comparison operators
    bool operator==(const AssetHandle& other) const { return id_ == other.id_; }
    bool operator!=(const AssetHandle& other) const { return id_ != other.id_; }
    
private:
    uint64_t id_;
    AssetData* data_;
    
    friend class AssetManager;
};

// Strongly typed asset handles for type safety
template<typename T>
class TypedAssetHandle {
public:
    TypedAssetHandle() : handle_() {}
    explicit TypedAssetHandle(AssetHandle handle) : handle_(handle) {}
    
    bool IsValid() const { return handle_.IsValid(); }
    uint64_t GetID() const { return handle_.GetID(); }
    
    AssetHandle GetHandle() const { return handle_; }
    
    bool operator==(const TypedAssetHandle& other) const { return handle_ == other.handle_; }
    bool operator!=(const TypedAssetHandle& other) const { return handle_ != other.handle_; }
    
private:
    AssetHandle handle_;
};

// Predefined handle types for CP3 assets
using MeshHandle = TypedAssetHandle<class Mesh>;
using TextureHandle = TypedAssetHandle<class Texture>;
using MaterialHandle = TypedAssetHandle<class Material>;

} // namespace Next
