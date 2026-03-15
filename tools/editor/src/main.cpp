#include "next/platform/platform.h"
#include "next/platform/window.h"
#include "next/foundation/logger.h"
#include "next/renderer/renderer.h"
#include "next/renderer/dx12/dx12_renderer.h"
#include "next/jobsystem/job_system.h"
#include "next/runtime/asset/asset_manager.h"
#include "next/runtime/asset/asset_types.h"
#include "next/runtime/asset/package_container.h"

#include <windows.h>
#include <shellapi.h> // DragAcceptFiles / WM_DROPFILES
#include <shlobj.h>   // SHGetFolderPathA
// Prevent Win32 macros from colliding with engine symbols and asset enums.
#ifdef CreateWindow
#undef CreateWindow
#endif
#ifdef OPAQUE
#undef OPAQUE
#endif
#ifdef TRANSPARENT
#undef TRANSPARENT
#endif

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>

#include "imgui.h"
#include "imgui_internal.h" // DockBuilder
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

// Forward declare (imgui_impl_win32.h intentionally avoids including <windows.h> for this).
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

static void __stdcall D3D12MessageCallback(D3D12_MESSAGE_CATEGORY category,
                                          D3D12_MESSAGE_SEVERITY severity,
                                          D3D12_MESSAGE_ID id,
                                          LPCSTR description,
                                          void* context) {
    (void)category;
    (void)id;
    (void)context;

    // Keep this lightweight and stderr-only (callback can be invoked from inside D3D12 calls).
    if (severity < D3D12_MESSAGE_SEVERITY_WARNING || !description) {
        return;
    }
    std::fprintf(stderr, "[D3D12] %s\n", description);
}

// Descriptor allocator for ImGui dynamic textures (font atlas, etc).
// Minimal bump allocator: good enough for editor bring-up.
struct ImGuiSrvAllocator {
    ID3D12DescriptorHeap* heap = nullptr;
    UINT increment = 0;
    std::atomic<uint32_t> next{0};
    // Optional freelist to allow reusing descriptors when ImGui destroys textures (e.g. font rebuild).
    CRITICAL_SECTION lock;
    bool lockInit = false;
    std::vector<uint32_t> freeList;
};

static void ImGuiSrvAlloc(ImGui_ImplDX12_InitInfo* info,
                          D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
                          D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle) {
    auto* alloc = static_cast<ImGuiSrvAllocator*>(info->UserData);
    uint32_t idx = UINT32_MAX;
    if (alloc->lockInit) {
        EnterCriticalSection(&alloc->lock);
        if (!alloc->freeList.empty()) {
            idx = alloc->freeList.back();
            alloc->freeList.pop_back();
        }
        LeaveCriticalSection(&alloc->lock);
    }
    if (idx == UINT32_MAX) {
        idx = alloc->next.fetch_add(1, std::memory_order_relaxed);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = alloc->heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = alloc->heap->GetGPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(idx) * alloc->increment;
    gpu.ptr += static_cast<UINT64>(idx) * alloc->increment;
    *out_cpu_handle = cpu;
    *out_gpu_handle = gpu;
}

static void ImGuiSrvFree(ImGui_ImplDX12_InitInfo* info,
                         D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
                         D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) {
    (void)gpu_handle;
    auto* alloc = static_cast<ImGuiSrvAllocator*>(info->UserData);
    if (!alloc || !alloc->heap || alloc->increment == 0 || !alloc->lockInit) {
        return;
    }

    const SIZE_T base = alloc->heap->GetCPUDescriptorHandleForHeapStart().ptr;
    if (cpu_handle.ptr < base) {
        return;
    }
    const SIZE_T delta = cpu_handle.ptr - base;
    if (delta % alloc->increment != 0) {
        return;
    }
    const uint32_t idx = static_cast<uint32_t>(delta / alloc->increment);

    EnterCriticalSection(&alloc->lock);
    alloc->freeList.push_back(idx);
    LeaveCriticalSection(&alloc->lock);
}

std::filesystem::path GetExeDir() {
    char path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    std::filesystem::path p(path);
    return p.parent_path();
}

void PushUniquePath(std::vector<std::filesystem::path>& roots, const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }

    for (const auto& existing : roots) {
        if (existing == path) {
            return;
        }
    }

    roots.push_back(path);
}

