#pragma once

#include <string>
#include <vector>
#include <map>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

namespace Next {

//=============================================================================
// Shader Compilation Configuration
//=============================================================================

struct ShaderCompileConfig {
    std::string sourceFile;
    std::string targetProfile;     // vs_5_1, ps_5_1, cs_5_1, etc.
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

    // Check if DXR is available
    bool IsDXRAvailable() const { return dxrAvailable_; }

    // Cleanup
    void Shutdown();

private:
    // Try to find and load dxc.dll
    bool LoadDXC();

    // Try to find and load fxc
    bool LoadFXC();

    // Check for DXR support
    bool CheckDXRSupport();

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

    // Function pointers for DXC
    typedef HRESULT (*DxcCompileFunc)(
        LPCWSTR pSource, // Source text to compile
        LPCWSTR pEntryPoint, // Entry point for shader
        LPCWSTR pTargetProfile, // Target shader profile
        LPCWSTR pIncludeEnv, // Include environment
        UINT Flags,             // Compilation flags
        LPCVOID pDefines,       // Shader defines
        UINT DefineCount,       // Number of defines
        LPCWSTR pSourceFilename,   // Optional shader file name
        UINT* pOutput,           // Pointer to ID3DBlob
        ID3DBlob** ppErrorMsgs,   // Optional error messages
        HRESULT* pOutputResult   // HRESULT output
    );

    DxcCompileFunc DxcCompile;

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
