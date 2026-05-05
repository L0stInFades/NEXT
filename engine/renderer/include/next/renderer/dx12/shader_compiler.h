#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#if defined(_WIN32) && __has_include(<dxcapi.h>)
#include <dxcapi.h>
#define NEXT_RENDERER_HAS_DXC 1
#else
#define NEXT_RENDERER_HAS_DXC 0
#endif

namespace Next {

//=============================================================================
// Shader Compilation Configuration
//=============================================================================

struct ShaderCompileConfig {
    std::string sourceFile;
    std::string targetProfile;     // vs_5_1, ps_5_1, cs_5_1, ms_6_5, etc.
    std::string entryPoint;
    std::string outputName;         // Optional: name for the compiled bytecode

    bool debugInfo = false;
    bool optimisationLevel0 = true;
    bool optimisationLevel3 = false;
    bool warningsAsErrors = false;

    // Shader defines
    std::vector<std::string> defines;

    // Include paths
    std::vector<std::string> includePaths;

    ShaderCompileConfig()
        : sourceFile("")
        , targetProfile("sm_5_1")
        , entryPoint("main")
        , outputName("")
        , debugInfo(false)
        , optimisationLevel0(true)
        , optimisationLevel3(false)
        , warningsAsErrors(false)
    {}
};

//=============================================================================
// Shader Compiler
// Supports both D3DCompiler (fxc) and DirectX Shader Compiler (dxc)
//=============================================================================

class ShaderCompiler {
public:
    ShaderCompiler();
    ~ShaderCompiler();

    // Initialize compiler
    bool Initialize(const std::string& dxcPath = "", const std::string& fxcPath = "");

    // Compile a single shader
    bool CompileShader(const ShaderCompileConfig& config,
                     std::vector<uint8_t>& outBytecode);

    // Batch compile multiple shaders
    bool CompileShaders(const std::vector<ShaderCompileConfig>& configs,
                       std::map<std::string, std::vector<uint8_t>>& outBytecodes);

    // Convenience methods for common shader types
    bool CompileVertexShader(const std::string& hlslFile,
                            const std::string& outputFile,
                            const std::vector<std::string>& defines = {});

    bool CompilePixelShader(const std::string& hlslFile,
                           const std::string& outputFile,
                           const std::vector<std::string>& defines = {});

    bool ComputeShader(const std::string& hlslFile,
                     const std::string& outputFile,
                     const std::vector<std::string>& errors = {});

    // Get compilation errors
    const std::vector<std::string>& GetErrors() const { return errors_; }

    // Check if DXC is available (for ray tracing shaders)
    bool IsDXCAvailable() const { return dxcAvailable_; }

    // Runtime DXR support is a device feature, not a shader compiler feature.
    bool IsDXRAvailable() const { return dxrAvailable_; }
    void SetDeviceDXRSupport(bool available) { dxrAvailable_ = available; }

    // Cleanup
    void Shutdown();

private:
    // Try to find and load dxc.dll
    bool LoadDXC();

    // Try to find and load fxc
    bool LoadFXC();

    // Compile using dxc (DirectX Shader Compiler)
    bool CompileWithDXC(const ShaderCompileConfig& config,
                     std::vector<uint8_t>& outBytecode);

    // Compile using fxc (D3DCompiler)
    bool CompileWithFXC(const ShaderCompileConfig& config,
                     std::vector<uint8_t>& outBytecode);

    // Try to compile using dxc first, fall back to fxc
    bool CompileWithBestAvailable(const ShaderCompileConfig& config,
                                 std::vector<uint8_t>& outBytecode);

    // Compiler paths
    std::string dxcPath_;
    std::string fxcPath_;

    // Availability flags
    bool dxcAvailable_;
    bool dxrAvailable_;

    // Error tracking
    std::vector<std::string> errors_;

    // HMODULE handles for loaded DLLs
    void* dxcModule_;
    void* fxcModule_;

#if NEXT_RENDERER_HAS_DXC
    typedef HRESULT (WINAPI *DxcCreateInstanceProc)(REFCLSID rclsid, REFIID riid, LPVOID* ppv);
    DxcCreateInstanceProc DxcCreateInstance_;
    Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils_;
    Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler_;
    Microsoft::WRL::ComPtr<IDxcIncludeHandler> dxcIncludeHandler_;
#endif

    // D3DCompile function pointer (from d3dcompiler.h)
    decltype(&D3DCompile) pD3DCompile_;

    bool initialized_;
};

//=============================================================================
// Shader Build Helper
// Provides utilities for building shaders as part of the build process
//=============================================================================

class ShaderBuildHelper {
public:
    // Find all .hlsl files in a directory
    static std::vector<std::string> FindHLSLFiles(const std::string& directory);

    // Generate CMake lists for shader compilation
    static std::string GenerateShaderCMakeLists(const std::string& shaderDirectory);

    // Generate custom build commands for shaders
    static std::string GenerateShaderBuildCommands(const std::string& shaderDirectory);

    // Compile all shaders in a directory
    static bool CompileAllShaders(const std::string& shaderDirectory,
                               const std::string& outputDirectory);

private:
    // Determine shader type from filename
    static std::string GetShaderType(const std::string& shaderFile);
};

} // namespace Next
