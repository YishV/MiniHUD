#include "recorder.h"

#include "frame_stats.h"
#include "util.h"

#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace {

std::wstring RecordsDirectory() {
    PWSTR roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &roaming)) || !roaming) return {};
    std::wstring dir(roaming);
    CoTaskMemFree(roaming);
    if (dir.empty()) return {};
    dir += L"\\MiniHUD\\records";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir + L'\\';
}

std::wstring Timestamp() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t text[64];
    std::swprintf(text, 64, L"%04u-%02u-%02u_%02u-%02u-%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                  now.wSecond);
    return text;
}

bool WriteUtf8(const std::wstring& path, const std::string& text, bool bom) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = true;
    if (bom) {
        const unsigned char prefix[] = {0xEF, 0xBB, 0xBF};
        ok = WriteFile(file, prefix, sizeof(prefix), &written, nullptr) != 0;
    }
    if (ok) ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != 0;
    CloseHandle(file);
    return ok;
}

float Mean(const std::vector<std::optional<float>>& values, bool& any) {
    double sum = 0.0;
    int count = 0;
    any = false;
    for (const auto& value : values) {
        if (!value) continue;
        sum += *value;
        ++count;
        any = true;
    }
    return count ? static_cast<float>(sum / count) : 0.0f;
}

float Peak(const std::vector<std::optional<float>>& values, bool& any) {
    float peak = 0.0f;
    any = false;
    for (const auto& value : values) {
        if (!value) continue;
        peak = any ? std::max(peak, *value) : *value;
        any = true;
    }
    return peak;
}

// Longest run of consecutive samples at or above `limit`, in seconds.
int LongestRunSeconds(const std::vector<std::optional<float>>& values, float limit) {
    int best = 0, run = 0;
    for (const auto& value : values) {
        run = value && *value >= limit ? run + 1 : 0;
        best = std::max(best, run);
    }
    return best;
}

} // namespace

void PerformanceRecorder::Start() {
    Clear();
    startQpc_ = Qpc();
    active_ = true;
}

void PerformanceRecorder::Stop() { active_ = false; }

void PerformanceRecorder::Clear() {
    frames_.clear();
    samples_.clear();
    acceptedEndQpc_ = 0;
    csvPath_.clear();
    reportPath_.clear();
    active_ = false;
    startQpc_ = 0;
}

double PerformanceRecorder::Seconds() const {
    return startQpc_ ? double(Qpc() - startQpc_) / double(QpcFrequency()) : 0.0;
}

void PerformanceRecorder::AddFrameTimes(const std::vector<float>& framesMs, int64_t lastFrameQpc) {
    if (!active_ || framesMs.empty() || lastFrameQpc == 0) return;
    const double ticksPerMs = double(QpcFrequency()) / 1000.0;
    const size_t n = framesMs.size();

    // Rebuild each frame's end time from the newest one backwards, then keep only the new frames.
    std::vector<double> ends(n);
    double end = double(lastFrameQpc);
    for (size_t i = n; i-- > 0;) {
        ends[i] = end;
        end -= double(framesMs[i]) * ticksPerMs;
    }
    const double accepted = double(acceptedEndQpc_);
    for (size_t i = 0; i < n; ++i) {
        if (ends[i] <= accepted) continue;
        if (framesMs[i] > 0.0f) frames_.push_back(framesMs[i]);
    }
    acceptedEndQpc_ = std::max<int64_t>(acceptedEndQpc_, lastFrameQpc);
}

void PerformanceRecorder::AddSample(const RecordSample& sample) {
    if (!active_) return;
    samples_.push_back(sample);
    if (samples_.size() > 100000) samples_.erase(samples_.begin()); // ~27 hours at 1 Hz
}

