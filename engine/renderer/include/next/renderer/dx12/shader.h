#pragma once

#include "next/renderer/dx12/device.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <string>
#include <vector>

namespace Next {

// DX12 Shader Bytecode Wrapper
class DX12Shader {
public:
    DX12Shader();
    ~DX12Shader();

    // Initialization
    bool InitializeFromFile(DX12Device* device, const char* filepath, const char* entryPoint, const char* target);
    bool InitializeFromBytecode(const void* data, size_t size);
    void Shutdown();

    // Shader Access
    D3D12_SHADER_BYTECODE GetBytecode() const {
        return { data_.data(), size_ };
    }

    bool IsInitialized() const { return initialized_; }

private:
    std::vector<uint8_t> data_;
    size_t size_;
    bool initialized_;
};

// Vertex Shader Helper
class DX12VertexShader : public DX12Shader {
public:
    bool LoadFromFile(DX12Device* device, const char* filepath) {
        // Shader Model 5.1 is required for register spaces (e.g. `space1`).
        return InitializeFromFile(device, filepath, "main", "vs_5_1");
    }
};

// Pixel Shader Helper
class DX12PixelShader : public DX12Shader {
public:
    bool LoadFromFile(DX12Device* device, const char* filepath) {
        // Shader Model 5.1 is required for register spaces (e.g. `space1`).
        return InitializeFromFile(device, filepath, "main", "ps_5_1");
    }

    bool CompileFromFile(DX12Device* device, const char* filepath, const char* entryPoint, const char* target) {
        return InitializeFromFile(device, filepath, entryPoint, target);
    }

    bool CompileFromFile(const wchar_t* filepath, const char* entryPoint, const char* target) {
        return false;
    }

    D3D12_SHADER_BYTECODE GetPixelShader() const { return GetBytecode(); }
};

// Geometry Shader Helper
class DX12GeometryShader : public DX12Shader {
public:
    bool LoadFromFile(DX12Device* device, const char* filepath) {
        return InitializeFromFile(device, filepath, "main", "gs_5_0");
    }

    D3D12_SHADER_BYTECODE GetGeometryShader() const { return GetBytecode(); }
};

// Compute Shader Helper
class DX12ComputeShader : public DX12Shader {
public:
    bool LoadFromFile(DX12Device* device, const char* filepath) {
        // Shader Model 5.1 is required for register spaces (e.g. `space1`).
        return InitializeFromFile(device, filepath, "main", "cs_5_1");
    }

    D3D12_SHADER_BYTECODE GetComputeShader() const { return GetBytecode(); }
};

} // namespace Next
