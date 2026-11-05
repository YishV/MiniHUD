#pragma once
#include <array>
#include <cstddef>
#include <cmath>
#include <algorithm>

enum class Metric : size_t { Fps, FrameTime, CpuUsage, CpuTemp, GpuUsage, GpuTemp,
    CpuPower, GpuPower, Vram, Ram, DiskTemp, DiskRead, DiskWrite, NetDown, NetUp,
    FpsLow1, FpsLow01, Count };
constexpr size_t kMetricCount = static_cast<size_t>(Metric::Count);
inline constexpr std::array<int,kMetricCount> kDefaultMetricOrder{0,15,16,1,2,3,4,5,6,7,8,9,10,11,12,13,14};
inline void MoveMetric(std::array<int,kMetricCount>& order,int source,int target) {
    auto from=std::find(order.begin(),order.end(),source), to=std::find(order.begin(),order.end(),target);
    if (from==order.end() || to==order.end() || from==to) return;
    if (from<to) std::rotate(from,from+1,to+1);
    else std::rotate(to,from,from+1);
}
inline constexpr std::array<float, kMetricCount> kDefaultLimits{
    45, 33.3f, 95, 90, 0, 85, 0, 0, 90, 90, 70, 0, 0, 0, 0, 0, 0};
inline constexpr std::array<const char*, kMetricCount> kMetricKeys{
    "Fps", "FrameTime", "CpuUsage", "CpuTemp", "GpuUsage", "GpuTemp",
    "CpuPower", "GpuPower", "Vram", "Ram", "DiskTemp", "DiskRead", "DiskWrite", "NetDown", "NetUp",
    "FpsLow1", "FpsLow01"};
inline bool MetricAlert(bool enabled, Metric metric, float value, float limit) {
    if (!enabled || !std::isfinite(value) || !std::isfinite(limit) || limit <= 0) return false;
    const bool lowerIsWorse = metric == Metric::Fps || metric == Metric::FpsLow1 || metric == Metric::FpsLow01;
    return lowerIsWorse ? value < limit : value >= limit;
}