bool PerformanceRecorder::Save(std::string& message) {
    if (samples_.empty()) {
        message = "没有记录到数据，请先开始记录。";
        return false;
    }
    const std::wstring directory = RecordsDirectory();
    if (directory.empty()) {
        message = "无法创建记录目录。";
        return false;
    }

    const std::wstring stamp = Timestamp();
    csvPath_ = directory + L"MiniHUD-" + stamp + L".csv";
    reportPath_ = directory + L"MiniHUD-" + stamp + L".md";

    std::ostringstream csv;
    csv << "秒,帧率,帧时间ms,1%low,0.1%low,CPU占用%,CPU温度C,CPU功耗W,GPU占用%,GPU温度C,GPU功耗W,显存GB,内存GB\n";
    auto cell = [&](const std::optional<float>& value) {
        if (value) csv << *value;
    };
    for (const RecordSample& s : samples_) {
        csv << s.elapsed;
        csv << ','; cell(s.fps);
        csv << ','; cell(s.frameTimeMs);
        csv << ','; cell(s.low1);
        csv << ','; cell(s.low01);
        csv << ','; cell(s.cpuUsage);
        csv << ','; cell(s.cpuTemp);
        csv << ','; cell(s.cpuPowerW);
        csv << ','; cell(s.gpuUsage);
        csv << ','; cell(s.gpuTemp);
        csv << ','; cell(s.gpuPowerW);
        csv << ','; cell(s.vramUsedGB);
        csv << ','; cell(s.ramUsedGB);
        csv << '\n';
    }
    if (!WriteUtf8(csvPath_, csv.str(), true)) {
        message = "无法写入 CSV 文件。";
        return false;
    }

    // Whole-session statistics from every frame, not from the per-second means.
    const FrameStats stats = ComputeFrameStats(frames_);
    std::vector<std::optional<float>> fps, cpuUsage, cpuTemp, gpuUsage, gpuTemp, vramUsed;
    for (const RecordSample& s : samples_) {
        fps.push_back(s.fps);
        cpuUsage.push_back(s.cpuUsage);
        cpuTemp.push_back(s.cpuTemp);
        gpuUsage.push_back(s.gpuUsage);
        gpuTemp.push_back(s.gpuTemp);
        vramUsed.push_back(s.vramUsedGB);
    }
    bool any = false;
    const float avgFps = Mean(fps, any);
    const float cpuAvg = Mean(cpuUsage, any);
    const float cpuPeak = Peak(cpuTemp, any);
    const float gpuAvg = Mean(gpuUsage, any);
    const float gpuPeak = Peak(gpuTemp, any);
    const float vramPeak = Peak(vramUsed, any);

    const int seconds = static_cast<int>(samples_.back().elapsed) + 1;
    std::ostringstream md;
    md << "# MiniHUD 性能记录\n\n";
    md << "- 时长：" << seconds / 60 << " 分 " << seconds % 60 << " 秒\n";
    md << "- 每秒样本：" << samples_.size() << " 个，逐帧样本：" << stats.frames << " 帧\n\n";

    md << "## 关键数字\n\n";
    md << "| 指标 | 数值 |\n|---|---|\n";
    md << "| 平均帧率 | " << std::lround(avgFps) << " FPS |\n";
    if (stats.frames >= 64) {
        md << "| 1% low | " << std::lround(stats.low1Fps) << " FPS |\n";
        md << "| 0.1% low | " << std::lround(stats.low01Fps) << " FPS |\n";
        md << "| 帧时间 中位 / P95 / P99 | " << std::lround(stats.p50Ms) << " / " << std::lround(stats.p95Ms) << " / "
           << std::lround(stats.p99Ms) << " ms |\n";
        md << "| 卡顿帧 | " << stats.stutters << "（"
           << (stats.frames ? std::lround(1000.0 * double(stats.stutters) / double(stats.frames)) / 10.0 : 0.0)
           << "% 的帧超过 33 ms 或中位数两倍） |\n";
    }
    md << "| CPU 占用均值 / 温度峰值 | " << std::lround(cpuAvg) << "% / " << std::lround(cpuPeak) << "℃ |\n";
    md << "| GPU 占用均值 / 温度峰值 | " << std::lround(gpuAvg) << "% / " << std::lround(gpuPeak) << "℃ |\n";
    if (vramPeak > 0.0f) md << "| 显存峰值 | " << vramPeak << " GB |\n";

    md << "\n## 结论\n\n";
    if (stats.frames >= 64) {
        const double stutterPercent = 100.0 * double(stats.stutters) / double(stats.frames);
        const int lowText = std::lround(stats.low1Fps);
        const int ratioText = avgFps > 0.0f ? std::lround(100.0 * stats.low1Fps / avgFps) : 0;
        if (stutterPercent < 1.0)
            md << "- 整体流畅：卡顿帧占 " << std::lround(stutterPercent * 10.0) / 10.0 << "%，1% low 还有 " << lowText
               << " FPS。\n";
        else if (stutterPercent < 5.0)
            md << "- 偶有卡顿：卡顿帧占 " << std::lround(stutterPercent * 10.0) / 10.0 << "%，1% low 掉到 " << lowText
               << " FPS（平均帧率的 " << ratioText << "%）。\n";
        else
            md << "- 卡顿明显：卡顿帧占 " << std::lround(stutterPercent * 10.0) / 10.0 << "%，1% low 只有 " << lowText
               << " FPS。\n";
    } else {
        md << "- 帧数据太少，无法判断卡顿，建议记录 1 分钟以上。\n";
    }
    const int hotSeconds = LongestRunSeconds(gpuTemp, 85.0f);
    if (hotSeconds >= 60)
        md << "- 显卡温度有连续 " << hotSeconds / 60 << " 分 " << hotSeconds % 60 << " 秒在 85℃ 以上，可能触发降频。\n";
    if (vramPeak > 0.0f) {
        float total = 0.0f;
        for (const RecordSample& s : samples_)
            if (s.vramTotalGB) total = std::max(total, *s.vramTotalGB);
        if (total > 0.0f && vramPeak / total >= 0.9f)
            md << "- 显存峰值 " << vramPeak << " / " << total << " GB，已接近上限，画质再高容易爆显存。\n";
    }
    md << "\n每秒明细见同目录 CSV。\n";

    if (!WriteUtf8(reportPath_, md.str(), false)) {
        message = "无法写入报告文件。";
        return false;
    }

    active_ = false;
    message = "记录已保存到 %APPDATA%\\MiniHUD\\records（CSV 与报告）。";
    return true;
}
