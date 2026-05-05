#include "next/renderer/dx12/shader_compiler.h"
#include "next/foundation/logger.h"
#include <Windows.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

namespace Next {

namespace {

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (length <= 0) {
        return std::wstring(text.begin(), text.end());
    }

    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), length);
    if (!wide.empty() && wide.back() == L'\0') {
        wide.pop_back();
    }
    return wide;
}

bool TargetRequiresDXC(const std::string& targetProfile) {
    return targetProfile.find("_6_") != std::string::npos ||
           targetProfile.rfind("as_", 0) == 0 ||
           targetProfile.rfind("ms_", 0) == 0 ||
           targetProfile.rfind("lib_", 0) == 0;
}

void AddUniquePath(std::vector<std::string>& paths, const std::string& path) {
    if (path.empty()) {
        return;
    }
    if (std::find(paths.begin(), paths.end(), path) == paths.end()) {
        paths.push_back(path);
    }
}

void AddWindowsSdkDxcPaths(std::vector<std::string>& paths) {
    const char* programFilesX86 = std::getenv("ProgramFiles(x86)");
    if (!programFilesX86) {
        return;
    }

    const std::filesystem::path binRoot =
        std::filesystem::path(programFilesX86) / "Windows Kits" / "10" / "bin";
    if (!std::filesystem::exists(binRoot)) {
        return;
    }

    std::vector<std::filesystem::path> versionDirs;
    for (const auto& entry : std::filesystem::directory_iterator(binRoot)) {
        if (entry.is_directory()) {
            versionDirs.push_back(entry.path());
        }
    }
    std::sort(versionDirs.begin(), versionDirs.end(), [](const auto& a, const auto& b) {
        return a.filename().string() > b.filename().string();
    });

    for (const auto& versionDir : versionDirs) {
        AddUniquePath(paths, (versionDir / "x64" / "dxcompiler.dll").string());
    }
}

} // namespace

//=============================================================================
// Shader Compiler Implementation
//=============================================================================

ShaderCompiler::ShaderCompiler()
    : dxcPath_()
    , fxcPath_("")
    , dxcAvailable_(false)
    , dxrAvailable_(false)
    , dxcModule_(nullptr)
    , fxcModule_(nullptr)
#if NEXT_RENDERER_HAS_DXC
    , DxcCreateInstance_(nullptr)
#endif
    , pD3DCompile_(nullptr)
    , initialized_(false)
{
}

ShaderCompiler::~ShaderCompiler() {
    Shutdown();
}

bool ShaderCompiler::Initialize(const std::string& dxcPath, const std::string& fxcPath)
{
    dxcPath_ = dxcPath;
    fxcPath_ = fxcPath;

    if (LoadDXC()) {
        NEXT_LOG_INFO("Using DXC for SM6 shader compilation");
    } else {
        NEXT_LOG_WARNING("DXC not available; SM6 shaders will not compile");
    }

    if (LoadFXC()) {
        NEXT_LOG_INFO("Using FXC for shader compilation");
    } else {
        NEXT_LOG_WARNING("FXC not available");
        // Try to use the system D3DCompile directly
        pD3DCompile_ = &D3DCompile;
        NEXT_LOG_INFO("Using system D3DCompile");
    }

    initialized_ = true;
    NEXT_LOG_INFO("Shader compiler initialized");
    return true;
}

