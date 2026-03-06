#include "next/runtime/asset/asset_manager.h"
#include "next/runtime/asset/package_container.h"
#include "next/runtime/asset/asset_types.h"
#include "next/jobsystem/job_system.h"
#include "next/foundation/logger.h"
#include "next/profiler/profiler.h"
#include "next/profiler/cpu_scope.h"
#include <algorithm>
#include <fstream>
#include <cstring>
#include <filesystem>

namespace Next {

// AssetData base class - holds actual asset data with reference counting
class AssetData {
public:
    AssetData(uint64_t id, AssetType type, std::string packageName, std::string name, size_t payloadSize)
        : id_(id)
        , type_(type)
        , packageName_(std::move(packageName))
        , name_(std::move(name))
        , refCount_(0)
        , payloadSize_(payloadSize) {}

    virtual ~AssetData() = default;

    uint64_t GetID() const { return id_; }
    AssetType GetType() const { return type_; }
    const std::string& GetPackageName() const { return packageName_; }
    const std::string& GetName() const { return name_; }

    void AddRef() { refCount_++; }
    uint32_t Release() { return --refCount_; }
    uint32_t GetRefCount() const { return refCount_.load(); }
    size_t GetPayloadSize() const { return payloadSize_; }

    virtual const void* GetPayload() const = 0;

private:
    uint64_t id_;
    AssetType type_;
    std::string packageName_;
    std::string name_;
    std::atomic<uint32_t> refCount_;
    size_t payloadSize_;
};

// Concrete asset data classes
class MeshData : public AssetData {
public:
    MeshData(uint64_t id, std::string packageName, const MeshHeader& header, const void* payload, size_t payloadSize)
        : AssetData(id, AssetType::Mesh, std::move(packageName), header.common.name, payloadSize)
        , header_(header) {
        data_.resize(payloadSize);
        memcpy(data_.data(), payload, payloadSize);
    }

    const void* GetPayload() const override { return data_.data(); }
    const MeshHeader& GetHeader() const { return header_; }

private:
    MeshHeader header_;
    std::vector<uint8_t> data_;
};

class TextureData : public AssetData {
public:
    TextureData(uint64_t id, std::string packageName, const TextureHeader& header, const void* payload, size_t payloadSize)
        : AssetData(id, AssetType::Texture, std::move(packageName), header.common.name, payloadSize)
        , header_(header) {
        data_.resize(payloadSize);
        memcpy(data_.data(), payload, payloadSize);
    }

    const void* GetPayload() const override { return data_.data(); }
    const TextureHeader& GetHeader() const { return header_; }

private:
    TextureHeader header_;
    std::vector<uint8_t> data_;
};

class MaterialData : public AssetData {
public:
    MaterialData(uint64_t id, std::string packageName, const MaterialHeader& header, const void* payload, size_t payloadSize)
        : AssetData(id, AssetType::Material, std::move(packageName), header.common.name, payloadSize)
        , header_(header) {
        data_.resize(payloadSize);
        memcpy(data_.data(), payload, payloadSize);
    }

    const void* GetPayload() const override { return data_.data(); }
    const MaterialHeader& GetHeader() const { return header_; }

private:
    MaterialHeader header_;
    std::vector<uint8_t> data_;
};

AssetManager& AssetManager::Instance() {
    static AssetManager instance;
    return instance;
}

bool AssetManager::Initialize() {
    NEXT_LOG_INFO("AssetManager initialized");
    return true;
}

void AssetManager::Shutdown() {
    NEXT_CPU_SCOPE("AssetManager::Shutdown");
    
    NEXT_LOG_INFO("AssetManager shutting down, unloading %llu assets", loadedAssetsCount_.load());
    
    // Unload all assets
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto& [name, data] : loadedAssets_) {
            NEXT_LOG_DEBUG("Unloading asset: %s", name.c_str());
        }
        
        loadedAssets_.clear();
        idToName_.clear();
        loadedPackages_.clear();
        loadedAssetsCount_ = 0;
        totalMemory_ = 0;
    }
    
    NEXT_LOG_INFO("AssetManager shutdown complete");
}

