#pragma once
#include <array>
#include <cstddef>
#include <cmath>

enum class Metric : size_t { Fps, FrameTime, CpuUsage, CpuTemp, GpuUsage, GpuTemp,
    CpuPower, GpuPower, Vram, Ram, DiskTemp, DiskRead, DiskWrite, NetDown, NetUp, Count };
constexpr size_t kMetricCount = static_cast<size_t>(Metric::Count);
inline constexpr std::array<float, kMetricCount> kDefaultLimits{
    45, 33.3f, 95, 90, 0, 85, 0, 0, 90, 90, 70, 0, 0, 0, 0};
inline constexpr std::array<const char*, kMetricCount> kMetricKeys{
    "Fps", "FrameTime", "CpuUsage", "CpuTemp", "GpuUsage", "GpuTemp",
    "CpuPower", "GpuPower", "Vram", "Ram", "DiskTemp", "DiskRead", "DiskWrite", "NetDown", "NetUp"};
inline bool MetricAlert(bool enabled, Metric metric, float value, float limit) {
    if (!enabled || !std::isfinite(value) || !std::isfinite(limit) || limit <= 0) return false;
    return metric == Metric::Fps ? value < limit : value >= limit;
}