bool ShaderCompiler::LoadDXC()
{
#if !NEXT_RENDERER_HAS_DXC
    NEXT_LOG_WARNING("DXC headers are unavailable in this Windows SDK");
    return false;
#else
    // Common paths to search for dxc.dll
    std::vector<std::string> searchPaths;
    AddUniquePath(searchPaths, dxcPath_.empty() ? "dxcompiler.dll" : dxcPath_);
    AddWindowsSdkDxcPaths(searchPaths);
    AddUniquePath(searchPaths, "C:\\Windows\\System32\\dxcompiler.dll");
    AddUniquePath(searchPaths, "C:\\Windows\\SysWOW64\\dxcompiler.dll");
    AddUniquePath(searchPaths, "./third_party/dxc/bin/x64/dxcompiler.dll");
    AddUniquePath(searchPaths, "../bin/dxcompiler.dll");

    for (const auto& path : searchPaths) {
        HMODULE module = LoadLibraryA(path.c_str());
        if (module) {
            dxcModule_ = module;
            dxcPath_ = std::filesystem::exists(path)
                ? std::filesystem::canonical(path).string()
                : path;
            break;
        }
    }

    if (!dxcModule_) {
        NEXT_LOG_WARNING("DXC not found");
        return false;
    }

    DxcCreateInstance_ = reinterpret_cast<DxcCreateInstanceProc>(
        GetProcAddress(static_cast<HMODULE>(dxcModule_), "DxcCreateInstance")
    );
    if (!DxcCreateInstance_) {
        NEXT_LOG_WARNING("DxcCreateInstance not found in: %s", dxcPath_.c_str());
        FreeLibrary(static_cast<HMODULE>(dxcModule_));
        dxcModule_ = nullptr;
        DxcCreateInstance_ = nullptr;
        return false;
    }

    HRESULT hr = DxcCreateInstance_(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils_));
    if (FAILED(hr)) {
        NEXT_LOG_WARNING("Failed to create IDxcUtils: 0x%X", hr);
        FreeLibrary(static_cast<HMODULE>(dxcModule_));
        dxcModule_ = nullptr;
        DxcCreateInstance_ = nullptr;
        return false;
    }

    hr = DxcCreateInstance_(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler_));
    if (FAILED(hr)) {
        NEXT_LOG_WARNING("Failed to create IDxcCompiler3: 0x%X", hr);
        dxcUtils_.Reset();
        FreeLibrary(static_cast<HMODULE>(dxcModule_));
        dxcModule_ = nullptr;
        DxcCreateInstance_ = nullptr;
        return false;
    }

    hr = dxcUtils_->CreateDefaultIncludeHandler(&dxcIncludeHandler_);
    if (FAILED(hr)) {
        NEXT_LOG_WARNING("Failed to create DXC include handler: 0x%X", hr);
        dxcCompiler_.Reset();
        dxcUtils_.Reset();
        FreeLibrary(static_cast<HMODULE>(dxcModule_));
        dxcModule_ = nullptr;
        DxcCreateInstance_ = nullptr;
        return false;
    }

    dxcAvailable_ = true;
    NEXT_LOG_INFO("Loaded DXC from: %s", dxcPath_.c_str());
    return true;
#endif
}

bool ShaderCompiler::LoadFXC()
{
    // Load d3dcompiler_47.dll (FXC)
    // Try standard paths
    std::vector<std::string> searchPaths = {
        fxcPath_.empty() ? "d3dcompiler_47.dll" : fxcPath_,
        "C:\\Windows\\System32\\d3dcompiler_47.dll",
        "C:\\Windows\\SysWOW64\\d3dcompiler_47.dll",
        "./third_party/fxc/bin/x64/d3dcompiler_47.dll",
        "../bin/d3dcompiler_47.dll"
    };

    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path)) {
            fxcPath_ = std::filesystem::canonical(path).string();
            break;
        }
    }

    if (!std::filesystem::exists(fxcPath_)) {
        NEXT_LOG_WARNING("FXC not found at: %s", fxcPath_.c_str());
        return false;
    }

    // Load FXC
    fxcModule_ = LoadLibraryA(fxcPath_.c_str());
    if (!fxcModule_) {
        NEXT_LOG_WARNING("Failed to load: %s", fxcPath_.c_str());
        return false;
    }

    // Get D3DCompile function
    pD3DCompile_ = reinterpret_cast<decltype(&D3DCompile)>(
        GetProcAddress(static_cast<HMODULE>(fxcModule_), "D3DCompile")
    );

    if (!pD3DCompile_) {
        NEXT_LOG_WARNING("D3DCompile function not found in: %s", fxcPath_.c_str());
        FreeLibrary(static_cast<HMODULE>(fxcModule_));
        fxcModule_ = nullptr;
        return false;
    }

    NEXT_LOG_INFO("Loaded FXC from: %s", fxcPath_.c_str());
    return true;
}

