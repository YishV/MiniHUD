#pragma once

#include <algorithm>
#include <cmath>
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
    // 1% low = average FPS of the slowest 1% of frames (the usual reviewer definition). Unlike a
    // plain percentile it cannot land on a fast frame when fewer than 1% of frames are slow.
    auto lowAverageFps = [&](double fraction) {
        const size_t count = std::max<size_t>(1, size_t(std::ceil(fraction * double(frameMs.size()))));
        const size_t first = frameMs.size() - count;
        double sum = 0.0;
        for (size_t i = first; i < frameMs.size(); ++i) sum += frameMs[i];
        const double average = sum / double(count);
        return average > 0.0 ? float(1000.0 / average) : 0.0f;
    };
    stats.low1Fps = lowAverageFps(0.01);
    stats.low01Fps = lowAverageFps(0.001);

    const float stutterLimit = std::max(33.3f, 2.0f * stats.p50Ms);
    for (float ms : frameMs)
        if (ms > stutterLimit) ++stats.stutters;
    return stats;
}
