#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

// Frame-time statistics shared by the live HUD and the performance recorder.
struct FrameStats {
    size_t frames = 0;
    float avgFps = 0.0f;
    float low1Fps = 0.0f;  // FPS at the slowest 1% of frames
    float low01Fps = 0.0f; // FPS at the slowest 0.1% of frames
    float p50Ms = 0.0f;
    float p95Ms = 0.0f;
    float p99Ms = 0.0f;
    size_t stutters = 0; // frames slower than max(33.3 ms, 2 x median)
};

// Percentiles over `frameMs`. Takes a copy, so the caller keeps its own order.
inline FrameStats ComputeFrameStats(std::vector<float> frameMs) {
    FrameStats stats;
    stats.frames = frameMs.size();
    if (frameMs.size() < 2) return stats;
    std::sort(frameMs.begin(), frameMs.end());

    auto at = [&](double p) {
        const size_t index = std::min(frameMs.size() - 1, size_t(p * double(frameMs.size())));
        return frameMs[index];
    };

    double sum = 0.0;
    for (float ms : frameMs) sum += ms;
    const double meanMs = sum / double(frameMs.size());

    stats.avgFps = meanMs > 0.0 ? float(1000.0 / meanMs) : 0.0f;
    stats.p50Ms = at(0.50);
    stats.p95Ms = at(0.95);
    stats.p99Ms = at(0.99);
    stats.low1Fps = stats.p99Ms > 0.0f ? 1000.0f / stats.p99Ms : 0.0f;
    const float p999 = at(0.999);
    stats.low01Fps = p999 > 0.0f ? 1000.0f / p999 : 0.0f;

    const float stutterLimit = std::max(33.3f, 2.0f * stats.p50Ms);
    for (float ms : frameMs)
        if (ms > stutterLimit) ++stats.stutters;
    return stats;
}