bool ShaderCompiler::CompileWithDXC(const ShaderCompileConfig& config,
                                 std::vector<uint8_t>& outBytecode)
{
#if !NEXT_RENDERER_HAS_DXC
    NEXT_LOG_ERROR("DXC support was not compiled into this build");
    return false;
#else
    if (!dxcAvailable_) {
        NEXT_LOG_ERROR("DXC not available");
        return false;
    }

    std::vector<uint8_t> sourceCode;
    {
        std::ifstream file(config.sourceFile, std::ios::binary);
        if (!file) {
            NEXT_LOG_ERROR("Failed to open shader source: %s", config.sourceFile.c_str());
            return false;
        }

        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        sourceCode.resize(fileSize);
        file.read(reinterpret_cast<char*>(sourceCode.data()), fileSize);
    }

    DxcBuffer sourceBuffer = {};
    sourceBuffer.Ptr = sourceCode.data();
    sourceBuffer.Size = sourceCode.size();
    sourceBuffer.Encoding = DXC_CP_UTF8;

    std::vector<std::wstring> ownedArgs;
    std::vector<LPCWSTR> args;
    ownedArgs.reserve(12 + (config.includePaths.size() + config.defines.size()) * 2);
    args.reserve(12 + (config.includePaths.size() + config.defines.size()) * 2);
    auto addArg = [&](const std::wstring& value) {
        ownedArgs.push_back(value);
        args.push_back(ownedArgs.back().c_str());
    };

    addArg(Utf8ToWide(config.sourceFile));
    if (config.targetProfile.rfind("lib_", 0) != 0) {
        addArg(L"-E");
        addArg(Utf8ToWide(config.entryPoint.empty() ? "main" : config.entryPoint));
    }
    addArg(L"-T");
    addArg(Utf8ToWide(config.targetProfile.empty() ? "cs_6_0" : config.targetProfile));

    if (config.debugInfo) {
        addArg(L"-Zi");
        addArg(L"-Qembed_debug");
    }
    if (config.optimisationLevel0) {
        addArg(L"-Od");
    } else if (config.optimisationLevel3) {
        addArg(L"-O3");
    }
    if (config.warningsAsErrors) {
        addArg(L"-WX");
    }

    for (const auto& includePath : config.includePaths) {
        addArg(L"-I");
        addArg(Utf8ToWide(includePath));
    }
    for (const auto& define : config.defines) {
        addArg(L"-D");
        addArg(Utf8ToWide(define));
    }

    Microsoft::WRL::ComPtr<IDxcResult> result;
    HRESULT hr = dxcCompiler_->Compile(
        &sourceBuffer,
        args.data(),
        static_cast<UINT32>(args.size()),
        dxcIncludeHandler_.Get(),
        IID_PPV_ARGS(&result)
    );
    if (FAILED(hr) || !result) {
        NEXT_LOG_ERROR("DXC failed to start compilation for %s: 0x%X", config.sourceFile.c_str(), hr);
        return false;
    }

    Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    if (errors && errors->GetStringLength() > 0) {
        NEXT_LOG_WARNING("DXC output for %s:\n%s", config.sourceFile.c_str(), errors->GetStringPointer());
    }

    HRESULT status = S_OK;
    result->GetStatus(&status);
    if (FAILED(status)) {
        NEXT_LOG_ERROR("DXC compilation failed for %s: 0x%X", config.sourceFile.c_str(), status);
        return false;
    }

    Microsoft::WRL::ComPtr<IDxcBlob> object;
    result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
    if (!object || object->GetBufferSize() == 0) {
        NEXT_LOG_ERROR("DXC produced no bytecode for %s", config.sourceFile.c_str());
        return false;
    }

    const auto* begin = static_cast<const uint8_t*>(object->GetBufferPointer());
    outBytecode.assign(begin, begin + object->GetBufferSize());
    NEXT_LOG_INFO("Compiled shader: %s with DXC (%s, %zu bytes)",
                  config.sourceFile.c_str(),
                  config.targetProfile.c_str(),
                  outBytecode.size());
    return true;
#endif
}