bool AssetManager::LoadPackage(const std::string& packagePath) {
    NEXT_CPU_SCOPE("AssetManager::LoadPackage");
    
    NEXT_LOG_INFO("Loading package: %s", packagePath.c_str());

    // Fast-path: if package already loaded, just bump refcount and avoid IO.
    std::string expectedName;
    try {
        expectedName = std::filesystem::path(packagePath).stem().string();
    } catch (...) {
        expectedName.clear();
    }

    if (!expectedName.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = loadedPackages_.find(expectedName);
        if (it != loadedPackages_.end()) {
            it->second.refCount++;
            NEXT_LOG_DEBUG("Package ref++: %s (ref=%u)", expectedName.c_str(), it->second.refCount);
            return true;
        }
    }

    auto package = LoadPackageInternal(packagePath);
    if (!package) {
        NEXT_LOG_ERROR("Failed to load package: %s", packagePath.c_str());
        failedLoads_++;
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = loadedPackages_.find(package->GetName());
    if (it != loadedPackages_.end()) {
        it->second.refCount++;
        NEXT_LOG_DEBUG("Package ref++ (race): %s (ref=%u)", package->GetName().c_str(), it->second.refCount);
        return true;
    }

    LoadedPackageEntry entry;
    entry.package = package;
    entry.refCount = 1;
    loadedPackages_[package->GetName()] = std::move(entry);

    NEXT_LOG_INFO("Package loaded successfully: %s (%u assets, ref=1)",
                  package->GetName().c_str(), package->GetAssetCount());
    return true;
}

void AssetManager::UnloadPackage(const std::string& packageName) {
    NEXT_CPU_SCOPE("AssetManager::UnloadPackage");
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = loadedPackages_.find(packageName);
    if (it == loadedPackages_.end()) {
        NEXT_LOG_WARNING("Package not found: %s", packageName.c_str());
        return;
    }

    if (it->second.refCount > 1) {
        it->second.refCount--;
        NEXT_LOG_DEBUG("Package ref--: %s (ref=%u)", packageName.c_str(), it->second.refCount);
        return;
    }

    // Final release: drop the package container. Assets already loaded are independent copies; keep them if referenced.
    loadedPackages_.erase(it);

    // Garbage collect unreferenced assets that originated from this package.
    for (auto aIt = loadedAssets_.begin(); aIt != loadedAssets_.end(); ) {
        const std::shared_ptr<AssetData>& a = aIt->second;
        if (a && a->GetPackageName() == packageName) {
            if (a->GetRefCount() == 0) {
                totalMemory_ -= a->GetPayloadSize();
                loadedAssetsCount_--;
                idToName_.erase(a->GetID());
                aIt = loadedAssets_.erase(aIt);
                continue;
            }
        }
        ++aIt;
    }

    NEXT_LOG_INFO("Package unloaded: %s (ref=0)", packageName.c_str());
}

AssetHandle AssetManager::LoadAssetSync(const std::string& assetName) {
    NEXT_CPU_SCOPE("AssetManager::LoadAssetSync");
    
    NEXT_LOG_DEBUG("Loading asset synchronously: %s", assetName.c_str());
    
    auto splitKey = [](const std::string& key, std::string& outPkg, std::string& outLocal) -> bool {
        const size_t pos = key.find("::");
        if (pos == std::string::npos) {
            return false;
        }
        outPkg = key.substr(0, pos);
        outLocal = key.substr(pos + 2);
        return !outPkg.empty() && !outLocal.empty();
    };

    auto resolveLoadedKey = [&](const std::string& key) -> std::string {
        // Exact match (already-qualified).
        auto itExact = loadedAssets_.find(key);
        if (itExact != loadedAssets_.end()) {
            return key;
        }

        // Unqualified: resolve by local name, but only if unique among loaded assets.
        std::string pkg;
        std::string local;
        if (splitKey(key, pkg, local)) {
            return {};
        }

        std::string foundKey;
        for (const auto& [k, a] : loadedAssets_) {
            if (!a) {
                continue;
            }
            if (a->GetName() == key) {
                if (!foundKey.empty() && foundKey != k) {
                    // Ambiguous: multiple loaded assets share the same local name.
                    return {};
                }
                foundKey = k;
            }
        }
        return foundKey;
    };

    // Check if already loaded
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string loadedKey = resolveLoadedKey(assetName);
        if (!loadedKey.empty()) {
            auto it = loadedAssets_.find(loadedKey);
            if (it != loadedAssets_.end()) {
                it->second->AddRef();
                NEXT_LOG_DEBUG("Asset already loaded: %s (refcount: %u)",
                               loadedKey.c_str(), it->second->GetRefCount());
                return AssetHandle(it->second->GetID(), it->second.get());
            }
        }
    }
    
    // Find asset in loaded packages
    std::shared_ptr<PackageContainer> package;
    std::string packageName;
    std::string localName = assetName;
    {
        std::string requestedPkg;
        std::string requestedLocal;
        if (splitKey(assetName, requestedPkg, requestedLocal)) {
            packageName = requestedPkg;
            localName = requestedLocal;
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!packageName.empty()) {
            auto it = loadedPackages_.find(packageName);
            if (it != loadedPackages_.end() && it->second.package && it->second.package->HasAsset(localName)) {
                package = it->second.package;
            }
        } else {
            // Unqualified: must be unique across loaded packages.
            std::string foundPkg;
            for (const auto& [pkgNameIt, entry] : loadedPackages_) {
                if (entry.package && entry.package->HasAsset(localName)) {
                    if (!foundPkg.empty() && foundPkg != pkgNameIt) {
                        NEXT_LOG_ERROR("Asset name is ambiguous across packages: %s (e.g. %s::%s and %s::%s). Please qualify with pkg::asset.",
                                       localName.c_str(),
                                       foundPkg.c_str(), localName.c_str(),
                                       pkgNameIt.c_str(), localName.c_str());
                        failedLoads_++;
                        return AssetHandle();
                    }
                    foundPkg = pkgNameIt;
                    package = entry.package;
                }
            }
            packageName = foundPkg;
        }
    }
    
    if (!package) {
        NEXT_LOG_ERROR("Asset not found in any loaded package: %s", assetName.c_str());
        failedLoads_++;
        return AssetHandle();
    }

    const std::string storageKey = packageName.empty() ? assetName : (packageName + "::" + localName);
    
    // Read asset data
    std::vector<uint8_t> assetData;
    if (!package->ReadAssetData(localName, assetData)) {
        NEXT_LOG_ERROR("Failed to read asset data: %s", storageKey.c_str());
        failedLoads_++;
        return AssetHandle();
    }
    
    if (assetData.size() < sizeof(AssetHeader)) {
        NEXT_LOG_ERROR("Asset data too small: %s", storageKey.c_str());
        failedLoads_++;
        return AssetHandle();
    }

    AssetHeader commonHeader;
    memcpy(&commonHeader, assetData.data(), sizeof(AssetHeader));

    if (!commonHeader.Validate()) {
        NEXT_LOG_ERROR("Invalid asset header: %s", storageKey.c_str());
        failedLoads_++;
        return AssetHandle();
    }

    std::shared_ptr<AssetData> data;
    uint64_t id = nextAssetID_++;

    switch (commonHeader.assetType) {
        case AssetType::Mesh: {
            if (assetData.size() < sizeof(MeshHeader)) {
                NEXT_LOG_ERROR("Mesh data too small: %s", storageKey.c_str());
                failedLoads_++;
                return AssetHandle();
            }
            MeshHeader meshHeader;
            memcpy(&meshHeader, assetData.data(), sizeof(MeshHeader));
            if (!ValidateMeshHeader(meshHeader)) {
                failedLoads_++;
                return AssetHandle();
            }
            size_t payloadSize = assetData.size() - sizeof(MeshHeader);
            data = std::make_shared<MeshData>(id, packageName, meshHeader, assetData.data() + sizeof(MeshHeader), payloadSize);
            break;
        }
        case AssetType::Texture: {
            if (assetData.size() < sizeof(TextureHeader)) {
                NEXT_LOG_ERROR("Texture data too small: %s", storageKey.c_str());
                failedLoads_++;
                return AssetHandle();
            }
            TextureHeader texHeader;
            memcpy(&texHeader, assetData.data(), sizeof(TextureHeader));
            if (!ValidateTextureHeader(texHeader)) {
                failedLoads_++;
                return AssetHandle();
            }
            size_t payloadSize = assetData.size() - sizeof(TextureHeader);
            data = std::make_shared<TextureData>(id, packageName, texHeader, assetData.data() + sizeof(TextureHeader), payloadSize);
            break;
        }
        case AssetType::Material: {
            if (assetData.size() < sizeof(MaterialHeader)) {
                NEXT_LOG_ERROR("Material data too small: %s", storageKey.c_str());
                failedLoads_++;
                return AssetHandle();
            }
            MaterialHeader matHeader;
            memcpy(&matHeader, assetData.data(), sizeof(MaterialHeader));
            if (!ValidateMaterialHeader(matHeader)) {
                failedLoads_++;
                return AssetHandle();
            }
            size_t payloadSize = assetData.size() - sizeof(MaterialHeader);
            data = std::make_shared<MaterialData>(id, packageName, matHeader, assetData.data() + sizeof(MaterialHeader), payloadSize);
            break;
        }
        default:
            NEXT_LOG_ERROR("Unknown asset type: %u", static_cast<uint32_t>(commonHeader.assetType));
            failedLoads_++;
            return AssetHandle();
    }

    if (!data) {
        NEXT_LOG_ERROR("Failed to create asset data: %s", assetName.c_str());
        failedLoads_++;
        return AssetHandle();
    }

    // Store in loaded assets
    std::lock_guard<std::mutex> lock(mutex_);
    data->AddRef(); // Initial reference

    loadedAssets_[storageKey] = data;
    idToName_[id] = storageKey;

    loadedAssetsCount_++;
    totalMemory_ += data->GetPayloadSize();

    NEXT_LOG_INFO("Asset loaded successfully: %s (ID: %llu, size: %llu bytes)",
                  storageKey.c_str(), id, data->GetPayloadSize());

    return AssetHandle(id, data.get());
}

