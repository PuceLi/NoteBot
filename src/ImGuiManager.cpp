#include "ImGuiManager.h"
#include "Main.h"
#include "NoteBotUI.h"

#define D3D12_FEATURE_DATA_D3D12_OPTIONS  D3D12_FEATURE_DATA_D3D12_OPTIONS_LEGACY
#define D3D12_FEATURE_DATA_ARCHITECTURE  D3D12_FEATURE_DATA_ARCHITECTURE_LEGACY
#define D3D12_RAYTRACING_GEOMETRY_DESC  D3D12_RAYTRACING_GEOMETRY_DESC_LEGACY

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>
#include <d3d11.h>
#include <d3d12.h>
#include <d3d11on12.h>
#include <dxgi1_4.h>
#include <MinHook.h>
#include <windows.h>
#include <filesystem>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#undef D3D12_FEATURE_DATA_D3D12_OPTIONS
#undef D3D12_FEATURE_DATA_ARCHITECTURE
#undef D3D12_RAYTRACING_GEOMETRY_DESC

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace notebot {

extern std::unique_ptr<NoteBotUI> gUI;

// 单例实例
ImGuiManager& ImGuiManager::getInstance() {
    static ImGuiManager instance;
    return instance;
}

typedef HRESULT(__stdcall* Present_t)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(__stdcall* Present1_t)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
typedef HRESULT(__stdcall* ResizeBuffers_t)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
typedef void(__stdcall* ExecuteCommandLists_t)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

static Present_t oPresent = nullptr;
static Present1_t oPresent1 = nullptr;
static ResizeBuffers_t oResizeBuffers = nullptr;
static ExecuteCommandLists_t oExecuteCommandLists = nullptr;

// D3D 全局变量
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static ID3D11On12Device* g_d3d11On12Device = nullptr;
static ID3D12CommandQueue* g_pCommandQueue = nullptr;
static HWND g_hWnd = nullptr;
static WNDPROC oWndProc = nullptr;
static bool g_imguiInitialized = false;
static std::atomic<bool> g_isRendering{false};
static std::string g_iniPath;
static ULONG_PTR g_gdiplusToken = 0;
static std::array<ID3D11ShaderResourceView*, 3> g_iconViews{};

static ID3D11ShaderResourceView* loadPngIcon(ID3D11Device* device,
                                              const std::filesystem::path& path) {
    Gdiplus::Bitmap image(path.c_str());
    if (image.GetLastStatus() != Gdiplus::Ok) return nullptr;
    const UINT width = image.GetWidth(), height = image.GetHeight();
    if (!width || !height) return nullptr;
    Gdiplus::Rect rect(0, 0, static_cast<INT>(width), static_cast<INT>(height));
    Gdiplus::BitmapData bits{};
    if (image.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bits)
        != Gdiplus::Ok) return nullptr;
    std::vector<std::uint8_t> pixels(size_t(width) * height * 4);
    for (UINT y = 0; y < height; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(bits.Scan0) + ptrdiff_t(y) * bits.Stride;
        std::copy_n(row, size_t(width) * 4, pixels.data() + size_t(y) * width * 4);
    }
    image.UnlockBits(&bits);

    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = pixels.data();
    data.SysMemPitch = width * 4;
    ID3D11Texture2D* texture = nullptr;
    if (FAILED(device->CreateTexture2D(&description, &data, &texture))) return nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    if (FAILED(device->CreateShaderResourceView(texture, nullptr, &view))) view = nullptr;
    texture->Release();
    return view;
}

// WndProc Hook
LRESULT CALLBACK WndProcHook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_imguiInitialized) {
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
    }
    return oWndProc ? CallWindowProc(oWndProc, hWnd, msg, wParam, lParam)
                    : DefWindowProc(hWnd, msg, wParam, lParam);
}

void __stdcall hkExecuteCommandLists(ID3D12CommandQueue* pQueue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) {
    if (!g_pCommandQueue) {
        D3D12_COMMAND_QUEUE_DESC desc = pQueue->GetDesc();
        if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            g_pCommandQueue = pQueue;
            g_pCommandQueue->AddRef();
            NoteBot::getInstance().getSelf().getLogger().info("Captured D3D12 CommandQueue");
        }
    }
    oExecuteCommandLists(pQueue, NumCommandLists, ppCommandLists);
}

