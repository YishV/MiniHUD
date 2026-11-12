#include "overlay_ui.h"

#include "pawnio_setup.h"
#include "sensors/system_io.h"
#include "util.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

// While docked inside the taskbar the panel shrinks a little so two rows still fit its height.
constexpr float kDockedFontScale = 0.9f;
const ImVec4 kFpsColor(0.56f, 0.87f, 0.36f, 1.0f);
const ImVec4 kValueColor(0.96f, 0.96f, 0.96f, 1.0f);
const ImVec4 kMissingColor(0.6f, 0.6f, 0.6f, 1.0f);
const ImVec4 kHotColor(1.0f, 0.35f, 0.3f, 1.0f);

// How far behind real time the frame graph is drawn. Fast graph updates flush ETW every ~33-47 ms,
// so frames have normally arrived by then; otherwise the newest part of the line appears late.
constexpr double kGraphDelayMs = 120.0;
// Height of the frame graph's 0 ms line, as a fraction of the graph height from the bottom.
constexpr float kGraphZero = 0.1f;
// Frame time at the top of the frame graph.
constexpr float kGraphMaxMs = 60.0f;

// Frame times over the last kFrameGraphSeconds, newest at the right edge. Each frame spans its
// duration on the time axis, and a pixel column shows the slowest frame in it, so spikes stay visible.
void DrawFrameGraph(const SensorSnapshot& snap, float width) {
    const float fontSize = ImGui::GetFontSize();
    const float height = std::round(fontSize * 2.5f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + width, p0.y + height);
    ImGui::Dummy(ImVec2(width, height));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 16), ImGui::GetStyle().FrameRounding);

    const std::vector<float>& frames = snap.frameTimesMs;
    if (frames.empty() || width < 2.0f) return;

    // The graph scrolls with the clock (not with data arrival, which comes in bursts), slightly
    // behind real time so that ETW has delivered the frames by the time they are drawn.
    const double msPerTick = 1000.0 / double(QpcFrequency());
    const double msPerPx = kFrameGraphSeconds * 1000.0 / width;
    const double rightMs = double(Qpc()) * msPerTick - kGraphDelayMs;
    const double leftMs = rightMs - kFrameGraphSeconds * 1000.0;

    // When each frame ended, oldest first.
    const size_t n = frames.size();
    std::vector<double> ends(n);
    double end = double(snap.lastFrameQpc) * msPerTick;
    for (size_t i = n; i-- > 0;) {
        ends[i] = end;
        end -= frames[i];
    }

    // Each frame's stutter limit: twice the average of the normal (non-stutter) frames in the second
    // before it. Leaving stutters out keeps one long hitch from inflating the limit for the next
    // second. If the frame rate drops for good, the reference runs out of normal frames after a
    // second and restarts from the new frame rate. Only earlier frames count, so a frame keeps its
    // color while it scrolls across the graph.
    std::vector<float> limits(n, 0.0f); // 0 = nothing to compare with
    std::vector<char> stutters(n, 0);
    double normalSum = 0.0;
    int normalCount = 0;
    for (size_t i = 0, first = 0; i < n; ++i) {
        const double start = ends[i] - frames[i];
        for (; first < i && ends[first] <= start - kStutterWindowSeconds * 1000.0; ++first) {
            if (stutters[first]) continue;
            normalSum -= frames[first];
            --normalCount;
        }
        if (normalCount > 0) limits[i] = float(2.0 * normalSum / normalCount);
        stutters[i] = limits[i] > 0.0f && frames[i] > limits[i];
        if (!stutters[i]) {
            normalSum += frames[i];
            ++normalCount;
        }
    }

    // Columns sit on a fixed time grid, so a frame stays in the same column as the graph scrolls,
    // and the grid is drawn at a sub-pixel offset: the line slides instead of shimmering. A column
    // shows its slowest frame (so spikes stay visible), and is a stutter if any frame in it is.
    struct Column {
        float slowest = 0.0f; // 0 = no frame
        bool stutter = false;
    };
    const int64_t firstCol = (int64_t)std::floor(leftMs / msPerPx);
    const int64_t lastCol = (int64_t)std::floor(rightMs / msPerPx);
    std::vector<Column> cols(size_t(lastCol - firstCol + 1));
    int counted = 0;
    for (size_t i = n; i-- > 0 && ends[i] > leftMs;) {
        const double start = ends[i] - frames[i];
        if (start >= rightMs) continue;
        const bool stutter = stutters[i] != 0;
        const int64_t c0 = std::max(firstCol, (int64_t)std::floor(start / msPerPx));
        const int64_t c1 = std::min(lastCol, (int64_t)std::ceil(ends[i] / msPerPx) - 1);
        for (int64_t c = c0; c <= c1; ++c) {
            Column& col = cols[size_t(c - firstCol)];
            col.slowest = std::max(col.slowest, frames[i]);
            col.stutter |= stutter;
        }
        ++counted;
    }
    if (counted == 0) return;
    const double colOffset = leftMs / msPerPx; // x of column c is c - colOffset
    auto columnX = [&](size_t i) { return p0.x + float(double(firstCol + int64_t(i)) + 0.5 - colOffset); };

    // A fixed scale, so the line never jumps; slower frames are drawn at the top edge.
    const float top = kGraphMaxMs;

    // 0 ms sits a bit above the bottom edge (kGraphZero of the height); the scale runs above it.
    const float thickness = std::max(1.0f, fontSize / 12.0f);
    const float zeroY = p1.y - thickness * 0.5f - (height - thickness) * kGraphZero;
    const float usable = zeroY - (p0.y + thickness * 0.5f);
    auto valueY = [&](float ms) { return zeroY - std::min(ms / top, 1.0f) * usable; };

    const ImU32 gridColor = IM_COL32(255, 255, 255, 30);
    const float midY = std::round(valueY(top * 0.5f));
    draw->AddLine(ImVec2(p0.x, midY), ImVec2(p1.x, midY), gridColor);
    char label[16];
    std::snprintf(label, sizeof(label), "%.0f ms", top);
    const float pad = std::round(fontSize * 0.15f);
    draw->AddText(nullptr, fontSize * 0.7f, ImVec2(p0.x + pad * 2.0f, p0.y + pad), ImGui::GetColorU32(kMissingColor), label);

    draw->PushClipRect(p0, p1, true); // the partial columns at both ends

    // The frame times; a segment is red when either end is a stutter (both edges of a spike).
    for (size_t i = 1; i < cols.size(); ++i) {
        const Column& a = cols[i - 1];
        const Column& b = cols[i];
        if (a.slowest <= 0.0f || b.slowest <= 0.0f) continue;
        const ImU32 color = ImGui::GetColorU32(a.stutter || b.stutter ? kHotColor : kFpsColor);
        draw->AddLine(ImVec2(columnX(i - 1), valueY(a.slowest)), ImVec2(columnX(i), valueY(b.slowest)), color, thickness);
    }
    draw->PopClipRect();
}

} // namespace

