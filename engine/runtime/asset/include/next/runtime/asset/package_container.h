#pragma once

#include "next/runtime/asset/asset_types.h"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace Next {

class PackageContainer {
public:
    ~PackageContainer();
    
    // Factory method to load package from file
    static std::shared_ptr<PackageContainer> LoadFromFile(const std::string& filePath);
    
    // Package information
    const std::string& GetName() const { return name_; }
    const std::string& GetFilePath() const { return filePath_; }
    uint32_t GetAssetCount() const { return static_cast<uint32_t>(assetEntries_.size()); }
    
    // Asset query
    bool HasAsset(const std::string& assetName) const;
    AssetType GetAssetType(const std::string& assetName) const;
    const AssetEntry* GetAssetEntry(const std::string& assetName) const;
    
    // Asset data access
    bool ReadAssetData(const std::string& assetName, std::vector<uint8_t>& outData) const;
    bool ReadAssetHeader(const std::string& assetName, AssetHeader& outHeader) const;
    
    // Enumerate assets
    std::vector<std::string> GetAssetNames() const;
    std::vector<std::string> GetAssetsByType(AssetType type) const;
    
    // Validation
    bool Validate() const;
    
private:
    PackageContainer() = default;
    
    bool Initialize(const std::string& filePath);
    bool ParsePackageHeader();
    bool ParseAssetIndex();
    
    // Memory-mapped file support
    struct MappedFile {
        void* data = nullptr;
        size_t size = 0;
        std::unique_ptr<uint8_t[]> ownedData; // Owns the data when allocated from memory

        ~MappedFile() {
            // ownedData is automatically cleaned up by unique_ptr
            data = nullptr;
            size = 0;
        }
    };
    
    std::string filePath_;
    std::string name_;
    std::unique_ptr<MappedFile> mappedFile_;
    PackageHeader packageHeader_;
    
    std::vector<AssetEntry> assetEntries_;
    std::unordered_map<std::string, size_t> nameToIndex_;
    
    // Data section pointers
    const uint8_t* dataSection_ = nullptr;
    size_t dataSectionSize_ = 0;
};

} // namespace Next