// 渲染 ImGui
void RenderImGui(IDXGISwapChain* pSwapChain) {
    if (g_isRendering.exchange(true)) return;

    if (!g_imguiInitialized) {
        HRESULT hr = pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pd3dDevice);
        if (SUCCEEDED(hr)) {
            g_pd3dDevice->GetImmediateContext(&g_pd3dDeviceContext);
            NoteBot::getInstance().getSelf().getLogger().info("Got D3D11 device directly");
        } else {
            if (!g_pCommandQueue) {
                NoteBot::getInstance().getSelf().getLogger().warn("CommandQueue not available yet");
                g_isRendering = false;
                return;
            }

            ID3D12Device* pD3D12Device = nullptr;
            if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&pD3D12Device))) {
                hr = D3D11On12CreateDevice(
                    pD3D12Device,
                    D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                    nullptr,
                    0,
                    (IUnknown**)&g_pCommandQueue,
                    1,
                    0,
                    &g_pd3dDevice,
                    &g_pd3dDeviceContext,
                    nullptr
                );

                if (SUCCEEDED(hr)) {
                    g_pd3dDevice->QueryInterface(__uuidof(ID3D11On12Device), (void**)&g_d3d11On12Device);
                    NoteBot::getInstance().getSelf().getLogger().info("Created D3D11On12 device");
                } else {
                    NoteBot::getInstance().getSelf().getLogger().error("Failed to create D3D11On12 device: {:X}", (unsigned int)hr);
                }
                pD3D12Device->Release();
            }
        }

        if (g_pd3dDevice) {
            DXGI_SWAP_CHAIN_DESC sd;
            pSwapChain->GetDesc(&sd);
            g_hWnd = sd.OutputWindow;
            if (!g_hWnd) g_hWnd = FindWindowW(L"Minecraft", NULL);

            if (g_hWnd) {
                oWndProc = (WNDPROC)SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)WndProcHook);
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                const auto dataDir = NoteBot::getInstance().getSelf().getDataDir();
                g_iniPath = std::filesystem::absolute(dataDir / "imgui.ini").string();
                io.IniFilename = g_iniPath.c_str();
                io.IniSavingRate = 2.0f;
                if (std::filesystem::exists(g_iniPath))
                    ImGui::LoadIniSettingsFromDisk(g_iniPath.c_str());

                ImGui::StyleColorsDark();
                auto& style = ImGui::GetStyle();
                style.WindowRounding = style.FrameRounding = style.ChildRounding = 0.0f;
                style.Colors[ImGuiCol_WindowBg] = ImVec4(20/255.f, 20/255.f, 20/255.f, 160/255.f);
                style.Colors[ImGuiCol_FrameBg] = ImVec4(20/255.f, 20/255.f, 20/255.f, 160/255.f);
                style.Colors[ImGuiCol_Button] = ImVec4(20/255.f, 20/255.f, 20/255.f, 160/255.f);
                style.Colors[ImGuiCol_ButtonHovered] = ImVec4(30/255.f, 30/255.f, 30/255.f, 175/255.f);
                style.Colors[ImGuiCol_ButtonActive] = ImVec4(40/255.f, 40/255.f, 40/255.f, 190/255.f);
                style.Colors[ImGuiCol_Header] = ImVec4(145/255.f, 61/255.f, 226/255.f, 1.0f);
                style.Colors[ImGuiCol_HeaderHovered] = style.Colors[ImGuiCol_Header];
                style.Colors[ImGuiCol_CheckMark] = style.Colors[ImGuiCol_Header];
                style.Colors[ImGuiCol_SliderGrab] = ImVec4(130/255.f, 0, 1, 1);
                style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(150/255.f, 60/255.f, 1, 1);
                style.Colors[ImGuiCol_Border] = ImVec4(0, 0, 0, 1);

                const auto fontPath = NoteBot::getInstance().getSelf().getDataDir() / "Comfortaa.ttf";
                if (std::filesystem::exists(fontPath)) {
                    if (!io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 18.0f))
                        io.Fonts->AddFontDefault();
                } else {
                    io.Fonts->AddFontDefault();
                }
                ImFontConfig cjkConfig;
                cjkConfig.MergeMode = true;
                cjkConfig.PixelSnapH = true;
                if (GetFileAttributesW(L"C:\\Windows\\Fonts\\msyh.ttc") != INVALID_FILE_ATTRIBUTES) {
                    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 18.0f,
                                                 &cjkConfig, io.Fonts->GetGlyphRangesChineseFull());
                }
                if (GetFileAttributesW(L"C:\\Windows\\Fonts\\YuGothR.ttc") != INVALID_FILE_ATTRIBUTES) {
                    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\YuGothR.ttc", 18.0f,
                                                 &cjkConfig, io.Fonts->GetGlyphRangesJapanese());
                }

                Gdiplus::GdiplusStartupInput gdiplusInput;
                if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr) == Gdiplus::Ok) {
                    const auto icons = dataDir / "ui";
                    g_iconViews[0] = loadPngIcon(g_pd3dDevice, icons / "reset.png");
                    g_iconViews[1] = loadPngIcon(g_pd3dDevice, icons / "copy.png");
                    g_iconViews[2] = loadPngIcon(g_pd3dDevice, icons / "paste.png");
                }
                if (gUI) gUI->setIconTextures(
                    reinterpret_cast<std::uintptr_t>(g_iconViews[0]),
                    reinterpret_cast<std::uintptr_t>(g_iconViews[1]),
                    reinterpret_cast<std::uintptr_t>(g_iconViews[2]));

                ImGui_ImplWin32_Init(g_hWnd);
                ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

                g_imguiInitialized = true;
                NoteBot::getInstance().getSelf().getLogger().info("ImGui initialized successfully");
            }
        }
    }

    if (!g_imguiInitialized) {
        g_isRendering = false;
        return;
    }

    // 渲染 ImGui
    auto renderFrame = [](ID3D11RenderTargetView* rtv) {
        g_pd3dDeviceContext->OMSetRenderTargets(1, &rtv, nullptr);
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // 渲染 NoteBot UI
        extern void RenderNoteBotUI();
        RenderNoteBotUI();

        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        ID3D11RenderTargetView* nullRTV = nullptr;
        g_pd3dDeviceContext->OMSetRenderTargets(1, &nullRTV, nullptr);
    };

    if (g_d3d11On12Device) {
        UINT bufferIndex = 0;
        IDXGISwapChain3* pSwapChain3 = nullptr;
        if (SUCCEEDED(pSwapChain->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&pSwapChain3))) {
            bufferIndex = pSwapChain3->GetCurrentBackBufferIndex();
            pSwapChain3->Release();
        }

        ID3D12Resource* d3d12BackBuffer = nullptr;
        if (SUCCEEDED(pSwapChain->GetBuffer(bufferIndex, __uuidof(ID3D12Resource), (void**)&d3d12BackBuffer))) {
            ID3D11Resource* wrappedBackBuffer = nullptr;
            D3D11_RESOURCE_FLAGS d3d11Flags = {D3D11_BIND_RENDER_TARGET};

            if (SUCCEEDED(g_d3d11On12Device->CreateWrappedResource(
                d3d12BackBuffer,
                &d3d11Flags,
                D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_PRESENT,
                __uuidof(ID3D11Resource),
                (void**)&wrappedBackBuffer)))
            {
                ID3D11RenderTargetView* rtv = nullptr;
                g_pd3dDevice->CreateRenderTargetView(wrappedBackBuffer, nullptr, &rtv);

                if (rtv) {
                    g_d3d11On12Device->AcquireWrappedResources(&wrappedBackBuffer, 1);
                    renderFrame(rtv);
                    g_d3d11On12Device->ReleaseWrappedResources(&wrappedBackBuffer, 1);
                    rtv->Release();
                }

                wrappedBackBuffer->Release();
                g_pd3dDeviceContext->Flush();
            }
            d3d12BackBuffer->Release();
        }
    } else {
        ID3D11Texture2D* pBackBuffer = nullptr;
        if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer))) {
            ID3D11RenderTargetView* rtv = nullptr;
            g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &rtv);
            pBackBuffer->Release();

            if (rtv) {
                renderFrame(rtv);
                rtv->Release();
            }
        }
    }

    g_isRendering = false;
}

HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    RenderImGui(pSwapChain);
    return oPresent(pSwapChain, SyncInterval, Flags);
}

HRESULT __stdcall hkPresent1(IDXGISwapChain1* pSwapChain, UINT SyncInterval, UINT Flags,
                             const DXGI_PRESENT_PARAMETERS* pParams) {
    RenderImGui(pSwapChain);
    return oPresent1(pSwapChain, SyncInterval, Flags, pParams);
}

HRESULT __stdcall hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    if (g_imguiInitialized) {
        ImGui_ImplDX11_InvalidateDeviceObjects();
    }
    HRESULT result = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    if (g_imguiInitialized) {
        ImGui_ImplDX11_CreateDeviceObjects();
    }
    return result;
}

bool ImGuiManager::initialize() {
    HWND hwnd = FindWindowW(L"Minecraft", NULL);
    if (!hwnd) hwnd = GetForegroundWindow();
    bool createdDummyHwnd = false;
    if (!hwnd) {
        hwnd = CreateWindowExW(0, L"STATIC", L"NoteBotDummy", WS_OVERLAPPED, 0, 0, 100, 100, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
        createdDummyHwnd = (hwnd != nullptr);
    }
    if (!hwnd) {
        NoteBot::getInstance().getSelf().getLogger().error("Failed to get window handle");
        return false;
    }

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device* dummyDevice = nullptr;
    IDXGISwapChain* dummySwapChain = nullptr;
    ID3D11DeviceContext* dummyContext = nullptr;

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        NoteBot::getInstance().getSelf().getLogger().error("MH_Initialize failed: {}", (int)status);
        if (createdDummyHwnd && hwnd) DestroyWindow(hwnd);
        return false;
    }

    bool success = false;
    if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &featureLevel, 1, D3D11_SDK_VERSION, &sd, &dummySwapChain, &dummyDevice, nullptr, &dummyContext))) {
        void** pVTable = *reinterpret_cast<void***>(dummySwapChain);

        if (MH_CreateHook(pVTable[8], (LPVOID)hkPresent, (void**)&oPresent) == MH_OK) {
            MH_EnableHook(pVTable[8]);
            NoteBot::getInstance().getSelf().getLogger().info("Hooked IDXGISwapChain::Present");
        }

        IDXGISwapChain1* dummySwapChain1 = nullptr;
        if (SUCCEEDED(dummySwapChain->QueryInterface(__uuidof(IDXGISwapChain1),
                                                      (void**)&dummySwapChain1))) {
            void** pVTable1 = *reinterpret_cast<void***>(dummySwapChain1);
            if (MH_CreateHook(pVTable1[22], (LPVOID)hkPresent1, (void**)&oPresent1) == MH_OK) {
                MH_EnableHook(pVTable1[22]);
                NoteBot::getInstance().getSelf().getLogger().info("Hooked IDXGISwapChain1::Present1");
            }
            dummySwapChain1->Release();
        }

        if (MH_CreateHook(pVTable[13], (LPVOID)hkResizeBuffers, (void**)&oResizeBuffers) == MH_OK) {
            MH_EnableHook(pVTable[13]);
            NoteBot::getInstance().getSelf().getLogger().info("Hooked IDXGISwapChain::ResizeBuffers");
        }

        dummySwapChain->Release();
        dummyDevice->Release();
        dummyContext->Release();
        success = true;
    }

    ID3D12Device* pDummyD12Device = nullptr;
    if (SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&pDummyD12Device))) {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

        ID3D12CommandQueue* pDummyQueue = nullptr;
        if (SUCCEEDED(pDummyD12Device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue), (void**)&pDummyQueue))) {
            void** pQueueVTable = *reinterpret_cast<void***>(pDummyQueue);

            if (MH_CreateHook(pQueueVTable[10], (LPVOID)hkExecuteCommandLists, (void**)&oExecuteCommandLists) == MH_OK) {
                MH_EnableHook(pQueueVTable[10]);
                NoteBot::getInstance().getSelf().getLogger().info("Hooked ID3D12CommandQueue::ExecuteCommandLists");
            }

            pDummyQueue->Release();
        }
        pDummyD12Device->Release();
    }

    if (createdDummyHwnd && hwnd) DestroyWindow(hwnd);
    NoteBot::getInstance().getSelf().getLogger().info("ImGuiManager initialized successfully");
    mInitialized = success;
    return success;
}

void ImGuiManager::shutdown() {
    mInitialized = false;
    if (g_imguiInitialized) {
        if (!g_iniPath.empty()) ImGui::SaveIniSettingsToDisk(g_iniPath.c_str());
        if (gUI) gUI->setIconTextures(0, 0, 0);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_imguiInitialized = false;
    }
    for (auto*& view : g_iconViews) {
        if (view) { view->Release(); view = nullptr; }
    }
    if (g_gdiplusToken) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
    g_iniPath.clear();

    if (g_hWnd && oWndProc) {
        SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
        oWndProc = nullptr;
    }

    if (g_d3d11On12Device) {
        g_d3d11On12Device->Release();
        g_d3d11On12Device = nullptr;
    }
    if (g_pd3dDeviceContext) {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
    if (g_pCommandQueue) {
        g_pCommandQueue->Release();
        g_pCommandQueue = nullptr;
    }

    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();

    NoteBot::getInstance().getSelf().getLogger().info("ImGuiManager shutdown");
}

} // namespace notebot