ImVec2 CornerPivot(Corner c) {
    switch (c) {
    case Corner::TopLeft: return {0.0f, 0.0f};
    case Corner::TopRight: return {1.0f, 0.0f};
    case Corner::BottomLeft: return {0.0f, 1.0f};
    case Corner::BottomRight: return {1.0f, 1.0f};
    case Corner::TopCenter: return {0.5f, 0.0f};
    }
    return {1.0f, 0.0f};
}

namespace {
struct HudItem { int id; std::string label, text; ImVec4 color; };
const char* const metricLabels[] = {"帧率", "帧时间", "CPU 占用", "CPU 温度", "GPU 占用", "GPU 温度",
    "CPU 功耗", "GPU 功耗", "显存", "内存", "SSD 温度", "磁盘读取", "磁盘写入", "下载", "上传",
    "1% 低帧", "0.1% 低帧", "时间"};
const char* const englishLabels[] = {"FPS", "Frame Time", "CPU Load", "CPU Temp", "GPU Load", "GPU Temp",
    "CPU Power", "GPU Power", "VRAM", "RAM", "SSD Temp", "Disk Read", "Disk Write", "Down", "Up",
    "1% Low", "0.1% Low", "Time"};
const char* const metricUnits[] = {"低于 FPS", "达到 ms", "达到 %", "达到 ℃", "达到 %", "达到 ℃",
    "达到 W", "达到 W", "达到 %", "达到 %", "达到 ℃", "达到 MB/s", "达到 MB/s", "达到 MB/s", "达到 MB/s",
    "低于 FPS", "低于 FPS", ""};
std::string StatusText(const std::string& text) {
    if (text.find("not installed") != std::string::npos) return "未安装 PawnIO 驱动";
    if (text.find("access denied") != std::string::npos) return "权限不足，请以管理员身份运行";
    if (text.find("PawnIO OK") != std::string::npos) return "PawnIO 正常（Intel 支持为实验性）";
    if (text.find("NVML OK") != std::string::npos) return "NVIDIA NVML 正常";
    if (text.find("session running") != std::string::npos) return "ETW 帧率采集运行中";
    if (text.find("No storage") != std::string::npos) return "设备未提供温度数据";
    if (text.find("Windows storage") != std::string::npos) return "Windows 存储温度采集正常";
    if (text.empty()) return "等待数据";
    return "诊断信息：" + text;
}
}

