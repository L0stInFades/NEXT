#include "next/runtime/asset/package_container.h"
#include "next/foundation/logger.h"
#include "next/profiler/cpu_scope.h"
#include <fstream>
#include <algorithm>
#include <cstring>

namespace Next {

PackageContainer::~PackageContainer() {
    NEXT_LOG_DEBUG("Package container destroyed: %s", name_.c_str());

    // mappedFile_ owns memory via MappedFile::ownedData (unique_ptr). Do NOT delete mappedFile_->data here,
    // otherwise we'll double-free when ownedData is destroyed.
    mappedFile_.reset();
    dataSection_ = nullptr;
    dataSectionSize_ = 0;
}

std::shared_ptr<PackageContainer> PackageContainer::LoadFromFile(const std::string& filePath) {
    NEXT_CPU_SCOPE("PackageContainer::LoadFromFile");
    
    std::shared_ptr<PackageContainer> container(new PackageContainer());
    if (!container->Initialize(filePath)) {
        NEXT_LOG_ERROR("Failed to initialize package container from: %s", filePath.c_str());
        return nullptr;
    }
    
    return container;
}

bool PackageContainer::Initialize(const std::string& filePath) {
    filePath_ = filePath;
    
    // Extract name from file path
    size_t slashPos = filePath.find_last_of("/\\");
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos != std::string::npos && dotPos > slashPos) {
        name_ = filePath.substr(slashPos + 1, dotPos - slashPos - 1);
    } else {
        name_ = filePath.substr(slashPos + 1);
    }
    
    // For CP3, we'll use simple file I/O instead of memory mapping
    // In a real implementation, we would use memory-mapped files
    
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        NEXT_LOG_ERROR("Failed to open package file: %s", filePath.c_str());
        return false;
    }
    
    // Read package header
    file.read(reinterpret_cast<char*>(&packageHeader_), sizeof(PackageHeader));
    
    if (!packageHeader_.Validate()) {
        NEXT_LOG_ERROR("Invalid package header in: %s", filePath.c_str());
        return false;
    }
    
    NEXT_LOG_INFO("Loading package: %s (version: %u, assets: %u)", 
                 name_.c_str(), packageHeader_.version, packageHeader_.assetCount);
    
    // Read asset index
    file.seekg(packageHeader_.indexOffset);
    assetEntries_.resize(packageHeader_.assetCount);
    file.read(reinterpret_cast<char*>(assetEntries_.data()), 
              sizeof(AssetEntry) * packageHeader_.assetCount);
    
    // Build name to index map
    for (size_t i = 0; i < assetEntries_.size(); ++i) {
        nameToIndex_[assetEntries_[i].name] = i;
    }
    
    // Store data section info
    dataSectionSize_ = 0;
    for (const auto& entry : assetEntries_) {
        dataSectionSize_ = std::max(dataSectionSize_,
                                   static_cast<size_t>(entry.dataOffset + entry.assetSize));
    }

    // Security: Validate dataSectionSize_ to prevent allocation issues
    const size_t MAX_REASONABLE_DATA_SIZE = 4ULL * 1024ULL * 1024ULL * 1024ULL; // 4GB max
    if (dataSectionSize_ == 0) {
        NEXT_LOG_ERROR("Invalid data section size: 0 bytes");
        return false;
    }
    if (dataSectionSize_ > MAX_REASONABLE_DATA_SIZE) {
        NEXT_LOG_ERROR("Data section size too large: %llu bytes (max: %llu bytes)",
                      dataSectionSize_, MAX_REASONABLE_DATA_SIZE);
        return false;
    }

    NEXT_LOG_DEBUG("Package data section size: %llu bytes", dataSectionSize_);

    // For CP3, we'll read the entire data section into memory
    // In a real implementation, this would be memory-mapped
    std::vector<uint8_t> tempData;
    try {
        tempData.resize(dataSectionSize_);
    } catch (const std::bad_alloc& e) {
        NEXT_LOG_ERROR("Failed to allocate memory for package data: %s", e.what());
        return false;
    }

    file.seekg(packageHeader_.dataOffset);
    file.read(reinterpret_cast<char*>(tempData.data()), dataSectionSize_);

    // Validate that the read was successful
    if (!file) {
        NEXT_LOG_ERROR("Failed to read package data section");
        return false;
    }

    // Store in mappedFile_ for simplicity - use vector for automatic cleanup
    mappedFile_ = std::make_unique<MappedFile>();
    mappedFile_->size = dataSectionSize_;
    mappedFile_->ownedData = std::make_unique<uint8_t[]>(dataSectionSize_);
    mappedFile_->data = mappedFile_->ownedData.get();
    memcpy(mappedFile_->data, tempData.data(), dataSectionSize_);
    dataSection_ = reinterpret_cast<const uint8_t*>(mappedFile_->data);
    
    file.close();
    
    NEXT_LOG_INFO("Package loaded successfully: %s with %llu assets", 
                 name_.c_str(), assetEntries_.size());
    
    return true;
}

