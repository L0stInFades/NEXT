#include "next/ops/ops_workspace.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

struct Options {
    std::string workspace;
    std::string snapshotName;
    bool reset = false;
    bool list = false;
    bool help = false;
};

void PrintUsage() {
    std::cout
        << "Usage: hackops_demo [--workspace path] [--reset] [--snapshot name] [--list]\n"
        << "\n"
        << "Creates the HackOps maintenance-window workspace without starting the renderer.\n";
}

bool ReadValue(int& index, int argc, char** argv, std::string& out) {
    if (index + 1 >= argc) {
        return false;
    }
    out = argv[++index];
    return true;
}

bool ParseArgs(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--workspace") {
            if (!ReadValue(i, argc, argv, options.workspace)) {
                return false;
            }
        } else if (arg == "--snapshot") {
            if (!ReadValue(i, argc, argv, options.snapshotName)) {
                return false;
            }
        } else if (arg == "--reset") {
            options.reset = true;
        } else if (arg == "--list") {
            options.list = true;
        } else if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else {
            return false;
        }
    }

    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!ParseArgs(argc, argv, options)) {
        PrintUsage();
        return 2;
    }

    if (options.help) {
        PrintUsage();
        return 0;
    }

    Next::OpsWorkspaceConfig config;
    config.rootPath = options.workspace;
    config.overwrite = options.reset;

    Next::OpsWorkspace workspace;
    if (!workspace.Initialize(config)) {
        std::cerr << "hackops_demo: " << workspace.LastError() << "\n";
        return 1;
    }

    std::cout << "workspace=" << workspace.RootPath() << "\n";
    std::cout << "scenario=" << workspace.ScenarioId() << "\n";

    if (!options.snapshotName.empty()) {
        Next::OpsWorkspaceSnapshot snapshot;
        if (!workspace.CreateSnapshot(options.snapshotName, &snapshot)) {
            std::cerr << "hackops_demo: " << workspace.LastError() << "\n";
            return 1;
        }
        std::cout << "snapshot=" << snapshot.path << "\n";
    }

    if (options.list) {
        for (const auto& file : workspace.ListFiles()) {
            if (file.relativePath.rfind("snapshots/", 0) == 0) {
                continue;
            }
            std::cout << "file=" << file.relativePath << " size=" << file.sizeBytes << "\n";
        }
    }

    return 0;
}
