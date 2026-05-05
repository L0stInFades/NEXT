#pragma once

// DX12U: DirectX 12 Ultimate (Feature Level 12.2+)
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3d12shader.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace Next {

// DX12U Feature Support Flags
struct DX12Features {
    bool meshShaders = false;
    bool raytracing = false;
    bool variableShading = false;
    bool samplerFeedback = false;
    bool workGraphs = false;
    uint32_t featureLevel = 0;
};

// DX12U Device Wrapper
class DX12Device {
public:
    DX12Device();
    ~DX12Device();

    // Initialization
    bool Initialize();
    void Shutdown();

    // Device Access
    ID3D12Device5* GetDevice() const { return device_.Get(); }
    IDXGIFactory4* GetFactory() const { return factory_.Get(); }
    IDXGIAdapter1* GetAdapter() const { return adapter_.Get(); }

    // Feature Queries
    const DX12Features& GetFeatures() const { return features_; }
    bool SupportsMeshShaders() const { return features_.meshShaders; }
    bool SupportsRayTracing() const { return features_.raytracing; }
    bool SupportsVRS() const { return features_.variableShading; }
    bool SupportsSamplerFeedback() const { return features_.samplerFeedback; }
    bool IsDX12U() const { return features_.featureLevel >= static_cast<uint32_t>(D3D_FEATURE_LEVEL_12_2); }

    // Adapter Info
    std::string GetAdapterDescription() const;
    size_t GetVRAM() const;

private:
    bool CreateDXGIFactory();
    bool CreateDevice();
    bool QueryFeatures();

    Microsoft::WRL::ComPtr<ID3D12Device5> device_;
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory_;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter_;

    DX12Features features_;
    bool initialized_;
};

} // namespace Next