bool ShaderCompiler::CompileWithFXC(const ShaderCompileConfig& config,
                                 std::vector<uint8_t>& outBytecode)
{
    if (!pD3DCompile_) {
        NEXT_LOG_ERROR("FXC not available");
        return false;
    }

    // Read source file
    std::vector<uint8_t> sourceCode;
    {
        std::ifstream file(config.sourceFile, std::ios::binary);
        if (!file) {
            NEXT_LOG_ERROR("Failed to open shader source: %s", config.sourceFile.c_str());
            return false;
        }

        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        sourceCode.resize(fileSize);
        file.read(reinterpret_cast<char*>(sourceCode.data()), fileSize);
    }

    // Prepare shader model
    std::string shaderModel = config.targetProfile;
    if (shaderModel.empty()) {
        shaderModel = "vs_5_1"; // Default
    }

    // Compile with FXC
    ID3DBlob* outputBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = pD3DCompile_(
        sourceCode.data(),
        sourceCode.size(),
        config.sourceFile.c_str(),
        nullptr,
        nullptr,
        config.entryPoint.c_str(),
        shaderModel.c_str(),
        0,
        0,
        &outputBlob,
        &errorBlob
    );

    if (SUCCEEDED(hr) && outputBlob) {
        // Copy bytecode
        outBytecode.assign(
            static_cast<uint8_t*>(outputBlob->GetBufferPointer()),
            static_cast<uint8_t*>(outputBlob->GetBufferPointer()) + outputBlob->GetBufferSize()
        );

        outputBlob->Release();
        NEXT_LOG_INFO("Compiled shader: %s with FXC (%s)", config.sourceFile.c_str(), shaderModel.c_str());

        if (errorBlob) {
            std::string errors = static_cast<char*>(errorBlob->GetBufferPointer());
            NEXT_LOG_ERROR("FXC Errors:\n%s", errors.c_str());
            errorBlob->Release();
        }
        return true;
    } else {
        NEXT_LOG_ERROR("FXC compilation failed for %s: 0x%X", config.sourceFile.c_str(), hr);

        if (errorBlob) {
            std::string errors = static_cast<char*>(errorBlob->GetBufferPointer());
            NEXT_LOG_ERROR("FXC Errors:\n%s", errors.c_str());
            errorBlob->Release();
        }
        return false;
    }
}

bool ShaderCompiler::CompileWithBestAvailable(const ShaderCompileConfig& config,
                                         std::vector<uint8_t>& outBytecode)
{
    const bool requiresDXC = TargetRequiresDXC(config.targetProfile);
    if (requiresDXC && !dxcAvailable_) {
        NEXT_LOG_ERROR("Shader target %s requires DXC, but DXC is not available",
                       config.targetProfile.c_str());
        return false;
    }

    if (dxcAvailable_) {
        if (CompileWithDXC(config, outBytecode)) {
            return true;
        }
        if (requiresDXC) {
            return false;
        }
    }

    // Fall back to FXC
    if (pD3DCompile_) {
        if (CompileWithFXC(config, outBytecode)) {
            return true;
        }
    }

    NEXT_LOG_ERROR("Failed to compile shader: %s (no compiler available)", config.sourceFile.c_str());
    return false;
}

bool ShaderCompiler::CompileShader(const ShaderCompileConfig& config,
                             std::vector<uint8_t>& outBytecode)
{
    return CompileWithBestAvailable(config, outBytecode);
}