ImVec2 DrawStats(const Config& cfg, const SensorSnapshot& snap, ImVec2 pos, ImVec2 pivot, HudRuntime* runtime) {
    static HudRuntime fallback;
    HudRuntime& hud=runtime ? *runtime : fallback;
    const double now=double(Qpc())/double(QpcFrequency());
    // Light theme: dark text for a transparent overlay over light backgrounds. While docked to the
    // taskbar the taskbar's own theme wins, so the text blends into it.
    const bool docked = cfg.docked;
    const bool light = docked ? TaskbarUsesLightTheme() : cfg.lightTheme;
    const ImVec4 valueColor = light ? ImVec4(0.06f,0.06f,0.06f,1) : kValueColor;
    const ImVec4 missingColor = light ? ImVec4(0.42f,0.42f,0.42f,1) : kMissingColor;
    const ImVec4 hotColor = light ? ImVec4(0.78f,0.08f,0.08f,1) : kHotColor;
    std::vector<HudItem> items;
    auto add = [&](Metric metric, const std::optional<float>& value, const char* format) {
        const size_t index = static_cast<size_t>(metric);
        char text[80];
        if (value) std::snprintf(text, sizeof(text), format, *value);
        else std::snprintf(text, sizeof(text), "--");
        const bool hot=hud.alerts[index].Update(metric,value,cfg.alertLimits[index],cfg.alertsEnabled,now,
            cfg.alertTriggerSeconds,cfg.alertRecoverySeconds);
        items.push_back({static_cast<int>(index), cfg.englishMetrics ? englishLabels[index] : metricLabels[index], text,
            !value ? missingColor : hot ? hotColor : valueColor});
    };
    auto temp = [&](Metric metric, const std::optional<float>& value) {
        add(metric, value, "%.0f C");
        if (value && cfg.fahrenheit) {
            char text[32]; std::snprintf(text, sizeof(text), "%.0f F", *value * 1.8f + 32);
            items.back().text = text;
        }
    };
    auto memory = [&](Metric metric, const std::optional<float>& used, const std::optional<float>& total) {
        const std::optional<float> percent = used && total && *total > 0 ? std::optional<float>(*used / *total * 100) : std::nullopt;
        add(metric, percent, "%.0f%%");
        if (percent) {
            char text[64]; std::snprintf(text, sizeof(text), "%.1f / %.1f GB", *used, *total);
            items.back().text = text;
        }
    };
    if (cfg.showFps) add(Metric::Fps, snap.fps, "%.0f");
    if (cfg.showFrameTime) add(Metric::FrameTime, snap.frameTimeMs, "%.1f ms");
    if (cfg.showLowFps) add(Metric::FpsLow1, snap.fpsLow1, "%.0f");
    if (cfg.showLow01Fps) add(Metric::FpsLow01, snap.fpsLow01, "%.0f");
    if (cfg.showCpuUsage) add(Metric::CpuUsage, snap.cpuUsage, "%.0f%%");
    if (cfg.showCpuTemp) temp(Metric::CpuTemp, snap.cpuTemp);
    if (cfg.showGpuUsage) add(Metric::GpuUsage, snap.gpuUsage, "%.0f%%");
    if (cfg.showGpuTemp) temp(Metric::GpuTemp, snap.gpuTemp);
    if (cfg.showCpuPower) add(Metric::CpuPower, snap.cpuPowerW, "%.0f W");
    if (cfg.showGpuPower) add(Metric::GpuPower, snap.gpuPowerW, "%.0f W");
    if (cfg.showVram) memory(Metric::Vram, snap.vramUsedGB, snap.vramTotalGB);
    if (cfg.showRam) memory(Metric::Ram, snap.ramUsedGB, snap.ramTotalGB);
    if (cfg.showDiskTemp) temp(Metric::DiskTemp, snap.diskTemp);
    if (cfg.showDiskSpeed) {
        add(Metric::DiskRead, snap.diskReadMBps, "%.1f MB/s");
        add(Metric::DiskWrite, snap.diskWriteMBps, "%.1f MB/s");
    }
    if (cfg.showNetworkSpeed) {
        add(Metric::NetDown, snap.networkDownMBps, "%.1f MB/s");
        add(Metric::NetUp, snap.networkUpMBps, "%.1f MB/s");
    }
    if (cfg.showTime) {
        const size_t index = static_cast<size_t>(Metric::Time);
        items.push_back({static_cast<int>(index), cfg.englishMetrics ? englishLabels[index] : metricLabels[index],
                         snap.clockText.empty() ? "--:--" : snap.clockText, valueColor});
    }
    std::array<bool,kMetricCount> visible{};
    for (const auto& item:items) visible[item.id]=true;
    for (size_t i=0;i<kMetricCount;++i) if (!visible[i]) hud.alerts[i]={};
    const bool noticeVisible=hud.NoticeVisible(now);
    if (items.empty() && !cfg.showFrameGraph && !snap.recording && !noticeVisible) return {};
    std::array<int,kMetricCount> rank{};
    for (size_t i=0;i<kMetricCount;++i) rank[cfg.metricOrder[i]]=static_cast<int>(i);
    std::sort(items.begin(),items.end(),[&](const auto& a,const auto& b){ return rank[a.id]<rank[b.id]; });
    // Docked: exactly two balanced, centred rows. Shrink the font until both rows fit the width.
    const float baseFont = ImGui::GetStyle().FontSizeBase;
    float dockScale = kDockedFontScale;
    int dockSplit = 0;
    std::vector<float> dockWidths;
    if (docked && !items.empty()) {
        const int count = static_cast<int>(items.size());
        dockSplit = (count + 1) / 2;
        dockWidths.resize(items.size());
        auto rowWidth = [&](int begin, int end) {
            float sum = 0;
            for (int i = begin; i < end; ++i) sum += dockWidths[size_t(i)];
            if (end > begin) sum += 16.0f * float(end - begin - 1);
            return sum;
        };
        for (float scale = kDockedFontScale; scale >= 0.6f; scale -= 0.05f) {
            ImGui::PushFont(nullptr, baseFont * scale);
            for (size_t i = 0; i < items.size(); ++i)
                dockWidths[i] = ImGui::CalcTextSize(items[i].label.c_str()).x + 7 +
                                ImGui::CalcTextSize(items[i].text.c_str()).x;
            ImGui::PopFont();
            dockScale = scale;
            if (std::max(rowWidth(0, dockSplit), rowWidth(dockSplit, count)) <= float(cfg.dockedMaxWidth)) break;
        }
    }
    const float fontSize = baseFont * (docked ? dockScale : cfg.fontScale);
    if (hud.fontSize!=fontSize) { hud.valueWidths.fill(0); hud.fontSize=fontSize; }
    ImGui::PushFont(nullptr, fontSize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, light ? ImVec4(1.0f,1.0f,1.0f,1.0f) : ImVec4(0.06f,0.06f,0.06f,1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, docked ? ImVec2(6, 1) : ImVec2(9, 5));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(docked ? 0.0f : cfg.backgroundAlpha);
    ImVec2 size{};
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("##stats", nullptr, flags)) {
        const ImVec2 start = ImGui::GetCursorPos();
        float x = snap.recording ? 12.0f : 0.0f, width = 0;
        if (snap.recording) {
            const auto screen=ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(screen.x+4,screen.y+ImGui::GetTextLineHeight()/2),3,IM_COL32(255,70,70,255));
        }
        int row = 0, col = 0;
        const float rowGap = docked ? std::min(cfg.rowGap, 2.0f) : cfg.rowGap;
        const float moduleGap = docked ? std::min(cfg.moduleGap, 16.0f) : cfg.moduleGap;
        const float height = ImGui::GetTextLineHeight() + rowGap;
        const float available = docked && cfg.dockedMaxWidth > 0
            ? float(cfg.dockedMaxWidth)
            : std::max(100.0f, ImGui::GetIO().DisplaySize.x - 18.0f);
        const float lineHeight = ImGui::GetTextLineHeight();
        if (docked && !items.empty()) {
            const int count = static_cast<int>(items.size());
            const int split = std::min(dockSplit, count);
            float w1 = 0, w2 = 0;
            for (int i = 0; i < split; ++i) w1 += dockWidths[size_t(i)] + 16.0f;
            for (int i = split; i < count; ++i) w2 += dockWidths[size_t(i)] + 16.0f;
            if (split) w1 -= 16.0f;
            if (split < count) w2 -= 16.0f;
            const float panelWidth = std::max(w1, w2);
            for (int row = 0; row < 2; ++row) {
                const int begin = row ? split : 0;
                const int end = row ? count : split;
                if (begin >= end) continue;
                float rx = x + (panelWidth - (row ? w2 : w1)) * 0.5f;
                const float ry = start.y + row * (lineHeight + rowGap);
                for (int i = begin; i < end; ++i) {
                    const HudItem& item = items[size_t(i)];
                    const float labelWidth = ImGui::CalcTextSize(item.label.c_str()).x;
                    const ImVec4 labelColor = light ? ImVec4(0.32f,0.32f,0.32f,1) : ImVec4(0.78f,0.78f,0.78f,1);
                    ImGui::SetCursorPos(ImVec2(start.x + rx, ry));
                    ImGui::TextColored(labelColor, "%s", item.label.c_str());
                    ImGui::SetCursorPos(ImVec2(start.x + rx + labelWidth + 7, ry));
                    ImGui::TextColored(item.color, "%s", item.text.c_str());
                    rx += dockWidths[size_t(i)] + 16.0f;
                }
            }
            width = x + panelWidth;
            ImGui::SetCursorPos(start);
            ImGui::Dummy(ImVec2(width, 2.0f * lineHeight + rowGap));
        } else {
        for (const auto& item : items) {
            const float labelWidth=ImGui::CalcTextSize(item.label.c_str()).x;
            const float textWidth=ImGui::CalcTextSize(item.text.c_str()).x;
            float& valueWidth=hud.valueWidths[item.id];
            if (cfg.fixedValueWidth)
                valueWidth=std::max({valueWidth,textWidth,ImGui::CalcTextSize(ValueWidthSample(static_cast<Metric>(item.id),cfg.fahrenheit)).x});
            // Compact (default) puts the value right after the label; fixed width right-aligns it in a
            // reserved slot, which keeps trailing modules from moving but leaves a visible gap.
            const float slot=cfg.fixedValueWidth ? valueWidth : textWidth;
            const float itemWidth=labelWidth+7+slot;
            if (col && ((!cfg.singleLine && col >= cfg.itemsPerRow) || x + itemWidth > available)) {
                width = std::max(width, x - moduleGap); x = 0; ++row; col = 0;
            }
            ImGui::SetCursorPos(ImVec2(start.x + x, start.y + row * height));
            const auto rgb=cfg.labelColors[item.id];
            // Docked: ignore custom colours so the text matches the taskbar instead of standing out.
            const ImVec4 labelColor = docked
                ? (light ? ImVec4(0.32f,0.32f,0.32f,1) : ImVec4(0.78f,0.78f,0.78f,1))
                : (light && rgb==0xFFFFFFu
                    ? ImVec4(0.06f,0.06f,0.06f,1)
                    : ImVec4(((rgb>>16)&255)/255.0f,((rgb>>8)&255)/255.0f,(rgb&255)/255.0f,1));
            ImGui::TextColored(labelColor, "%s", item.label.c_str());
            ImGui::SetCursorPos(ImVec2(start.x+x+labelWidth+7+slot-textWidth,start.y+row*height));
            ImGui::TextColored(item.color, "%s", item.text.c_str());
            x += itemWidth + moduleGap; ++col;
        }
        width = std::max(width, x > 0 ? x - moduleGap : 180);
        ImGui::SetCursorPos(start);
        ImGui::Dummy(ImVec2(width, items.empty() && !snap.recording ? 0 : (row + 1) * height - rowGap));
        }
        if (cfg.showFrameGraph) DrawFrameGraph(snap, width);
        if (noticeVisible) ImGui::TextColored(valueColor,"%s",hud.notice.c_str());
        size = ImGui::GetWindowSize();
    }
    ImGui::End(); ImGui::PopStyleVar(3); ImGui::PopStyleColor(); ImGui::PopFont();
    return size;
}

