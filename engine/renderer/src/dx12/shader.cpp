#include "next/renderer/dx12/shader.h"
#include "next/foundation/logger.h"
#include <d3dcompiler.h>
#include <fstream>

#pragma comment(lib, "d3dcompiler.lib")

namespace Next {

DX12Shader::DX12Shader()
    : size_(0), initialized_(false) {
}

DX12Shader::~DX12Shader() {
    Shutdown();
}

bool DX12Shader::InitializeFromFile(DX12Device* device, const char* filepath, const char* entryPoint, const char* target) {
    NEXT_LOG_INFO("Compiling shader: %s (Entry: %s, Target: %s)", filepath, entryPoint, target);

    // Compile shader using D3DCompile
    Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    HRESULT hr = D3DCompileFromFile(
        std::wstring(filepath, filepath + strlen(filepath)).c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint,
        target,
        flags,
        0,
        &shaderBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            NEXT_LOG_ERROR("Shader compilation failed: %s", (const char*)errorBlob->GetBufferPointer());
        } else {
            NEXT_LOG_ERROR("Shader compilation failed: 0x%X (File not found?)", hr);
        }
        return false;
    }

    // Copy bytecode to vector
    size_ = shaderBlob->GetBufferSize();
    data_.resize(size_);
    memcpy(data_.data(), shaderBlob->GetBufferPointer(), size_);

    initialized_ = true;
    NEXT_LOG_INFO("Shader compiled successfully (%zu bytes)", size_);
    return true;
}

bool DX12Shader::InitializeFromBytecode(const void* data, size_t size) {
    if (!data || size == 0) {
        NEXT_LOG_ERROR("Invalid bytecode data");
        return false;
    }

    size_ = size;
    data_.resize(size);
    memcpy(data_.data(), data, size);

    initialized_ = true;
    NEXT_LOG_DEBUG("Shader loaded from bytecode (%zu bytes)", size_);
    return true;
}

void DX12Shader::Shutdown() {
    data_.clear();
    size_ = 0;
    initialized_ = false;
}

} // namespace Next
