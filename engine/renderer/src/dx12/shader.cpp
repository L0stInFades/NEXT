#include "next/renderer/dx12/shader.h"
#include "next/renderer/dx12/shader_compiler.h"
#include "next/foundation/logger.h"
#include <Windows.h>
#include <d3dcompiler.h>
#include <fstream>
#include <utility>

#pragma comment(lib, "d3dcompiler.lib")

namespace Next {

namespace {

bool TargetRequiresDXC(const char* target) {
    if (!target) {
        return false;
    }

    const std::string profile(target);
    return profile.find("_6_") != std::string::npos ||
           profile.rfind("as_", 0) == 0 ||
           profile.rfind("ms_", 0) == 0 ||
           profile.rfind("lib_", 0) == 0;
}

std::wstring Utf8ToWidePath(const char* text) {
    if (!text || text[0] == '\0') {
        return {};
    }

    const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (length <= 0) {
        return std::wstring(text, text + strlen(text));
    }

    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), length);
    if (!wide.empty() && wide.back() == L'\0') {
        wide.pop_back();
    }
    return wide;
}

} // namespace

DX12Shader::DX12Shader()
    : size_(0), initialized_(false) {
}

DX12Shader::~DX12Shader() {
    Shutdown();
}

bool DX12Shader::InitializeFromFile(DX12Device* device, const char* filepath, const char* entryPoint, const char* target) {
    Shutdown();

    if (!device || !device->GetDevice() || !filepath || filepath[0] == '\0' ||
        !entryPoint || entryPoint[0] == '\0' || !target || target[0] == '\0') {
        NEXT_LOG_ERROR("Invalid shader compile parameters");
        return false;
    }

    NEXT_LOG_INFO("Compiling shader: %s (Entry: %s, Target: %s)", filepath, entryPoint, target);

    if (TargetRequiresDXC(target)) {
        ShaderCompiler compiler;
        if (!compiler.Initialize()) {
            NEXT_LOG_ERROR("Failed to initialize DXC-capable shader compiler");
            return false;
        }

        ShaderCompileConfig config;
        config.sourceFile = filepath;
        config.entryPoint = entryPoint ? entryPoint : "main";
        config.targetProfile = target ? target : "cs_6_0";
        config.optimisationLevel0 = false;
        config.optimisationLevel3 = true;

        std::vector<uint8_t> bytecode;
        if (!compiler.CompileShader(config, bytecode)) {
            NEXT_LOG_ERROR("Shader compilation failed for SM6/DX12U target: %s", filepath);
            return false;
        }

        data_ = std::move(bytecode);
        size_ = data_.size();
        initialized_ = true;
        NEXT_LOG_INFO("Shader compiled successfully with DXC (%zu bytes)", size_);
        return true;
    }

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
        Utf8ToWidePath(filepath).c_str(),
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