UiAction DrawSettings(Config& cfg, const SensorSnapshot& snap, const std::vector<MonitorInfo>& monitors, SettingsState& state) {
    UiAction action = UiAction::None;
    const auto display = ImGui::GetIO().DisplaySize;
    const bool editing = ImGui::IsAnyItemActive();
    ImGui::SetNextWindowPos(ImVec2(display.x/2, display.y/2), ImGuiCond_Appearing, ImVec2(.5f,.5f));
    ImGui::SetNextWindowSize(ImVec2(600, std::min(display.y - 40, 690.0f)), ImGuiCond_Appearing);
    bool open = true;
    if (ImGui::Begin("MiniHUD 设置", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (ImGui::BeginTabBar("##tabs")) {
            if (ImGui::BeginTabItem("显示设置")) {
                ImGui::BeginChild("##display", ImVec2(0,-45));
                ImGui::SeparatorText("显示指标");
                bool* toggles[] = {&cfg.showFps,&cfg.showFrameTime,&cfg.showCpuUsage,&cfg.showCpuTemp,&cfg.showGpuUsage,&cfg.showGpuTemp,
                    &cfg.showCpuPower,&cfg.showGpuPower,&cfg.showVram,&cfg.showRam,&cfg.showDiskTemp};
                if (ImGui::BeginTable("##metrics", 2)) {
                    for (size_t i=0;i<std::size(toggles);++i) { ImGui::TableNextColumn(); ImGui::Checkbox(metricLabels[i],toggles[i]); }
                    ImGui::TableNextColumn(); ImGui::Checkbox("1% 低帧", &cfg.showLowFps);
                    ImGui::TableNextColumn(); ImGui::Checkbox("0.1% 低帧", &cfg.showLow01Fps);
                    ImGui::SetItemTooltip("最近 60 秒里最慢的 1% 和 0.1% 帧对应的帧率，越低说明卡顿越重。");
                    ImGui::TableNextColumn(); ImGui::Checkbox("显示时间",&cfg.showTime);
                    ImGui::SetItemTooltip("在卡片上显示本地时间（时:分）。");
                    ImGui::TableNextColumn(); ImGui::Checkbox("磁盘读写速度", &cfg.showDiskSpeed);
                    ImGui::TableNextColumn(); ImGui::Checkbox("网络上传 / 下载", &cfg.showNetworkSpeed);
                    ImGui::TableNextColumn(); ImGui::Checkbox("帧时间曲线", &cfg.showFrameGraph);
                    ImGui::TableNextColumn(); ImGui::BeginDisabled(!cfg.showFrameGraph);
                    ImGui::Checkbox("曲线流畅刷新（增加开销）", &cfg.fastFrameGraph); ImGui::EndDisabled();
                    ImGui::EndTable();
                }
                ImGui::SeparatorText("布局与位置");
                int language=cfg.englishMetrics ? 1 : 0;
                const char* languages[]={"中文指标", "English 指标"};
                if (ImGui::Combo("指标语言", &language, languages, 2)) cfg.englishMetrics=language==1;
                if (ImGui::TreeNode("指标名称颜色")) {
                    ImGui::TextWrapped("只修改名称颜色；数值保持正常白色、告警红色，无数据为灰色。默认所有名称为白色。");
                    for (size_t i=0;i<kMetricCount;++i) {
                        const auto rgb=cfg.labelColors[i];
                        float color[]={((rgb>>16)&255)/255.0f,((rgb>>8)&255)/255.0f,(rgb&255)/255.0f};
                        ImGui::PushID(static_cast<int>(i));
                        if (ImGui::ColorEdit3(metricLabels[i],color,ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoAlpha))
                            cfg.labelColors[i]=(uint32_t(std::lround(color[0]*255))<<16)|(uint32_t(std::lround(color[1]*255))<<8)|uint32_t(std::lround(color[2]*255));
                        ImGui::PopID();
                    }
                    if (ImGui::Button("名称全部恢复白色")) cfg.labelColors.fill(0xFFFFFFu);
                    ImGui::TreePop();
                }
                ImGui::Checkbox("单行显示", &cfg.singleLine);
                if (!cfg.singleLine) ImGui::SliderInt("每行指标数量", &cfg.itemsPerRow, 1, 8);
                ImGui::SliderFloat("指标水平间距", &cfg.moduleGap, 0, 80, "%.0f px");
                if (!cfg.singleLine) ImGui::SliderFloat("指标行间距", &cfg.rowGap, 0, 40, "%.0f px");
                if (ImGui::TreeNode("拖动调整指标顺序")) {
                    int source=-1,target=-1;
                    ImGui::TextWrapped("拖动下方指标到目标位置；关闭的指标也会保留排序。");
                    ImGui::BeginChild("##metric-order",ImVec2(0,150),ImGuiChildFlags_Borders);
                    for (int id:cfg.metricOrder) {
                        ImGui::PushID(id);
                        ImGui::Selectable(metricLabels[id]);
                        if (ImGui::BeginDragDropSource()) {
                            ImGui::SetDragDropPayload("MINIHUD_METRIC",&id,sizeof(id));
                            ImGui::TextUnformatted(metricLabels[id]); ImGui::EndDragDropSource();
                        }
                        if (ImGui::BeginDragDropTarget()) {
                            if (const auto* payload=ImGui::AcceptDragDropPayload("MINIHUD_METRIC")) {
                                if (payload->DataSize==sizeof(int)) { source=*static_cast<const int*>(payload->Data); target=id; }
                            }
                            ImGui::EndDragDropTarget();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                    if (source>=0) MoveMetric(cfg.metricOrder,source,target);
                    if (ImGui::Button("恢复指标顺序")) cfg.metricOrder=kDefaultMetricOrder;
                    ImGui::TreePop();
                }
                const char* corners[] = {"左上角","右上角","左下角","右下角","顶部居中"};
                int corner = static_cast<int>(cfg.corner);
                if (ImGui::Combo("屏幕位置", &corner, corners, 5)) cfg.corner = static_cast<Corner>(corner);
                ImGui::Checkbox("非全屏时贴到任务栏",&cfg.dockTaskbar);
                ImGui::SetItemTooltip("前台不是全屏应用时，整条停靠到任务栏左端的空白处；全屏游戏时回到上面的位置。任务栏高度不够会回到原位置。");
                ImGui::BeginDisabled(!cfg.dockTaskbar);
                ImGui::SliderInt("任务栏停靠宽度",&cfg.dockWidth,0,1400,"%d px");
                ImGui::SetItemTooltip("停靠时按这个宽度换行，调到任务栏图标左侧的空白区即可；0 表示按任务栏宽度的 45%% 自动计算。");
                ImGui::EndDisabled();
                const std::string current = cfg.monitor < static_cast<int>(monitors.size()) ? monitors[cfg.monitor].label : "显示器未连接";
                if (ImGui::BeginCombo("显示器",current.c_str())) {
                    for (int i=0;i<static_cast<int>(monitors.size());++i)
                        if (ImGui::Selectable(monitors[i].label.c_str(),i==cfg.monitor)) cfg.monitor=i;
                    ImGui::EndCombo();
                }
                ImGui::DragInt("水平边距",&cfg.offsetX,1,0,kMaxOffset,"%d px");
                ImGui::DragInt("垂直边距",&cfg.offsetY,1,0,kMaxOffset,"%d px");
                if (ImGui::Button("紧贴屏幕边缘")) { cfg.offsetX=0; cfg.offsetY=0; }
                ImGui::SliderFloat("字体大小",&cfg.fontScale,kMinFontScale,kMaxFontScale,"%.2fx");
                ImGui::SliderFloat("背景不透明度",&cfg.backgroundAlpha,0,1,"%.2f");
                ImGui::Checkbox("固定数值宽度（数字变化时不跳动）",&cfg.fixedValueWidth);
                ImGui::SetItemTooltip("关闭时数值紧跟名称，间距更小；开启时右侧对齐，后面的指标位置更稳定。");
                const char* themes[]={"深色","浅色","跟随系统"};
                int theme=static_cast<int>(cfg.themeMode);
                if (ImGui::Combo("主题", &theme, themes, 3)) {
                    cfg.themeMode=static_cast<ThemeMode>(theme);
                    cfg.lightTheme=EffectiveLightTheme(cfg.themeMode);
                }
                ImGui::SetItemTooltip("深色为白字，浅色为黑字；跟随系统时按 Windows 应用主题自动切换，改系统主题后立即生效。");
                ImGui::Checkbox("使用华氏温度（告警阈值仍为摄氏度）",&cfg.fahrenheit);
                ImGui::SeparatorText("行为");
                bool startup=state.startup.enabled;
                if (ImGui::Checkbox("登录 Windows 后自动启动", &startup))
                    action=startup ? UiAction::EnableStartup : UiAction::DisableStartup;
                ImGui::SetItemTooltip("默认启用；登录后显示黑条和托盘。显隐快捷键仍同步控制两者，开关立即生效。");
                if (!state.startup.error.empty()) ImGui::TextWrapped("%s",state.startup.error.c_str());
                if (!state.startupMessage.empty()) ImGui::TextWrapped("%s",state.startupMessage.c_str());
                if (state.startup.enabled && !state.startup.currentPath) {
                    ImGui::TextWrapped("启动项路径或启动方式与当前版本不一致，请更新启动项。");
                    if (ImGui::Button("更新当前启动项")) action=UiAction::EnableStartup;
                }
                ImGui::SliderInt("刷新间隔",&cfg.updateIntervalMs,kMinUpdateIntervalMs,kMaxUpdateIntervalMs,"%d ms",ImGuiSliderFlags_Logarithmic);
                UINT mods=0,vk=0;
                ImGui::InputText("显示 / 隐藏快捷键",state.hotkeyBuf,sizeof(state.hotkeyBuf));
                ImGui::SetItemTooltip("快捷键同时控制黑条和托盘图标显隐。隐藏时再次运行程序，也能打开设置。");
                if (ParseHotkey(state.hotkeyBuf,mods,vk)) cfg.hotkey=state.hotkeyBuf;
                else ImGui::TextColored(kHotColor,"快捷键格式无效，例如 Ctrl+Shift+F12");
                ImGui::InputText("开始 / 停止录制快捷键",state.recordHotkeyBuf,sizeof(state.recordHotkeyBuf));
                UINT recordMods=0,recordVk=0;
                if (ParseHotkey(state.recordHotkeyBuf,recordMods,recordVk) && (recordMods!=mods || recordVk!=vk))
                    cfg.recordHotkey=state.recordHotkeyBuf;
                else ImGui::TextColored(kHotColor,"录制快捷键须有效，且不能与显隐快捷键相同。");
                if (!state.recordHotkeyRegistered) ImGui::TextWrapped("录制快捷键未能注册，可能被占用；请更换快捷键并保存。");
                ImGui::Checkbox("切换应用后继续跟踪游戏帧率",&cfg.trackGame);
                ImGui::BeginDisabled(!cfg.trackGame); ImGui::Checkbox("仅在检测到游戏时显示",&cfg.onlyWithGame); ImGui::EndDisabled();
                ImGui::BeginDisabled(!cfg.onlyWithGame); ImGui::Checkbox("仅在游戏获得焦点时显示",&cfg.onlyWhenFocused); ImGui::EndDisabled();
                ImGui::Checkbox("不显示在截图和录屏中",&cfg.hideFromCapture);
                ImGui::TextWrapped("单行超出屏幕宽度时会自动换行。磁盘速度为所有物理磁盘的总吞吐。无数据时显示 --。");
                ImGui::EndChild(); ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("告警阈值")) {
                ImGui::BeginChild("##alerts",ImVec2(0,-45));
                ImGui::Checkbox("启用红色告警",&cfg.alertsEnabled);
                ImGui::TextWrapped("帧率低于阈值时标红；其他指标达到阈值时标红。填 0 关闭单项告警。温度阈值为摄氏度，内存和显存按占用百分比判断。阈值是提醒设置，不是硬件安全上限。");
                ImGui::BeginDisabled(!cfg.alertsEnabled);
                ImGui::SliderFloat("连续超标时间",&cfg.alertTriggerSeconds,0,10,"%.1f 秒");
                ImGui::SliderFloat("恢复等待时间",&cfg.alertRecoverySeconds,0,10,"%.1f 秒");
                ImGui::TextWrapped("默认连续超标 3 秒才标红。恢复需回落一定幅度并持续 3 秒，例如温度 90℃ 告警后，降至 87℃ 才开始计时。时间设为 0 可立即切换；检查随刷新间隔进行。");
                for (size_t i=0;i<kMetricCount;++i) {
                    if (static_cast<Metric>(i)==Metric::Time) continue; // clock has no threshold
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::SetNextItemWidth(120);
                    ImGui::InputFloat(metricLabels[i],&cfg.alertLimits[i],1,5,"%.1f");
                    ImGui::SameLine(); ImGui::TextDisabled("%s",metricUnits[i]);
                    ImGui::PopID();
                }
                ImGui::EndDisabled();
                if (ImGui::Button("恢复默认阈值")) { cfg.alertLimits=kDefaultLimits; cfg.alertTriggerSeconds=3; cfg.alertRecoverySeconds=3; }
                ImGui::EndChild(); ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("性能记录")) {
                ImGui::BeginChild("##record", ImVec2(0,-45));
                ImGui::TextWrapped("记录游戏过程中的帧率和温度。停止后会生成每秒明细 CSV 和一份可读报告，保存在 %%APPDATA%%\\MiniHUD\\records。");
                ImGui::Text("录制快捷键：%s",cfg.recordHotkey.c_str());
                ImGui::TextWrapped("黑条上的小红点表示正在录制。开始和保存后会短暂提示 3 秒。隐藏黑条不会停止录制或自动弹出提示；再次按录制快捷键停止。");
                if (snap.recording) {
                    ImGui::Text("正在记录：%d 分 %02d 秒，样本 %d 个", int(snap.recordSeconds)/60, int(snap.recordSeconds)%60, snap.recordSamples);
                    if (ImGui::Button("停止并生成报告")) action=UiAction::StopRecording;
                } else {
                    if (ImGui::Button("开始记录")) action=UiAction::StartRecording;
                    ImGui::SameLine();
                    if (ImGui::Button("打开记录目录")) action=UiAction::OpenRecords;
                }
                if (!snap.recordMessage.empty()) ImGui::TextWrapped("%s", snap.recordMessage.c_str());
                ImGui::TextWrapped("提示：记录期间让游戏保持在前台；1% low 至少要 1 分钟数据才稳定。记录只保存在本机，不会上传。");
                ImGui::EndChild(); ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("电脑配置")) {
                ImGui::BeginChild("##computer",ImVec2(0,-45));
                if (ImGui::Button("复制配置摘要")) action=UiAction::CopyHardwareSummary;
                ImGui::SetItemTooltip("复制 CPU、显卡、内存、主板、硬盘和显示器信息，不包含序列号或日志。");
                if (!state.copyMessage.empty()) { ImGui::SameLine(); ImGui::TextUnformatted(state.copyMessage.c_str()); }
                MEMORYSTATUSEX memory{sizeof(memory)}; GlobalMemoryStatusEx(&memory);
                ImGui::TextWrapped("处理器：%s",snap.cpuName.c_str());
                if (snap.cpuCores) {
                    if (snap.cpuMaxMhz > snap.cpuBaseMhz * 1.05f)
                        ImGui::TextWrapped("CPU：%s · %d 核 %d 线程 · %.1f / %.1f GHz",snap.cpuVendor.c_str(),snap.cpuCores,
                                           snap.cpuThreads,snap.cpuBaseMhz/1000.0f,snap.cpuMaxMhz/1000.0f);
                    else
                        ImGui::TextWrapped("CPU：%s · %d 核 %d 线程 · %.1f GHz",snap.cpuVendor.c_str(),snap.cpuCores,
                                           snap.cpuThreads,snap.cpuBaseMhz/1000.0f);
                }
                if (!snap.systemVendor.empty() || !snap.systemModel.empty())
                    ImGui::TextWrapped("制造商 / 型号：%s %s",snap.systemVendor.c_str(),snap.systemModel.c_str());
                if (!snap.baseboardVendor.empty() || !snap.baseboardProduct.empty())
                    ImGui::TextWrapped("主板：%s %s",snap.baseboardVendor.c_str(),snap.baseboardProduct.c_str());
                if (!snap.biosVendor.empty() || !snap.biosVersion.empty())
                    ImGui::TextWrapped("BIOS：%s %s（%s）",snap.biosVendor.c_str(),snap.biosVersion.c_str(),snap.biosDate.c_str());
                if (snap.gpuVendor.empty())
                    ImGui::TextWrapped("显卡：%s",snap.gpuName.c_str());
                else
                    ImGui::TextWrapped("显卡：%s（%s）",snap.gpuName.c_str(),snap.gpuVendor.c_str());
                ImGui::Text("物理内存：%.1f GB",memory.ullTotalPhys/(1024.0*1024*1024));
                for (const std::string& module : snap.ramModules) ImGui::TextWrapped("内存条：%s",module.c_str());
                if (snap.vramTotalGB) ImGui::Text("显存容量：%.1f GB",*snap.vramTotalGB);
                if (!state.diskInfoLoaded) { state.diskInfo=ReadDiskTemperature(); state.diskInfoLoaded=true; }
                if (state.diskInfoLoaded) {
                    if (state.diskInfo.capacityGB && !state.diskInfo.bus.empty())
                        ImGui::TextWrapped("固态硬盘：%s · %.0f GB · %s",state.diskInfo.name.empty()?"未知":state.diskInfo.name.c_str(),
                                           *state.diskInfo.capacityGB,state.diskInfo.bus.c_str());
                    else
                        ImGui::TextWrapped("固态硬盘：%s",state.diskInfo.name.empty()?"未知":state.diskInfo.name.c_str());
                }
                OSVERSIONINFOW version{sizeof(version)};
                if (HMODULE ntdll=GetModuleHandleW(L"ntdll.dll")) {
                    using VersionFn=LONG(WINAPI*)(OSVERSIONINFOW*);
                    if (auto fn=reinterpret_cast<VersionFn>(GetProcAddress(ntdll,"RtlGetVersion"))) fn(&version);
                }
                if (version.dwBuildNumber) ImGui::Text("Windows 内部版本：%lu",version.dwBuildNumber);
                for (const MonitorInfo& monitor : monitors) {
                    const char* monitorName = monitor.name.empty() ? monitor.deviceName.c_str() : monitor.name.c_str();
                    if (monitor.refreshHz)
                        ImGui::TextWrapped("显示器：%s %dx%d @ %d Hz",monitorName,monitor.width,monitor.height,monitor.refreshHz);
                    else
                        ImGui::TextWrapped("显示器：%s %dx%d",monitorName,monitor.width,monitor.height);
                }
                ImGui::SeparatorText("采集状态");
                ImGui::TextWrapped("帧率目标：%s（PID %lu）",snap.foregroundProcess.c_str(),snap.foregroundPid);
                ImGui::TextWrapped("帧率：%s",StatusText(snap.fpsStatus).c_str());
                ImGui::TextWrapped("CPU：%s",StatusText(snap.cpuStatus).c_str());
                if (state.pawnioInstalling) ImGui::TextUnformatted("正在下载并安装 PawnIO...");
                else if (snap.cpuNeedsDriver) {
                    if (ImGui::Button("安装 PawnIO 驱动")) action=UiAction::InstallPawnIO;
                    ImGui::SetItemTooltip(PawnIOSetupBundled()
                        ? "使用内置的官方安装器，需要管理员权限。"
                        : "从官方地址下载安装器（约 3.4 MB），校验 SHA-256 后以管理员权限安装。");
                    if (!PawnIOSetupBundled()) {
                        ImGui::SameLine();
                        if (ImGui::Button("手动下载")) action=UiAction::OpenPawnIODownload;
                        ImGui::SetItemTooltip("在浏览器中打开官方下载页。安装完成后关闭并重新打开设置即可重新检测。");
                    }
                }
                if (!state.pawnioMessage.empty()) ImGui::TextWrapped("%s",state.pawnioMessage.c_str());
                ImGui::TextWrapped("GPU：%s",StatusText(snap.gpuStatus).c_str());
                ImGui::TextWrapped("SSD：%s",StatusText(state.diskInfo.status).c_str());
                ImGui::SeparatorText("诊断");
                if (ImGui::Button("导出诊断信息")) action=UiAction::ExportDiagnostics;
                ImGui::SetItemTooltip("把版本、硬件、传感器状态和最近日志写入 %APPDATA%\\MiniHUD\\diagnostics。");
                if (!state.diagMessage.empty()) ImGui::TextWrapped("%s",state.diagMessage.c_str());
                ImGui::EndChild(); ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::Separator();
        if (ImGui::Button("保存并关闭")) action=UiAction::CloseSettings;
        ImGui::SameLine();
        if (ImGui::Button("恢复全部默认设置")) { const bool startupApplied=cfg.startupDefaultApplied; cfg=Config{}; cfg.startupDefaultApplied=startupApplied; std::snprintf(state.hotkeyBuf,sizeof(state.hotkeyBuf),"%s",cfg.hotkey.c_str()); std::snprintf(state.recordHotkeyBuf,sizeof(state.recordHotkeyBuf),"%s",cfg.recordHotkey.c_str()); }
        ImGui::SameLine(); if (ImGui::Button("退出 MiniHUD")) action=UiAction::ExitApp;
    }
    ImGui::End();
    if (!open || (!editing && ImGui::IsKeyPressed(ImGuiKey_Escape))) action=UiAction::CloseSettings;
    cfg.Clamp(); return action;
}