bool ShaderCompiler::CompileShaders(const std::vector<ShaderCompileConfig>& configs,
                            std::map<std::string, std::vector<uint8_t>>& outBytecodes)
{
    size_t successCount = 0;
    for (const auto& config : configs) {
        std::vector<uint8_t> bytecode;
        if (CompileShader(config, bytecode)) {
            outBytecodes[config.outputName] = bytecode;
            successCount++;
        }
    }

    NEXT_LOG_INFO("Compiled %zu / %zu shaders", successCount, configs.size());
    return successCount == configs.size();
}

bool ShaderCompiler::CompileVertexShader(const std::string& hlslFile,
                                    const std::string& outputFile,
                                    const std::vector<std::string>& defines)
{
    ShaderCompileConfig config;
    config.sourceFile = hlslFile;
    config.outputName = outputFile;
    config.targetProfile = "vs_5_1";
    config.entryPoint = "main";
    config.defines = defines;

    std::vector<uint8_t> bytecode;
    return CompileShader(config, bytecode);
}

bool ShaderCompiler::CompilePixelShader(const std::string& hlslFile,
                                   const std::string& outputFile,
                                   const std::vector<std::string>& defines)
{
    ShaderCompileConfig config;
    config.sourceFile = hlslFile;
    config.outputName = outputFile;
    config.targetProfile = "ps_5_1";
    config.entryPoint = "main";
    config.defines = defines;

    std::vector<uint8_t> bytecode;
    return CompileShader(config, bytecode);
}

bool ShaderCompiler::ComputeShader(const std::string& hlslFile,
                                 const std::string& outputFile,
                                 const std::vector<std::string>& errors)
{
    ShaderCompileConfig config;
    config.sourceFile = hlslFile;
    config.outputName = outputFile;
    config.targetProfile = "cs_5_1";
    config.entryPoint = "main";

    std::vector<uint8_t> bytecode;
    return CompileShader(config, bytecode);
}

void ShaderCompiler::Shutdown()
{
#if NEXT_RENDERER_HAS_DXC
    dxcIncludeHandler_.Reset();
    dxcCompiler_.Reset();
    dxcUtils_.Reset();
    DxcCreateInstance_ = nullptr;
#endif
    pD3DCompile_ = nullptr;

    if (dxcModule_) {
        FreeLibrary(static_cast<HMODULE>(dxcModule_));
        dxcModule_ = nullptr;
    }

    if (fxcModule_) {
        FreeLibrary(static_cast<HMODULE>(fxcModule_));
        fxcModule_ = nullptr;
    }

    dxcAvailable_ = false;
    dxrAvailable_ = false;
    initialized_ = false;

    NEXT_LOG_INFO("Shader compiler shutdown complete");
}

//=============================================================================
// Shader Build Helper Implementation
//=============================================================================

std::vector<std::string> ShaderBuildHelper::FindHLSLFiles(const std::string& directory)
{
    std::vector<std::string> hlslFiles;

    if (!std::filesystem::exists(directory)) {
        NEXT_LOG_ERROR("Directory not found: %s", directory.c_str());
        return hlslFiles;
    }

    try {
        // Search recursively for .hlsl files
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
            if (entry.path().extension() == ".hlsl") {
                hlslFiles.push_back(entry.path().string());
            }
        }
    } catch (const std::exception& e) {
        NEXT_LOG_ERROR("Error scanning directory: %s", e.what());
    }

    NEXT_LOG_INFO("Found %zu HLSL files in %s", hlslFiles.size(), directory.c_str());
    return hlslFiles;
}

