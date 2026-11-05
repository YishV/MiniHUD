#include "overlay_ui.h"
#include <cstdio>

int main() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1920, 1080);
    io.DeltaTime = 1.0f / 60;
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    wchar_t directory[MAX_PATH]; GetWindowsDirectoryW(directory, MAX_PATH);
    const std::wstring wide = std::wstring(directory) + L"\\Fonts\\msyh.ttc";
    char path[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, path, sizeof(path), nullptr, nullptr);
    if (!io.Fonts->AddFontFromFileTTF(path, 15)) return 2;
    Config cfg;
    SensorSnapshot snap;
    snap.fps=120.0f; snap.cpuTemp=95.0f; snap.gpuTemp=60.0f; snap.vramUsedGB=4.0f; snap.vramTotalGB=8.0f;
    ImGui::NewFrame();
    DrawStats(cfg,snap,ImVec2(12,12),ImVec2(0,0));
    const bool chineseGlyph=ImGui::GetFontBaked()->FindGlyphNoFallback(0x663E)!=nullptr;
    ImGui::Render();
    // Auto-size settles on the following frame.
    ImGui::NewFrame();
    const auto single = DrawStats(cfg,snap,ImVec2(12,12),ImVec2(0,0));
    ImGui::Render();
    cfg.singleLine=false; cfg.itemsPerRow=1;
    ImGui::NewFrame(); DrawStats(cfg,snap,ImVec2(12,12),ImVec2(0,0)); ImGui::Render();
    ImGui::NewFrame(); const auto multi=DrawStats(cfg,snap,ImVec2(12,12),ImVec2(0,0)); ImGui::Render();
    cfg.singleLine=true;
    cfg.moduleGap=40;
    ImGui::NewFrame(); DrawStats(cfg,snap,ImVec2(12,12),ImVec2(0,0)); ImGui::Render();
    ImGui::NewFrame(); const auto spaced=DrawStats(cfg,snap,ImVec2(12,12),ImVec2(0,0)); ImGui::Render();
    const bool spacingOk=std::abs(spaced.x-single.x-72.0f)<2;
    cfg.englishMetrics=true;
    ImGui::NewFrame(); DrawStats(cfg,snap,ImVec2(12,12),ImVec2(0,0)); ImGui::Render();
    ImGui::NewFrame(); const auto english=DrawStats(cfg,snap,ImVec2(12,12),ImVec2(0,0)); ImGui::Render();
    const bool englishOk=english.x>0 && english.x<1920 && english.y==spaced.y && english.x!=spaced.x;
    SettingsState state;
    std::snprintf(state.hotkeyBuf,sizeof(state.hotkeyBuf),"Ctrl+Shift+F12");
    ImGui::NewFrame(); DrawSettings(cfg,snap,{},state); ImGui::Render();
    // Driver missing: renders the install button (bundled build) or the download button (default).
    snap.cpuNeedsDriver=true;
    ImGui::NewFrame(); const UiAction missing=DrawSettings(cfg,snap,{},state); ImGui::Render();
    const bool missingOk=missing==UiAction::None;
    const bool ok=missingOk && englishOk && spacingOk && chineseGlyph && single.x>100 && single.x<1920 && single.y<50 && multi.y>single.y*2 && ImGui::GetDrawData()->TotalVtxCount>0;
    std::printf("Driver-missing settings layout: %s\n",missingOk?"PASSED":"FAILED");
    std::printf("Chinese UI geometry: %s; single %.0fx%.0f; multi %.0fx%.0f\n",ok?"PASSED":"FAILED",single.x,single.y,multi.x,multi.y);
    std::printf("English metric style: %s; %.0fx%.0f\n",englishOk?"PASSED":"FAILED",english.x,english.y);
    ImGui::DestroyContext();
    return ok?0:1;
}