std::filesystem::path ResolveRuntimePath(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) {
        return path;
    }

    std::error_code ec;
    std::vector<std::filesystem::path> roots;
    PushUniquePath(roots, std::filesystem::current_path(ec));

    std::filesystem::path probe = GetExeDir();
    for (int i = 0; i < 6 && !probe.empty(); ++i) {
        PushUniquePath(roots, probe);
        const std::filesystem::path parent = probe.parent_path();
        if (parent == probe) {
            break;
        }
        probe = parent;
    }

    std::filesystem::path fallback;
    for (const auto& root : roots) {
        const std::filesystem::path candidate = root / path;
        if (fallback.empty()) {
            const std::filesystem::path absoluteCandidate = std::filesystem::absolute(candidate, ec);
            fallback = ec ? candidate : absoluteCandidate;
        }

        if (std::filesystem::exists(candidate, ec)) {
            const std::filesystem::path absoluteCandidate = std::filesystem::absolute(candidate, ec);
            return ec ? candidate : absoluteCandidate;
        }
    }

    return fallback.empty() ? path : fallback;
}

std::string GetLocalAppDataDir() {
    char path[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path))) {
        return std::string(path);
    }
    return {};
}

std::vector<std::filesystem::path> ScanPackages(const std::filesystem::path& assetsDir) {
    std::vector<std::filesystem::path> out;
    if (!std::filesystem::exists(assetsDir)) {
        return out;
    }
    for (const auto& entry : std::filesystem::directory_iterator(assetsDir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() == ".npkg") {
            out.push_back(entry.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string FormatByteSize(uint64_t bytes) {
    static constexpr const char* kUnits[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < 4) {
        value /= 1024.0;
        ++unit;
    }

    char buffer[64] = {};
    if (unit == 0) {
        std::snprintf(buffer, sizeof(buffer), "%llu %s",
                      static_cast<unsigned long long>(bytes), kUnits[unit]);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f %s", value, kUnits[unit]);
    }
    return buffer;
}

template <typename T>
bool ReadTypedAssetHeader(const std::shared_ptr<Next::PackageContainer>& package,
                          const std::string& assetName,
                          T& outHeader) {
    if (!package) {
        return false;
    }

    std::vector<uint8_t> assetData;
    if (!package->ReadAssetData(assetName, assetData) || assetData.size() < sizeof(T)) {
        return false;
    }

    std::memcpy(&outHeader, assetData.data(), sizeof(T));
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    double smokeSeconds = 0.0;
    int smokeFrames = 0;
    bool disableImGui = false;
    int autoResizeFrame = -1;
    int autoResizeW = 0;
    int autoResizeH = 0;
    std::vector<std::string> packagesToLoad;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] ? argv[i] : "";
        if (arg == "--no-imgui") {
            disableImGui = true;
        } else if (arg == "--auto-resize" && i + 2 < argc) {
            autoResizeW = std::atoi(argv[++i]);
            autoResizeH = std::atoi(argv[++i]);
            if (autoResizeFrame < 0) {
                autoResizeFrame = 5;
            }
        } else if (arg == "--auto-resize-frame" && i + 1 < argc) {
            autoResizeFrame = std::atoi(argv[++i]);
        } else if (arg == "--load-package" && i + 1 < argc) {
            packagesToLoad.emplace_back(argv[++i]);
        } else if (arg == "--smoke-seconds" && i + 1 < argc) {
            smokeSeconds = std::atof(argv[++i]);
        } else if (arg == "--smoke-frames" && i + 1 < argc) {
            smokeFrames = std::atoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::fprintf(stdout,
                         "NEXT Editor\n"
                         "  --no-imgui            Disable ImGui overlay (renderer-only)\n"
                         "  --auto-resize <w> <h> Resize the window once (use with --smoke-frames)\n"
                         "  --auto-resize-frame n Perform --auto-resize at frame n (default: 5)\n"
                         "  --load-package <p>    Load package path (can repeat)\n"
                         "  --smoke-seconds <n>   Run for n seconds then exit\n"
                         "  --smoke-frames <n>    Run for n frames then exit\n");
            return 0;
        }
    }

    // Let ImGui Win32 backend configure DPI-awareness (prevents Windows bitmap-scaling blur).
    // Must be called before creating any windows.
    ImGui_ImplWin32_EnableDpiAwareness();

    if (!Next::PlatformInitialize()) {
        return 1;
    }

    Next::Logger::Initialize();

    Next::Window* window = Next::CreateWindow();
    Next::WindowDesc windowDesc;
    windowDesc.title = "NEXT Editor";
    windowDesc.width = 1600;
    windowDesc.height = 900;

    if (!window || !window->Initialize(windowDesc)) {
        NEXT_LOG_FATAL("Failed to create editor window");
        delete window;
        Next::Logger::Shutdown();
        Next::PlatformShutdown();
        return 1;
    }

    Next::Renderer* renderer = Next::Renderer::Create();
    if (!renderer || !renderer->Initialize(window)) {
        NEXT_LOG_FATAL("Failed to initialize renderer");
        if (renderer) {
            delete renderer;
        }
        window->Shutdown();
        delete window;
        Next::Logger::Shutdown();
        Next::PlatformShutdown();
        return 1;
    }

    auto& jobSystem = Next::JobSystem::Instance();
    jobSystem.Initialize(0);

    auto& assetManager = Next::AssetManager::Instance();
    if (!assetManager.Initialize()) {
        NEXT_LOG_WARNING("AssetManager failed to initialize (asset browser/import will be limited)");
    }

    for (const std::string& p : packagesToLoad) {
        assetManager.LoadPackage(ResolveRuntimePath(std::filesystem::path(p)).string());
    }

    auto* dx12 = static_cast<Next::DX12Renderer*>(renderer);
    ID3D12Device* device = dx12->GetD3DDevice();
    if (!device) {
        NEXT_LOG_FATAL("DX12 device is null");
        renderer->Shutdown();
        delete renderer;
        window->Shutdown();
        delete window;
        Next::Logger::Shutdown();
        Next::PlatformShutdown();
        return 1;
    }

    // Renderer defaults to PBR; editor can later expose render mode in UI.

    HWND hwnd = static_cast<HWND>(window->GetNativeHandle());
    if (hwnd) {
        // Enable simple file drag-and-drop UX (OBJ to asset pipeline is wired via next_assetc).
        DragAcceptFiles(hwnd, TRUE);
    }

    // Capture D3D12 debug-layer messages to stderr to help diagnose GPU/device removal issues.
    {
        Microsoft::WRL::ComPtr<ID3D12InfoQueue1> infoQueue;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue))) && infoQueue) {
            DWORD cookie = 0;
            infoQueue->RegisterMessageCallback(D3D12MessageCallback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &cookie);
        }
    }

    const bool useImGui = !disableImGui;

    // Create a shader-visible SRV heap dedicated to ImGui.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> imguiSrvHeap;
    ImGuiSrvAllocator imguiSrvAlloc = {};
    if (useImGui) {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 1024;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&imguiSrvHeap));
        if (FAILED(hr)) {
            NEXT_LOG_FATAL("Failed to create ImGui SRV heap: 0x%X", hr);
            renderer->Shutdown();
            delete renderer;
            window->Shutdown();
            delete window;
            Next::Logger::Shutdown();
            Next::PlatformShutdown();
            return 1;
        }
    }

    bool imguiInitialized = false;
    ImGuiIO* ioPtr = nullptr;
    if (useImGui) {
        imguiSrvAlloc.heap = imguiSrvHeap.Get();
        imguiSrvAlloc.increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        InitializeCriticalSection(&imguiSrvAlloc.lock);
        imguiSrvAlloc.lockInit = true;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        ioPtr = &io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
         io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
 
         ImGui::StyleColorsDark();

        // Keep ImGui ini out of the repo root. Persist per-user under LocalAppData.
        // This avoids "stuck" layouts when the working directory changes or an ini gets corrupted.
        static std::string s_imguiIniPath;
        if (s_imguiIniPath.empty()) {
            const std::string localAppData = GetLocalAppDataDir();
            if (!localAppData.empty()) {
                std::filesystem::path iniDir = std::filesystem::path(localAppData) / "NEXT";
                std::error_code ec;
                std::filesystem::create_directories(iniDir, ec);
                s_imguiIniPath = (iniDir / "next_editor_imgui.ini").string();
            }
        }
        if (!s_imguiIniPath.empty()) {
            io.IniFilename = s_imguiIniPath.c_str();
        }

         // DPI-aware font setup (crisp text at 125%/150% scaling).
         float dpiScale = 1.0f;
         if (hwnd) {
            dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
        }
        if (dpiScale < 0.5f) dpiScale = 1.0f;
        if (dpiScale > 4.0f) dpiScale = 4.0f;

        io.Fonts->Clear();
        const char* fontPath = "C:\\Windows\\Fonts\\segoeui.ttf";
        if (std::filesystem::exists(fontPath)) {
            io.Fonts->AddFontFromFileTTF(fontPath, 16.0f * dpiScale);
        } else {
            io.Fonts->AddFontDefault();
        }

        ImGui_ImplWin32_Init(hwnd);

        // Use InitInfo API so texture uploads happen on the SAME queue as our renderer.
        ImGui_ImplDX12_InitInfo initInfo = {};
        initInfo.Device = device;
        initInfo.CommandQueue = dx12->GetD3DCommandQueue();
        initInfo.NumFramesInFlight = static_cast<int>(dx12->GetFramesInFlight());
        initInfo.RTVFormat = dx12->GetBackBufferFormat();
        initInfo.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        initInfo.SrvDescriptorHeap = imguiSrvHeap.Get();
        initInfo.SrvDescriptorAllocFn = ImGuiSrvAlloc;
        initInfo.SrvDescriptorFreeFn = ImGuiSrvFree;
        initInfo.UserData = &imguiSrvAlloc;
        if (!ImGui_ImplDX12_Init(&initInfo)) {
            NEXT_LOG_FATAL("ImGui_ImplDX12_Init failed");
            renderer->Shutdown();
            delete renderer;
            window->Shutdown();
            delete window;
            Next::Logger::Shutdown();
            Next::PlatformShutdown();
            return 1;
        }
        imguiInitialized = true;

        // Render ImGui draw data inside the renderer main pass.
        dx12->SetOverlayRenderCallback([&](ID3D12GraphicsCommandList* cmd) {
            if (!cmd) {
                return;
            }
            static bool s_logged = false;
            if (!s_logged) {
                NEXT_LOG_INFO("ImGui overlay callback active");
                s_logged = true;
            }
            ID3D12DescriptorHeap* heaps[] = {imguiSrvHeap.Get()};
            cmd->SetDescriptorHeaps(1, heaps);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);
        });
    }

    bool running = true;
    bool showDemo = false;
    bool showContent = true;
    bool showImport = true;
    bool showViewport = true;
    bool requestResetLayout = false;

    std::string selectedPackageName;
    std::string selectedAssetName;

    const std::filesystem::path resolvedImportSrc = ResolveRuntimePath("SourceAssets/tri.obj");
    const std::filesystem::path resolvedImportDst = ResolveRuntimePath("assets/tri_import.npkg");
    char importSrc[512] = {};
    char importDst[512] = {};
    std::snprintf(importSrc, sizeof(importSrc), "%s", resolvedImportSrc.string().c_str());
    std::snprintf(importDst, sizeof(importDst), "%s", resolvedImportDst.string().c_str());
    int lastImportCode = 0;
    bool lastImportOk = false;
    bool importAutoLoad = true;
    char lastImportMsg[512] = "";

    // Route window messages to ImGui (input, cursor, etc) and handle basic drag-drop imports.
    window->SetMessageCallback([&](void* nativeWindow,
                                   uint32_t message,
                                   uint64_t wParam,
                                   int64_t lParam,
                                   int64_t* outResult) -> bool {
        if (!nativeWindow || !outResult) {
            return false;
        }

        if (message == WM_DROPFILES) {
            const HDROP drop = reinterpret_cast<HDROP>(wParam);
            char path[MAX_PATH] = {};
            if (DragQueryFileA(drop, 0, path, MAX_PATH) > 0) {
                std::snprintf(importSrc, sizeof(importSrc), "%s", path);
                std::filesystem::path srcPath(path);
                std::filesystem::path dstPath = ResolveRuntimePath("assets") / (srcPath.stem().string() + ".npkg");
                std::snprintf(importDst, sizeof(importDst), "%s", dstPath.string().c_str());
                lastImportCode = 0;
                NEXT_LOG_INFO("Dropped file: %s", path);
            }
            DragFinish(drop);
            *outResult = 0;
            return true;
        }

        if (imguiInitialized) {
            LRESULT r = ImGui_ImplWin32_WndProcHandler(
                static_cast<HWND>(nativeWindow),
                static_cast<UINT>(message),
                static_cast<WPARAM>(wParam),
                static_cast<LPARAM>(lParam));
            *outResult = static_cast<int64_t>(r);
            return r != 0;
        }

        return false;
    });

    std::filesystem::path assetsDir = ResolveRuntimePath("assets");
    auto packages = ScanPackages(assetsDir);
    double lastScanTime = Next::GetTimeInSeconds();

    const double smokeStart = Next::GetTimeInSeconds();
    int frameCount = 0;

    while (running && !window->ShouldClose()) {
        window->PollEvents();

        // Editor main thread helps consume budgeted jobs (up to 0.25ms).
        jobSystem.Pump(0.25);

        if (autoResizeW > 0 && autoResizeH > 0 && autoResizeFrame >= 0 && frameCount == autoResizeFrame) {
            window->Resize(autoResizeW, autoResizeH);
            autoResizeW = 0;
            autoResizeH = 0;
        }

        renderer->BeginFrame();

        if (imguiInitialized) {
            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            // Ensure input and rendering operate in the same coordinate space.
            // If swapchain/backbuffer doesn't match the Win32 client size for any reason (DPI, timing),
            // the OS will scale the output (blur) and ImGui hit-testing will feel offset.
            if (ioPtr && ioPtr->DisplaySize.x > 0.0f && ioPtr->DisplaySize.y > 0.0f) {
                const float sx = static_cast<float>(dx12->GetBackBufferWidth()) / ioPtr->DisplaySize.x;
                const float sy = static_cast<float>(dx12->GetBackBufferHeight()) / ioPtr->DisplaySize.y;
                ioPtr->DisplayFramebufferScale = ImVec2(sx, sy);
            }
            ImGui::NewFrame();
        }

        // Periodic package rescan (1 Hz) to mimic "content browser updates".
        double now = Next::GetTimeInSeconds();
        if (now - lastScanTime > 1.0) {
            packages = ScanPackages(assetsDir);
            lastScanTime = now;
        }

        if (imguiInitialized) {
            // Global dockspace.
            ImGuiWindowFlags hostFlags =
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);
            ImGui::SetNextWindowViewport(vp->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::Begin("##DockHost", nullptr, hostFlags);
            ImGui::PopStyleVar(2);

            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Exit")) {
                        running = false;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Layout")) {
                    if (ImGui::MenuItem("Reset Layout")) {
                        requestResetLayout = true;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Window")) {
                    ImGui::MenuItem("ImGui Demo", nullptr, &showDemo);
                    ImGui::MenuItem("Content", nullptr, &showContent);
                    ImGui::MenuItem("Import", nullptr, &showImport);
                    ImGui::MenuItem("Viewport", nullptr, &showViewport);
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            ImGuiID dockId = ImGui::GetID("NEXT_DockSpace");

            // Ensure we always have a sane default layout and a way to recover if it gets stuck.
            if (requestResetLayout || ImGui::DockBuilderGetNode(dockId) == nullptr) {
                requestResetLayout = false;

                ImGui::DockBuilderRemoveNode(dockId);
                ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
                ImGui::DockBuilderSetNodeSize(dockId, vp->WorkSize);

                ImGuiID dockMain = dockId;
                ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.28f, nullptr, &dockMain);
                ImGuiID dockLeftBottom = ImGui::DockBuilderSplitNode(dockLeft, ImGuiDir_Down, 0.45f, nullptr, &dockLeft);

                ImGui::DockBuilderDockWindow("Content", dockLeft);
                ImGui::DockBuilderDockWindow("Import", dockLeftBottom);
                ImGui::DockBuilderDockWindow("Viewport", dockMain);
                if (showDemo) {
                    ImGui::DockBuilderDockWindow("Dear ImGui Demo", dockMain);
                }
                ImGui::DockBuilderFinish(dockId);
            }

            ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
            ImGui::End();

            if (showDemo) {
                ImGui::ShowDemoWindow(&showDemo);
            }

            // Content Browser
            if (showContent) {
                ImGui::Begin("Content", &showContent);
                ImGui::Text("Assets dir: %s", assetsDir.string().c_str());

                if (packages.empty()) {
                    ImGui::TextDisabled("No .npkg found. Use Import panel or next_assetc.");
                } else if (ImGui::BeginTable("##packages", 4,
                                             ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                                 ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Package");
                    ImGui::TableSetupColumn("Status");
                    ImGui::TableSetupColumn("Action");
                    ImGui::TableSetupColumn("Inspect");
                    ImGui::TableHeadersRow();

                     for (const auto& p : packages) {
                         const std::string pkgName = p.stem().string();
                         const bool loaded = (assetManager.GetPackage(pkgName) != nullptr);
                        const uint32_t pkgRef = loaded ? assetManager.GetPackageRefCount(pkgName) : 0;
 
                         ImGui::TableNextRow();
                         ImGui::TableSetColumnIndex(0);
                         ImGui::TextUnformatted(p.filename().string().c_str());
 
                         ImGui::TableSetColumnIndex(1);
                        if (loaded) {
                            ImGui::Text("Loaded (ref=%u)", pkgRef);
                        } else {
                            ImGui::TextUnformatted("Not loaded");
                        }

                        ImGui::TableSetColumnIndex(2);
                        ImGui::PushID(pkgName.c_str());
                        if (!loaded) {
                            if (ImGui::Button("Load")) {
                                assetManager.LoadPackage(p.string());
                                selectedPackageName = pkgName;
                            }
                        } else {
                            if (ImGui::Button("Unload")) {
                                assetManager.UnloadPackage(pkgName);
                                if (selectedPackageName == pkgName) {
                                    selectedAssetName.clear();
                                }
                            }
                        }
                        ImGui::PopID();

                        ImGui::TableSetColumnIndex(3);
                        ImGui::PushID((pkgName + "_inspect").c_str());
                        if (ImGui::Button("Inspect")) {
                            selectedPackageName = pkgName;
                        }
                        ImGui::PopID();
                    }

                    ImGui::EndTable();
                }

                ImGui::Separator();
                if (!selectedPackageName.empty()) {
                    ImGui::Text("Selected package: %s", selectedPackageName.c_str());
                    auto pkg = assetManager.GetPackage(selectedPackageName);
                    if (!pkg) {
                        ImGui::TextDisabled("Not loaded. Select the package row and click Load.");
                    } else {
                        ImGui::Text("Assets: %u", pkg->GetAssetCount());
                        ImGui::TextWrapped("Package path: %s", pkg->GetFilePath().c_str());
                        ImGui::Text("Package ref count: %u", assetManager.GetPackageRefCount(selectedPackageName));
                        ImGui::Text("Mesh / Texture / Material: %zu / %zu / %zu",
                                    pkg->GetAssetsByType(Next::AssetType::Mesh).size(),
                                    pkg->GetAssetsByType(Next::AssetType::Texture).size(),
                                    pkg->GetAssetsByType(Next::AssetType::Material).size());
                        ImGui::BeginChild("##asset_list", ImVec2(0, 180), true);
                        auto names = pkg->GetAssetNames();
                        for (const std::string& name : names) {
                            const Next::AssetType type = pkg->GetAssetType(name);
                            const bool selected = (selectedAssetName == name);
                            std::string item = name;
                            item += " [";
                            item += Next::AssetTypeToString(type);
                            item += "]";
                            if (ImGui::Selectable(item.c_str(), selected)) {
                                selectedAssetName = name;
                            }
                        }
                        ImGui::EndChild();

                         if (!selectedAssetName.empty()) {
                             const Next::AssetEntry* entry = pkg->GetAssetEntry(selectedAssetName);
                             ImGui::Separator();
                             ImGui::Text("Selected asset: %s", selectedAssetName.c_str());
                             if (entry) {
                                ImGui::Text("Type: %s", Next::AssetTypeToString(entry->assetType));
                                ImGui::Text("Stored size: %s", FormatByteSize(entry->assetSize).c_str());
                                if (entry->compressedSize > 0) {
                                    ImGui::Text("Compressed size: %s", FormatByteSize(entry->compressedSize).c_str());
                                    ImGui::Text("Decompressed size: %s",
                                                FormatByteSize(entry->decompressedSize).c_str());
                                }

                                Next::AssetHeader header = {};
                                if (pkg->ReadAssetHeader(selectedAssetName, header)) {
                                    ImGui::Text("Header payload: %s", FormatByteSize(header.dataSize).c_str());
                                    ImGui::Text("Checksum: 0x%llX",
                                                static_cast<unsigned long long>(header.checksum));
                                }

                                switch (entry->assetType) {
                                case Next::AssetType::Mesh: {
                                    Next::MeshHeader meshHeader = {};
                                    if (ReadTypedAssetHeader(pkg, selectedAssetName, meshHeader)) {
                                        ImGui::Text("Vertices: %u", meshHeader.vertexCount);
                                        ImGui::Text("Indices: %u", meshHeader.indexCount);
                                        ImGui::Text("Vertex stride: %u bytes", meshHeader.vertexStride);
                                        ImGui::Text("Submeshes: %u", meshHeader.materialCount);
                                    }
                                    break;
                                }
                                case Next::AssetType::Texture: {
                                    Next::TextureHeader textureHeader = {};
                                    if (ReadTypedAssetHeader(pkg, selectedAssetName, textureHeader)) {
                                        ImGui::Text("Dimensions: %ux%u", textureHeader.width, textureHeader.height);
                                        ImGui::Text("Mip levels: %u", textureHeader.mipLevels);
                                        ImGui::Text("Format: %u", textureHeader.format);
                                    }
                                    break;
                                }
                                case Next::AssetType::Material: {
                                    Next::MaterialHeader materialHeader = {};
                                    if (ReadTypedAssetHeader(pkg, selectedAssetName, materialHeader)) {
                                        ImGui::Text("Textures: %u", materialHeader.textureCount);
                                        ImGui::Text("Parameters: %u", materialHeader.parameterCount);
                                        ImGui::Text("Shader ID: %u", materialHeader.shaderID);
                                    }
                                    break;
                                }
                                default:
                                    break;
                                }
                             }

                             if (ImGui::Button("Load Asset (Sync)")) {
                                const std::string qualified = selectedPackageName + "::" + selectedAssetName;
                                Next::AssetHandle h = assetManager.LoadAssetSync(qualified);
                                 if (!h.IsValid()) {
                                    NEXT_LOG_WARNING("LoadAssetSync failed: %s", qualified.c_str());
                                 }
                             }
                         }
                    }
                }
                ImGui::End();
            }

            // Import panel (shells out to next_assetc import)
            if (showImport) {
                ImGui::Begin("Import", &showImport);
                ImGui::TextWrapped("Import is implemented via next_assetc.exe (OBJ mesh-only for now).");
                ImGui::InputText("Source", importSrc, sizeof(importSrc));
                ImGui::InputText("Output .npkg", importDst, sizeof(importDst));
                ImGui::Checkbox("Auto-load after import", &importAutoLoad);
                if (ImGui::Button("Run Import")) {
                    lastImportOk = false;
                    std::snprintf(lastImportMsg, sizeof(lastImportMsg), "Running...");

                    const std::filesystem::path src(importSrc);
                    const std::filesystem::path dst(importDst);
                    if (!std::filesystem::exists(src)) {
                        lastImportCode = 1;
                        std::snprintf(lastImportMsg, sizeof(lastImportMsg), "Source not found");
                    } else {
                        if (!dst.parent_path().empty()) {
                            std::error_code ec;
                            std::filesystem::create_directories(dst.parent_path(), ec);
                        }

                        const std::filesystem::path exeDir = GetExeDir();
                        const std::filesystem::path assetcExe = exeDir / "next_assetc.exe";
                        const std::string assetc = std::filesystem::exists(assetcExe) ? assetcExe.string() : "next_assetc.exe";

                        const std::string cmd =
                            "\"" + assetc + "\" import \"" + src.string() + "\" \"" + dst.string() + "\"";
                        lastImportCode = std::system(cmd.c_str());

                        if (lastImportCode == 0 && std::filesystem::exists(dst)) {
                            lastImportOk = true;
                            std::snprintf(lastImportMsg, sizeof(lastImportMsg), "OK: %s", dst.string().c_str());

                            // Refresh content browser immediately.
                            packages = ScanPackages(assetsDir);

                            if (importAutoLoad) {
                                assetManager.LoadPackage(dst.string());
                                selectedPackageName = dst.stem().string();
                            }
                        } else {
                            std::snprintf(lastImportMsg, sizeof(lastImportMsg), "Failed (code=%d)", lastImportCode);
                        }
                    }
                }
                ImGui::SameLine();
                ImGui::Text("last code: %d", lastImportCode);
                if (lastImportMsg[0]) {
                    if (lastImportOk) {
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%s", lastImportMsg);
                    } else {
                        ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.5f, 1.0f), "%s", lastImportMsg);
                    }
                }
                ImGui::End();
            }

            // Viewport status
            if (showViewport) {
                ImGui::Begin("Viewport", &showViewport);
                ImGui::TextDisabled("Viewport shows the renderer's live backbuffer output.");
                ImGui::TextDisabled("Scene rendering still goes through the main swapchain.");
                ImGui::Text("Window: %dx%d", window->GetWidth(), window->GetHeight());
                if (ioPtr) {
                    ImGui::Text("FPS: %.1f", ioPtr->Framerate);
                }
                ImGui::End();
            }

            ImGui::Render();
        }

        renderer->Render();
        renderer->EndFrame();
        window->SwapBuffers();

        ++frameCount;
        if (smokeFrames > 0 && frameCount >= smokeFrames) {
            running = false;
        }
        if (smokeSeconds > 0.0) {
            double t = Next::GetTimeInSeconds() - smokeStart;
            if (t >= smokeSeconds) {
                running = false;
            }
        }
    }

    // Cleanup
    dx12->SetOverlayRenderCallback(nullptr);
    window->SetMessageCallback(nullptr);

    if (imguiInitialized) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        if (imguiSrvAlloc.lockInit) {
            DeleteCriticalSection(&imguiSrvAlloc.lock);
            imguiSrvAlloc.lockInit = false;
        }
        imguiSrvHeap.Reset();
    }

    assetManager.Shutdown();
    jobSystem.Shutdown();

    renderer->Shutdown();
    delete renderer;

    window->Shutdown();
    delete window;

    Next::Logger::Shutdown();
    Next::PlatformShutdown();

    return 0;
}