void AssetManager::LoadAssetAsync(const std::string& assetName, AssetLoadCallback callback) {
    NEXT_CPU_SCOPE("AssetManager::LoadAssetAsync");
    
    pendingLoads_++;
    NEXT_LOG_DEBUG("Queueing async asset load: %s", assetName.c_str());
    
    // Submit job to JobSystem
    auto& jobSystem = JobSystem::Instance();
    jobSystem.Submit([this, assetName, callback]() {
        AssetLoadResult result;
        result.handle = LoadAssetSync(assetName);
        result.success = result.handle.IsValid();
        
        if (!result.success) {
            result.errorMessage = "Failed to load asset: " + assetName;
        }
        
        pendingLoads_--;
        
        // Call callback (would need to be on main thread in real implementation)
        if (callback) {
            callback(result);
        }
    }, JobPriority::Normal);
}

void AssetManager::UnloadAsset(const AssetHandle& handle) {
    if (!handle.IsValid()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = idToName_.find(handle.GetID());
    if (it == idToName_.end()) {
        return;
    }

    // Make a copy of the name before we potentially erase it
    std::string name = it->second;
    auto assetIt = loadedAssets_.find(name);
    if (assetIt == loadedAssets_.end()) {
        return;
    }

    // Release reference and cleanup when 0
    uint32_t refs = assetIt->second->Release();
    if (refs == 0) {
        totalMemory_ -= assetIt->second->GetPayloadSize();
        loadedAssetsCount_--;
        loadedAssets_.erase(assetIt);
        idToName_.erase(it);
        NEXT_LOG_INFO("Asset unloaded: %s", name.c_str());
    } else {
        NEXT_LOG_DEBUG("Asset reference released: %s (refcount: %u)", name.c_str(), refs);
    }
}

void AssetManager::UnloadAsset(const std::string& assetName) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = loadedAssets_.find(assetName);
    if (it == loadedAssets_.end()) {
        // Unqualified: try resolve by local name if unique.
        std::string found;
        for (const auto& [k, a] : loadedAssets_) {
            if (a && a->GetName() == assetName) {
                if (!found.empty() && found != k) {
                    NEXT_LOG_WARNING("UnloadAsset ambiguous (multiple packages): %s. Use pkg::asset.", assetName.c_str());
                    return;
                }
                found = k;
            }
        }
        if (found.empty()) {
            NEXT_LOG_WARNING("Asset not found for unloading: %s", assetName.c_str());
            return;
        }
        it = loadedAssets_.find(found);
        if (it == loadedAssets_.end()) {
            NEXT_LOG_WARNING("Asset not found for unloading: %s", assetName.c_str());
            return;
        }
    }
    
    uint32_t refs = it->second->Release();
    if (refs == 0) {
        totalMemory_ -= it->second->GetPayloadSize();
        loadedAssetsCount_--;
        idToName_.erase(it->second->GetID());
        loadedAssets_.erase(it);
        NEXT_LOG_INFO("Asset unloaded: %s", assetName.c_str());
    } else {
        NEXT_LOG_DEBUG("Asset reference released: %s (refcount: %u)", 
                      assetName.c_str(), refs);
    }
}

