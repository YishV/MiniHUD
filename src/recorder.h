#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// One per-second row of a performance recording.
struct RecordSample {
    double elapsed = 0.0;
    std::optional<float> fps, frameTimeMs, low1, low01;
    std::optional<float> cpuUsage, cpuTemp, cpuPowerW;
    std::optional<float> gpuUsage, gpuTemp, gpuPowerW;
    std::optional<float> vramUsedGB, vramTotalGB, ramUsedGB;
};

// Collects frame times and per-second samples while the user records, then writes a CSV plus a
// human-readable Markdown summary. Every method except Seconds()/Samples()/Active() is called on
// the sensor thread only.
class PerformanceRecorder {
public:
    void Start();
    void Stop();
    void Clear();

    bool Active() const { return active_; }
    double Seconds() const;
    int Samples() const { return static_cast<int>(samples_.size()); }

    // `framesMs` is a sliding window (oldest first), so only frames newer than the last accepted
    // one are kept. `lastFrameQpc` is the QPC time at which the newest one ended.
    void AddFrameTimes(const std::vector<float>& framesMs, int64_t lastFrameQpc);
    void AddSample(const RecordSample& sample);

    // Writes <MiniHUD-yyyy-mm-dd_hh-mm-ss>.csv and .md under %APPDATA%\MiniHUD\records.
    // `message` is a Chinese status line for the settings panel.
    bool Save(std::string& message);

private:
    bool active_ = false;
    int64_t startQpc_ = 0;
    int64_t acceptedEndQpc_ = 0;
    std::vector<float> frames_;
    std::vector<RecordSample> samples_;
    std::wstring csvPath_;
    std::wstring reportPath_;
};