bool PackageContainer::HasAsset(const std::string& assetName) const {
    return nameToIndex_.find(assetName) != nameToIndex_.end();
}

AssetType PackageContainer::GetAssetType(const std::string& assetName) const {
    auto it = nameToIndex_.find(assetName);
    if (it == nameToIndex_.end()) {
        return AssetType::Unknown;
    }
    
    return assetEntries_[it->second].assetType;
}

const AssetEntry* PackageContainer::GetAssetEntry(const std::string& assetName) const {
    auto it = nameToIndex_.find(assetName);
    if (it == nameToIndex_.end()) {
        return nullptr;
    }
    
    return &assetEntries_[it->second];
}

bool PackageContainer::ReadAssetData(const std::string& assetName, std::vector<uint8_t>& outData) const {
    NEXT_CPU_SCOPE("PackageContainer::ReadAssetData");
    
    auto it = nameToIndex_.find(assetName);
    if (it == nameToIndex_.end()) {
        NEXT_LOG_ERROR("Asset not found in package: %s", assetName.c_str());
        return false;
    }
    
    const AssetEntry& entry = assetEntries_[it->second];
    
    if (entry.dataOffset + entry.assetSize > dataSectionSize_) {
        NEXT_LOG_ERROR("Asset data out of bounds: %s", assetName.c_str());
        return false;
    }
    
    outData.resize(entry.assetSize);
    memcpy(outData.data(), dataSection_ + entry.dataOffset, entry.assetSize);
    
    NEXT_LOG_DEBUG("Read asset data: %s (%u bytes)", assetName.c_str(), entry.assetSize);
    return true;
}

bool PackageContainer::ReadAssetHeader(const std::string& assetName, AssetHeader& outHeader) const {
    std::vector<uint8_t> assetData;
    if (!ReadAssetData(assetName, assetData)) {
        return false;
    }
    
    if (assetData.size() < sizeof(AssetHeader)) {
        NEXT_LOG_ERROR("Asset data too small for header: %s", assetName.c_str());
        return false;
    }
    
    memcpy(&outHeader, assetData.data(), sizeof(AssetHeader));
    return true;
}

std::vector<std::string> PackageContainer::GetAssetNames() const {
    std::vector<std::string> names;
    names.reserve(assetEntries_.size());
    
    for (const auto& entry : assetEntries_) {
        names.push_back(entry.name);
    }
    
    return names;
}

std::vector<std::string> PackageContainer::GetAssetsByType(AssetType type) const {
    std::vector<std::string> names;
    
    for (const auto& entry : assetEntries_) {
        if (entry.assetType == type) {
            names.push_back(entry.name);
        }
    }
    
    return names;
}

bool PackageContainer::Validate() const {
    if (!packageHeader_.Validate()) {
        return false;
    }
    
    if (assetEntries_.size() != packageHeader_.assetCount) {
        return false;
    }
    
    // Check for duplicate names
    std::unordered_map<std::string, bool> seenNames;
    for (const auto& entry : assetEntries_) {
        if (seenNames.find(entry.name) != seenNames.end()) {
            NEXT_LOG_ERROR("Duplicate asset name in package: %s", entry.name);
            return false;
        }
        seenNames[entry.name] = true;
    }
    
    return true;
}

} // namespace Next
