#include "overlay_ui.h"
#include "util.h"
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
    // Compact mode puts the value right after the label, so the panel is narrower than fixed-width mode.
    Config fixedCfg=cfg; fixedCfg.fixedValueWidth=true;
    ImGui::NewFrame(); DrawStats(fixedCfg,snap,ImVec2(12,12),ImVec2(0,0)); ImGui::Render();
    ImGui::NewFrame(); const auto fixedWidthStats=DrawStats(fixedCfg,snap,ImVec2(12,12),ImVec2(0,0)); ImGui::Render();
    const bool compactTighter=single.x<fixedWidthStats.x;
    // Docked to the taskbar: wraps into multiple rows within the given width and uses a smaller font.
    Config dockedCfg=cfg; dockedCfg.docked=true; dockedCfg.dockedMaxWidth=220;
    ImGui::NewFrame(); DrawStats(dockedCfg,snap,ImVec2(0,0),ImVec2(0,0)); ImGui::Render();
    ImGui::NewFrame(); const auto dockedStats=DrawStats(dockedCfg,snap,ImVec2(0,0),ImVec2(0,0)); ImGui::Render();
    Config narrowCfg=cfg; narrowCfg.docked=true; narrowCfg.dockedMaxWidth=120;
    ImGui::NewFrame(); DrawStats(narrowCfg,snap,ImVec2(0,0),ImVec2(0,0)); ImGui::Render();
    ImGui::NewFrame(); const auto narrowStats=DrawStats(narrowCfg,snap,ImVec2(0,0),ImVec2(0,0)); ImGui::Render();
    // Docked layout is always two rows; shrinking the width changes the fit, never the row count.
    const bool dockedWraps=dockedStats.y>single.y && dockedStats.y<=64 && narrowStats.y<=dockedStats.y;
    cfg.singleLine=false; cfg.itemsPerRow=1;
    ImGui::NewFrame(); DrawStats(cfg,snap,ImVec2(12,12),ImVec2(0,0)); ImGui::Render();
    ImGui::NewFrame(); const auto multi=DrawStats(cfg,snap,ImVec2(12,12),ImVec2(0,0)); ImGui::Render();
    cfg.singleLine=true;
    cfg.moduleGap=40;
    ImGui::NewFrame(); DrawStats(cfg,snap,ImVec2(12,12),ImVec2(0,0)); ImGui::Render();
    ImGui::NewFrame(); const auto spaced=DrawStats(cfg,snap,ImVec2(12,12),ImVec2(0,0)); ImGui::Render();
    // +24 px per gap (moduleGap 16 -> 40); the number of gaps follows the visible metric count.
    const int gapDelta = int(std::lround(spaced.x-single.x));
    const bool spacingOk=gapDelta>0 && gapDelta%24==0 && gapDelta<=24*10;
    cfg.englishMetrics=true;
    ImGui::NewFrame(); DrawStats(cfg,snap,ImVec2(12,12),ImVec2(0,0)); ImGui::Render();
    ImGui::NewFrame(); const auto english=DrawStats(cfg,snap,ImVec2(12,12),ImVec2(0,0)); ImGui::Render();
    const bool englishOk=english.x>0 && english.x<1920 && english.y==spaced.y && english.x!=spaced.x;
    snap.recording=true;
    ImGui::NewFrame(); DrawStats(cfg,snap,ImVec2(0,0),ImVec2(0,0)); ImGui::Render();
    bool recordingDot=false;
    for (const auto* list:ImGui::GetDrawData()->CmdLists)
        for (const auto& vertex:list->VtxBuffer) if (vertex.col==IM_COL32(255,70,70,255)) recordingDot=true;
    snap.recording=false;
    Config colored;
    colored.fixedValueWidth=true; // the width-stability checks below exercise the fixed-width mode
    colored.showFps=false; colored.showGpuTemp=false; colored.showVram=false;
    colored.labelColors[static_cast<size_t>(Metric::CpuTemp)]=0x00FF00u;
    colored.alertTriggerSeconds=0; colored.alertRecoverySeconds=0;
    HudRuntime runtime;
    auto draw=[&] {
        ImVec2 size;
        for (int i=0;i<3;++i) {
            ImGui::NewFrame(); size=DrawStats(colored,snap,ImVec2(0,0),ImVec2(0,0),&runtime); ImGui::Render();
        }
        return size;
    };
    auto hasColor=[](ImU32 color) {
        for (const auto* list:ImGui::GetDrawData()->CmdLists)
            for (const auto& vertex:list->VtxBuffer) if (vertex.col==color) return true;
        return false;
    };
    snap.cpuTemp=75.0f;
    draw();
    const bool independentColor=hasColor(IM_COL32(0,255,0,255)) && hasColor(IM_COL32(245,245,245,255)) && !hasColor(IM_COL32(255,89,77,255));
    // Light theme draws near-black text (labels and values) instead of white.
    Config lightCfg; lightCfg.lightTheme=true;
    ImGui::NewFrame(); DrawStats(lightCfg,snap,ImVec2(0,0),ImVec2(0,0)); ImGui::Render();
    const bool lightText=hasColor(IM_COL32(15,15,15,255)) && !hasColor(IM_COL32(245,245,245,255));
    snap.cpuTemp=99.0f;
    const auto twoDigits=draw();
    const bool redValue=hasColor(ImGui::ColorConvertFloat4ToU32(ImVec4(1,.35f,.3f,1))) && hasColor(IM_COL32(0,255,0,255));
    snap.cpuTemp=100.0f;
    const auto threeDigits=draw();
    snap.cpuTemp=std::nullopt;
    const auto missingDigits=draw();
    const bool fixedWidth=twoDigits.x==threeDigits.x && threeDigits.x==missingDigits.x && twoDigits.y==threeDigits.y;
    runtime.Notify("开始记录",double(Qpc())/double(QpcFrequency()),60);
    const auto withNotice=draw();
    runtime.noticeUntil=0;
    const auto expiredNotice=draw();
    const bool noticeLayout=withNotice.y>expiredNotice.y && expiredNotice.y==missingDigits.y;
    SettingsState state;
    std::snprintf(state.hotkeyBuf,sizeof(state.hotkeyBuf),"Ctrl+Shift+F12");
    std::snprintf(state.recordHotkeyBuf,sizeof(state.recordHotkeyBuf),"Ctrl+Shift+F11");
    ImGui::NewFrame(); DrawSettings(cfg,snap,{},state); ImGui::Render();
    // Driver missing: renders the install button (bundled build) or the download button (default).
    snap.cpuNeedsDriver=true;
    ImGui::NewFrame(); const UiAction missing=DrawSettings(cfg,snap,{},state); ImGui::Render();
    const bool missingOk=missing==UiAction::None;
    const ImVec2 centerPivot=CornerPivot(Corner::TopCenter);
    const bool pivotOk=centerPivot.x==0.5f && centerPivot.y==0.0f;
    const bool ok=dockedWraps && independentColor && lightText && redValue && fixedWidth && noticeLayout && recordingDot && missingOk && pivotOk && compactTighter && englishOk && spacingOk && chineseGlyph && single.x>100 && single.x<1920 && single.y<50 && multi.y>single.y*2 && ImGui::GetDrawData()->TotalVtxCount>0;
    std::printf("Label/value colors: %s; alert retains label color: %s\n",independentColor?"PASSED":"FAILED",redValue?"PASSED":"FAILED");
    std::printf("Fixed value width: %s; notice layout restores: %s\n",fixedWidth?"PASSED":"FAILED",noticeLayout?"PASSED":"FAILED");
    std::printf("Recording indicator: %s\n",recordingDot?"PASSED":"FAILED");
    std::printf("Driver-missing settings layout: %s\n",missingOk?"PASSED":"FAILED");
    std::printf("Top-center pivot: %s\n",pivotOk?"PASSED":"FAILED");
    std::printf("Compact value gap: %s; light theme: %s; docked wrap: %s\n",compactTighter?"PASSED":"FAILED",lightText?"PASSED":"FAILED",dockedWraps?"PASSED":"FAILED");
    std::printf("Chinese UI geometry: %s; single %.0fx%.0f; multi %.0fx%.0f\n",ok?"PASSED":"FAILED",single.x,single.y,multi.x,multi.y);
    std::printf("English metric style: %s; %.0fx%.0f\n",englishOk?"PASSED":"FAILED",english.x,english.y);
    ImGui::DestroyContext();
    return ok?0:1;
}
