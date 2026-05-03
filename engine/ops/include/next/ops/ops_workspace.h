#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Next {

struct OpsWorkspaceConfig {
    std::string rootPath;
    std::string scenarioId = "maintenance-window";
    bool overwrite = false;
};

struct OpsWorkspaceFile {
    std::string relativePath;
    uint64_t sizeBytes = 0;
};

struct OpsWorkspaceSnapshot {
    std::string name;
    std::string path;
    std::vector<OpsWorkspaceFile> files;
};

class OpsWorkspace {
public:
    bool Initialize(const OpsWorkspaceConfig& config);
    bool Reset();
    bool EnsureSeedFiles();
    bool CreateSnapshot(const std::string& name, OpsWorkspaceSnapshot* outSnapshot = nullptr) const;

    std::vector<OpsWorkspaceFile> ListFiles() const;

    const std::string& RootPath() const { return rootPath_; }
    const std::string& ScenarioId() const { return scenarioId_; }
    const std::string& LastError() const { return lastError_; }
    bool IsInitialized() const { return initialized_; }

private:
    bool WriteTextFile(const std::string& relativePath, const std::string& content, bool overwrite);
    bool CopyFileToSnapshot(const std::string& relativePath, const std::string& snapshotRoot) const;
    std::string AbsolutePath(const std::string& relativePath) const;
    void SetError(const std::string& message) const;

    std::string rootPath_;
    std::string scenarioId_;
    mutable std::string lastError_;
    bool initialized_ = false;
};

std::string DefaultHackOpsWorkspacePath();

} // namespace Next