std::string ShaderBuildHelper::GenerateShaderCMakeLists(const std::string& shaderDirectory)
{
    // Generate CMake lists for shader compilation
    std::vector<std::string> hlslFiles = FindHLSLFiles(shaderDirectory);

    std::ostringstream cmakLists;
    cmakLists << "# Auto-generated shader compilation lists\n";
    cmakLists << "# Generated by ShaderBuildHelper\n";

    for (const auto& hlslFile : hlslFiles) {
        std::string shaderName = std::filesystem::path(hlslFile).stem().string();
        std::string outputFile = "shaders/" + shaderName + ".cso";

        cmakLists << "\n# " << shaderName << "\n";
        cmakLists << "set(SHADER_" << shaderName << " \"\")\n";
    }

    return cmakLists.str();
}

std::string ShaderBuildHelper::GenerateShaderBuildCommands(const std::string& shaderDirectory)
{
    // Generate build commands for manual shader compilation (for development)
    std::vector<std::string> hlslFiles = FindHLSLFiles(shaderDirectory);

    std::ostringstream commands;
    commands << "Shader Compilation Commands\n";
    commands << "========================\n\n";

    for (const auto& hlslFile : hlslFiles) {
        std::string shaderName = std::filesystem::path(hlslFile).stem().string();
        std::string outputFile = "shaders/" + shaderName + ".cso";

        commands << "Shader: " << shaderName << "\n";
        commands << "Source:  " << hlslFile << "\n";
        commands << "Output: " << outputFile << "\n";
        commands << "\n";
    }

    return commands.str();
}

bool ShaderBuildHelper::CompileAllShaders(const std::string& shaderDirectory,
                                   const std::string& outputDirectory)
{
    ShaderCompiler compiler;
    if (!compiler.Initialize()) {
        NEXT_LOG_ERROR("Failed to initialize shader compiler");
        return false;
    }

    auto hlslFiles = FindHLSLFiles(shaderDirectory);

    size_t successCount = 0;
    for (const auto& hlslFile : hlslFiles) {
        std::string shaderName = std::filesystem::path(hlslFile).stem().string();

        // Determine shader type from filename or directory structure
        std::string shaderType = GetShaderType(hlslFile);

        std::string outputFile = outputDirectory + "/" + shaderName + ".cso";

        // Compile appropriate shader type
        bool success = false;
        if (shaderType == "vs") {
            success = compiler.CompileVertexShader(hlslFile, outputFile, {});
        } else if (shaderType == "ps") {
            success = compiler.CompilePixelShader(hlslFile, outputFile, {});
        } else if (shaderType == "cs") {
            success = compiler.ComputeShader(hlslFile, outputFile);
        } else {
            // Try to auto-detect
            success = compiler.CompileVertexShader(hlslFile, outputFile, {});
            if (!success) {
                success = compiler.CompilePixelShader(hlslFile, outputFile, {});
            }
        }

        if (!success) {
            NEXT_LOG_ERROR("Failed to compile shader: %s", hlslFile.c_str());
        } else {
            NEXT_LOG_INFO("Compiled: %s -> %s", hlslFile.c_str(), outputFile.c_str());
            successCount++;
        }
    }

    NEXT_LOG_INFO("Compiled %zu / %zu shaders", successCount, hlslFiles.size());

    return successCount == hlslFiles.size();
}

std::string ShaderBuildHelper::GetShaderType(const std::string& shaderFile)
{
    // Check filename for shader type hints
    std::string filename = std::filesystem::path(shaderFile).stem().string();
    std::string directory = std::filesystem::path(shaderFile).parent_path().stem().string();

    // Check directory name first
    if (directory == "vertex_shaders" || directory == "vs") {
        return "vs";
    } else if (directory == "pixel_shaders" || directory == "ps") {
        return "ps";
    } else if (directory == "compute_shaders" || directory == "cs") {
        return "cs";
    }

    // Check filename
    if (filename.find("Vertex") != std::string::npos || filename.find("VS") != std::string::npos) {
        return "vs";
    } else if (filename.find("Pixel") != std::string::npos || filename.find("PS") != std::string::npos) {
        return "ps";
    } else if (filename.find("Compute") != std::string::npos || filename.find("CS") != std::string::npos) {
        return "cs";
    }

    // Default to vs
    return "vs";
}

} // namespace Next
