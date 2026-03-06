#pragma once

#include "next/runtime/asset/asset_handle.h"
#include "next/runtime/asset/asset_types.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <mutex>
#include <functional>

namespace Next {

// Forward declarations
class AssetData;
class PackageContainer;

/**
 * @brief Result of an asset load operation
 */
struct AssetLoadResult {
    AssetHandle handle;           ///< Handle to the loaded asset
    bool success;                 ///< true if load succeeded
    std::string errorMessage;     ///< Error message if load failed
};

/**
 * @brief Callback function type for async asset loading
 *
 * Called when an async load operation completes.
 */
using AssetLoadCallback = std::function<void(const AssetLoadResult&)>;

/**
 * @brief Statistics snapshot for the asset manager
 */
struct AssetStats {
    size_t loadedAssets;    ///< Number of currently loaded assets
    size_t totalMemory;     ///< Total memory used by loaded assets
    size_t pendingLoads;    ///< Number of async loads in progress
    size_t failedLoads;     ///< Number of failed loads since startup
};

/**
 * @brief Central asset management system
 *
 * Handles loading, unloading, and lifetime management of game assets.
 * Supports both synchronous and asynchronous loading with reference counting.
 *
 * Features:
 * - Package-based asset storage
 * - Synchronous and asynchronous loading
 * - Automatic reference counting
 * - Memory usage tracking
 *
 * Usage:
 *   AssetManager::Instance().Initialize();
 *   AssetManager::Instance().LoadPackage("game_assets.pkg");
 *   auto texture = AssetManager::Instance().LoadAssetSync<Texture>("textures/player.png");
 */
class AssetManager {
public:
    /**
     * @brief Get the singleton instance
     */
    static AssetManager& Instance();

    /**
     * @brief Initialize the asset manager
     * @return true if successful
     */
    bool Initialize();

    /**
     * @brief Shutdown the asset manager
     *
     * Unloads all packages and assets.
     */
    void Shutdown();

    /**
     * @brief Load an asset package
     * @param packagePath Path to the package file
     * @return true if loaded successfully
     */
    bool LoadPackage(const std::string& packagePath);

    /**
     * @brief Unload an asset package
     * @param packageName Name of the package to unload
     */
    void UnloadPackage(const std::string& packageName);

    /**
     * @brief Synchronously load an asset
     *
     * Blocks until the asset is fully loaded.
     *
     * @param assetName Name/path of the asset within the package
     * @return Handle to the loaded asset
     */
    AssetHandle LoadAssetSync(const std::string& assetName);

    /**
     * @brief Asynchronously load an asset
     *
     * Returns immediately and calls the callback when loading completes.
     *
     * @param assetName Name/path of the asset within the package
     * @param callback Function to call when load completes
     */
    void LoadAssetAsync(const std::string& assetName, AssetLoadCallback callback);

    /**
     * @brief Synchronously load a typed asset
     * @tparam T Asset type
     * @param assetName Name/path of the asset
     * @return Typed handle to the loaded asset
     */
    template<typename T>
    TypedAssetHandle<T> LoadAssetSync(const std::string& assetName) {
        return TypedAssetHandle<T>(LoadAssetSync(assetName));
    }

    /**
     * @brief Unload an asset by handle
     * @param handle Handle to the asset to unload
     */
    void UnloadAsset(const AssetHandle& handle);

    /**
     * @brief Unload an asset by name
     * @param assetName Name of the asset to unload
     */
    void UnloadAsset(const std::string& assetName);

    /**
     * @brief Check if an asset is currently loaded
     * @param assetName Name of the asset to check
     * @return true if the asset is loaded
     */
    bool IsAssetLoaded(const std::string& assetName) const;

    /**
     * @brief Get a handle to a loaded asset
     * @param assetName Name of the asset
     * @return Handle to the asset (invalid if not loaded)
     */
    AssetHandle GetAssetHandle(const std::string& assetName) const;

    /**
     * @brief Increment reference count for an asset
     * @param handle Handle to the asset
     */
    void AddRef(const AssetHandle& handle);

    /**
     * @brief Decrement reference count for an asset
     *
     * Asset is unloaded when reference count reaches zero.
     *
     * @param handle Handle to the asset
     */
    void Release(const AssetHandle& handle);

    /**
     * @brief Get the current reference count for an asset
     * @param handle Handle to the asset
     * @return Current reference count
     */
    uint32_t GetRefCount(const AssetHandle& handle) const;

    /**
     * @brief Get asset manager statistics
     * @return Statistics snapshot
     */
    AssetStats GetStats() const;

    /**
     * @brief Get access to a package container (advanced use)
     * @param packageName Name of the package
     * @return Shared pointer to the package container
     */
    std::shared_ptr<PackageContainer> GetPackage(const std::string& packageName) const;
    uint32_t GetPackageRefCount(const std::string& packageName) const;
    
private:
    AssetManager() = default;
    ~AssetManager() = default;
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;
    
    // Internal loading implementation
    AssetHandle LoadAssetInternal(const std::string& assetName);
    void LoadAssetAsyncInternal(const std::string& assetName, AssetLoadCallback callback);
    
    // Package management
    std::shared_ptr<PackageContainer> LoadPackageInternal(const std::string& packagePath);
    
    // Member variables
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<AssetData>> loadedAssets_;
    struct LoadedPackageEntry {
        std::shared_ptr<PackageContainer> package;
        uint32_t refCount = 0;
    };
    std::unordered_map<std::string, LoadedPackageEntry> loadedPackages_; // key: package name (stem)
    std::unordered_map<uint64_t, std::string> idToName_;
    std::atomic<uint64_t> nextAssetID_{1};
    
    // Statistics
    std::atomic<size_t> loadedAssetsCount_{0};
    std::atomic<size_t> totalMemory_{0};
    std::atomic<size_t> pendingLoads_{0};
    std::atomic<size_t> failedLoads_{0};
};

} // namespace Next
