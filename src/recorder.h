#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "frame_histogram.h"

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
    explicit PerformanceRecorder(std::wstring directory = {}) : directory_(std::move(directory)) {}
    ~PerformanceRecorder();
    bool Start();
    void Stop();
    void Clear();

    bool Active() const { return active_; }
    double Seconds() const;
    int Samples() const { return samples_; }
    size_t BufferedBytes() const { return frames_.Bytes(); }
    const std::string& Error() const { return error_; }
    const std::wstring& CsvPath() const { return csvPath_; }
    const std::wstring& ReportPath() const { return reportPath_; }

    // `framesMs` is a sliding window (oldest first), so only frames newer than the last accepted
    // one are kept. `lastFrameQpc` is the QPC time at which the newest one ended.
    void AddFrameTimes(const std::vector<float>& framesMs, int64_t lastFrameQpc);
    void AddSample(const RecordSample& sample);

    // Writes <MiniHUD-yyyy-mm-dd_hh-mm-ss>.csv and .md under %APPDATA%\MiniHUD\records.
    // `message` is a Chinese status line for the settings panel.
    bool Save(std::string& message);

private:
    bool active_ = false;
    bool saved_ = false;
    int64_t startQpc_ = 0;
    int64_t acceptedEndQpc_ = 0;
    FrameHistogram frames_;
    struct Aggregate {
        double sum=0; int count=0; float peak=0;
        void Add(const std::optional<float>& value) {
            if (!value || !std::isfinite(*value)) return;
            sum+=*value; peak=count ? std::max(peak,*value) : *value; ++count;
        }
    };
    Aggregate fps_,cpuUsage_,cpuTemp_,gpuUsage_,gpuTemp_,vramUsed_,vramTotal_;
    int samples_=0, hotRun_=0, longestHotRun_=0;
    double lastElapsed_=-1;
    int64_t stoppedQpc_=0;
    HANDLE csv_=INVALID_HANDLE_VALUE;
    std::wstring directory_;
    std::string error_;
    std::wstring csvPath_;
    std::wstring reportPath_;
};
