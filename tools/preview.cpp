// Renders the docked HUD off-screen to a BMP, so the layout can be reviewed without launching
// the elevated app. Run: minihud_preview.exe out.bmp

#include "overlay_ui.h"
#include "util.h"

#include <d3d11.h>
#include <wrl/client.h>
#include "imgui_impl_dx11.h"
#include <cstring>

#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

bool SaveBmp(const wchar_t* path, const unsigned char* bgra, int width, int height) {
    const int rowBytes = width * 4;
    const int imageBytes = rowBytes * height;
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + imageBytes;
    info.biSize = sizeof(info);
    info.biWidth = width;
    info.biHeight = height; // positive: bottom-up
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    info.biSizeImage = imageBytes;

    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, &file, sizeof(file), &written, nullptr) != 0 &&
              WriteFile(f, &info, sizeof(info), &written, nullptr) != 0;
    for (int y = height - 1; y >= 0 && ok; --y)
        ok = WriteFile(f, bgra + y * rowBytes, rowBytes, &written, nullptr) != 0;
    CloseHandle(f);
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    wchar_t out[MAX_PATH] = L"preview.bmp";
    if (argc > 1) MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, out, MAX_PATH);

    const int width = 900, height = 64;

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level{};
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                 &device, &level, &context))) {
        std::printf("D3D11CreateDevice failed\n");
        return 1;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &target))) return 1;
    ComPtr<ID3D11RenderTargetView> rtv;
    if (FAILED(device->CreateRenderTargetView(target.Get(), nullptr, &rtv))) return 1;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(width), float(height));
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    char fontPath[MAX_PATH * 3] = {};
    {
        wchar_t windows[MAX_PATH];
        GetWindowsDirectoryW(windows, MAX_PATH);
        const std::wstring font = std::wstring(windows) + L"\\Fonts\\msyh.ttc";
        WideCharToMultiByte(CP_UTF8, 0, font.c_str(), -1, fontPath, sizeof(fontPath), nullptr, nullptr);
    }
    if (!io.Fonts->AddFontFromFileTTF(fontPath, 15)) return 2;
    if (!ImGui_ImplDX11_Init(device.Get(), context.Get())) return 3;

    Config cfg;
    cfg.docked = true;
    cfg.dockedMaxWidth = 720;
    cfg.showFps = false;
    cfg.showFrameTime = false;
    cfg.showLowFps = false;
    cfg.showLow01Fps = false;
    cfg.showCpuUsage = true;
    cfg.showCpuTemp = true;
    cfg.showGpuUsage = true;
    cfg.showGpuTemp = true;
    cfg.showCpuPower = true;
    cfg.showGpuPower = true;
    cfg.showVram = true;
    cfg.showRam = true;
    cfg.showDiskTemp = true;
    cfg.showNetworkSpeed = true;
    cfg.showTime = true;

    SensorSnapshot snap;
    snap.cpuUsage = 27.0f;
    snap.cpuTemp = 99.0f;
    snap.gpuUsage = 26.0f;
    snap.gpuTemp = 57.0f;
    snap.cpuPowerW = 36.0f;
    snap.gpuPowerW = 19.0f;
    snap.vramUsedGB = 1.5f;
    snap.vramTotalGB = 8.0f;
    snap.ramUsedGB = 24.5f;
    snap.ramTotalGB = 31.7f;
    snap.diskTemp = 59.0f;
    snap.networkDownMBps = 13.0f;
    snap.networkUpMBps = 0.1f;
    snap.clockText = "15:50";

    ImVec2 panel{};
    for (int i = 0; i < 5; ++i) {
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        panel = DrawStats(cfg, snap, ImVec2(6, 6), ImVec2(0, 0));
        ImGui::Render();

        const float clear[4] = {0.93f, 0.93f, 0.93f, 1.0f}; // light taskbar background
        context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
        context->ClearRenderTargetView(rtv.Get(), clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    D3D11_TEXTURE2D_DESC staging = desc;
    staging.Usage = D3D11_USAGE_STAGING;
    staging.BindFlags = 0;
    staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    if (FAILED(device->CreateTexture2D(&staging, nullptr, &readback))) return 4;
    context->CopyResource(readback.Get(), target.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &map))) return 5;

    std::vector<unsigned char> pixels(size_t(width) * height * 4);
    for (int y = 0; y < height; ++y)
        std::memcpy(pixels.data() + size_t(y) * width * 4,
                    static_cast<const unsigned char*>(map.pData) + size_t(y) * map.RowPitch, size_t(width) * 4);
    context->Unmap(readback.Get(), 0);

    const bool ok = SaveBmp(out, pixels.data(), width, height);
    std::printf("panel=%.0fx%.0f saved=%s\n", panel.x, panel.y, ok ? "ok" : "failed");

    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    return ok ? 0 : 6;
}
