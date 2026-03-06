#include "next/renderer/dx12/device.h"
#include "next/foundation/logger.h"
#include <windows.h>
#include <array>

namespace Next {

DX12Device::DX12Device() : initialized_(false) {
    memset(&features_, 0, sizeof(features_));
}

DX12Device::~DX12Device() {
    Shutdown();
}

bool DX12Device::Initialize() {
    NEXT_LOG_INFO("Initializing DX12U Device...");

    if (initialized_) {
        NEXT_LOG_WARNING("DX12Device already initialized");
        return true;
    }

    // Create DXGI Factory
    if (!CreateDXGIFactory()) {
        NEXT_LOG_ERROR("Failed to create DXGI Factory");
        return false;
    }

    // Create D3D12 Device
    if (!CreateDevice()) {
        NEXT_LOG_ERROR("Failed to create DX12 Device");
        return false;
    }

    if (!QueryFeatures()) {
        NEXT_LOG_WARNING("Failed to query DX12 feature support");
    }

    initialized_ = true;
    NEXT_LOG_INFO("DX12 Device initialized successfully");
    return true;
}

void DX12Device::Shutdown() {
    if (!initialized_) {
        return;
    }

    NEXT_LOG_INFO("Shutting down DX12 Device...");

    device_.Reset();
    factory_.Reset();
    adapter_.Reset();

    initialized_ = false;
    NEXT_LOG_INFO("DX12 Device shutdown complete");
}

bool DX12Device::CreateDXGIFactory() {
    UINT dxgiFlags = 0;
#ifdef _DEBUG
    dxgiFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

    HRESULT hr = CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&factory_));
    if (FAILED(hr)) {
        NEXT_LOG_ERROR("CreateDXGIFactory2 failed: 0x%X", hr);
        return false;
    }

    NEXT_LOG_INFO("DXGI Factory created successfully");
    return true;
}

bool DX12Device::CreateDevice() {
    // Enable Debug Layer for detailed error messages
#ifdef _DEBUG
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        NEXT_LOG_INFO("DX12 Debug Layer enabled");

        // Break on shader errors
        Microsoft::WRL::ComPtr<ID3D12Debug1> debugController1;
        if (SUCCEEDED(debugController->QueryInterface(IID_PPV_ARGS(&debugController1)))) {
            debugController1->SetEnableSynchronizedCommandQueueValidation(TRUE);
        }
    } else {
        NEXT_LOG_WARNING("Failed to enable DX12 Debug Layer");
    }
#endif

    // Enumerate adapters and find the best one
    Microsoft::WRL::ComPtr<IDXGIAdapter1> hardwareAdapter;
    for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != factory_->EnumAdapters1(adapterIndex, &hardwareAdapter); ++adapterIndex) {
        DXGI_ADAPTER_DESC1 desc;
        hardwareAdapter->GetDesc1(&desc);

        // Skip software adapters
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            NEXT_LOG_DEBUG("  Skipping software adapter: %S", desc.Description);
            continue;
        }

        NEXT_LOG_INFO("  Found hardware adapter: %S (VRAM: %llu MB)",
                     desc.Description, desc.DedicatedVideoMemory / (1024 * 1024));

        // Try to create D3D12 device
        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_12_2,  // DX12U
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };

        HRESULT hr = D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_12_2,  // Request DX12U
            IID_PPV_ARGS(&device_)
        );

        if (SUCCEEDED(hr)) {
            adapter_ = hardwareAdapter;
            NEXT_LOG_INFO("  DX12 Device created successfully (Feature Level 12.2)");
            return true;
        }

        NEXT_LOG_WARNING("  Failed to create DX12 device with FL 12.2, trying lower levels...");

        // Try lower feature levels
        for (D3D_FEATURE_LEVEL level : featureLevels) {
            hr = D3D12CreateDevice(
                hardwareAdapter.Get(),
                level,
                IID_PPV_ARGS(&device_)
            );

            if (SUCCEEDED(hr)) {
                adapter_ = hardwareAdapter;
                NEXT_LOG_INFO("  DX12 Device created with Feature Level 0x%X", level);
                return true;
            }
        }
    }

    NEXT_LOG_ERROR("No suitable DX12 adapter found");
    return false;
}

std::string DX12Device::GetAdapterDescription() const {
    if (!adapter_) {
        return "Unknown";
    }

    DXGI_ADAPTER_DESC1 desc;
    adapter_->GetDesc1(&desc);

    // Convert WCHAR to std::string
    int length = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nullptr, 0, nullptr, nullptr);
    std::string result(length, 0);
    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, &result[0], length, nullptr, nullptr);

    return result;
}

size_t DX12Device::GetVRAM() const {
    if (!adapter_) {
        return 0;
    }

    DXGI_ADAPTER_DESC1 desc;
    adapter_->GetDesc1(&desc);

    return desc.DedicatedVideoMemory;
}

bool DX12Device::QueryFeatures() {
    if (!device_) {
        return false;
    }

    features_ = {};

    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
    if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7)))) {
        features_.meshShaders = options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6 = {};
    if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &options6, sizeof(options6)))) {
        features_.variableShading = options6.VariableShadingRateTier != D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))) {
        features_.raytracing = options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    }

    features_.workGraphs = false;

    D3D12_FEATURE_DATA_FEATURE_LEVELS levels = {};
    const std::array<D3D_FEATURE_LEVEL, 5> requested = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };
    levels.NumFeatureLevels = static_cast<UINT>(requested.size());
    levels.pFeatureLevelsRequested = requested.data();
    if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof(levels)))) {
        features_.featureLevel = static_cast<uint32_t>(levels.MaxSupportedFeatureLevel);
    }

    NEXT_LOG_INFO("DX12 Features - FL 0x%X, Mesh: %s, RT: %s, VRS: %s, WorkGraphs: %s",
                  features_.featureLevel,
                  features_.meshShaders ? "Yes" : "No",
                  features_.raytracing ? "Yes" : "No",
                  features_.variableShading ? "Yes" : "No",
                  features_.workGraphs ? "Yes" : "No");

    return true;
}

} // namespace Next