bool AssetManager::IsAssetLoaded(const std::string& assetName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (loadedAssets_.find(assetName) != loadedAssets_.end()) {
        return true;
    }
    // Unqualified: check by local name (unique not enforced for queries).
    for (const auto& [k, a] : loadedAssets_) {
        if (a && a->GetName() == assetName) {
            return true;
        }
    }
    return false;
}

AssetHandle AssetManager::GetAssetHandle(const std::string& assetName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = loadedAssets_.find(assetName);
    if (it == loadedAssets_.end()) {
        // Unqualified: resolve by local name if unique among loaded assets.
        std::string found;
        for (const auto& [k, a] : loadedAssets_) {
            if (a && a->GetName() == assetName) {
                if (!found.empty() && found != k) {
                    return AssetHandle();
                }
                found = k;
            }
        }
        if (found.empty()) {
            return AssetHandle();
        }
        auto it2 = loadedAssets_.find(found);
        if (it2 == loadedAssets_.end()) {
            return AssetHandle();
        }
        return AssetHandle(it2->second->GetID(), it2->second.get());
    }
    
    return AssetHandle(it->second->GetID(), it->second.get());
}

void AssetManager::AddRef(const AssetHandle& handle) {
    if (!handle.IsValid()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = idToName_.find(handle.GetID());
    if (it == idToName_.end()) {
        return;
    }
    
    const std::string& name = it->second;
    auto assetIt = loadedAssets_.find(name);
    if (assetIt == loadedAssets_.end()) {
        return;
    }
    
    assetIt->second->AddRef();
}

void AssetManager::Release(const AssetHandle& handle) {
    UnloadAsset(handle);
}

uint32_t AssetManager::GetRefCount(const AssetHandle& handle) const {
    if (!handle.IsValid()) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = idToName_.find(handle.GetID());
    if (it == idToName_.end()) {
        return 0;
    }
    
    const std::string& name = it->second;
    auto assetIt = loadedAssets_.find(name);
    if (assetIt == loadedAssets_.end()) {
        return 0;
    }
    
    return assetIt->second->GetRefCount();
}

AssetStats AssetManager::GetStats() const {
    AssetStats stats;
    stats.loadedAssets = loadedAssetsCount_.load();
    stats.totalMemory = totalMemory_.load();
    stats.pendingLoads = pendingLoads_.load();
    stats.failedLoads = failedLoads_.load();
    return stats;
}

std::shared_ptr<PackageContainer> AssetManager::GetPackage(const std::string& packageName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = loadedPackages_.find(packageName);
    if (it == loadedPackages_.end()) {
        return nullptr;
    }
    
    return it->second.package;
}

uint32_t AssetManager::GetPackageRefCount(const std::string& packageName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = loadedPackages_.find(packageName);
    if (it == loadedPackages_.end()) {
        return 0;
    }
    return it->second.refCount;
}

std::shared_ptr<PackageContainer> AssetManager::LoadPackageInternal(const std::string& packagePath) {
    return PackageContainer::LoadFromFile(packagePath);
}

} // namespace Next
