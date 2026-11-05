// Synthetic frame-time checks for the shared statistics used by the HUD and the recorder.

#include "frame_stats.h"
#include "frame_histogram.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
int failures = 0;

void Check(bool condition, const char* name) {
    if (!condition) {
        std::printf("FAIL: %s\n", name);
        ++failures;
    }
}

bool Near(double value, double expected, double tolerance) { return std::abs(value - expected) <= tolerance; }
} // namespace

int main() {
    // 990 frames at 120 FPS and 10 frames at 30 FPS: the slowest 1% is exactly the slow frames.
    std::vector<float> frames(990, 1000.0f / 120.0f);
    frames.insert(frames.end(), 10, 1000.0f / 30.0f);
    const FrameStats mixed = ComputeFrameStats(frames);
    FrameHistogram histogram;
    for(float ms:frames) histogram.Add(ms);
    const auto bounded=histogram.Summary();
    Check(Near(bounded.avgFps,mixed.avgFps,.01) && Near(bounded.low1Fps,mixed.low1Fps,.1),"bounded histogram matches exact baseline");
    const auto bytes=histogram.Bytes();
    for(int i=0;i<1000000;++i) histogram.Add(8.333f);
    Check(histogram.Bytes()==bytes && bytes<200000,"million-frame recording keeps fixed memory");
    histogram.Clear(); Check(histogram.Bytes()==0,"histogram release");
    Check(mixed.frames == 1000, "frame count");
    Check(Near(mixed.avgFps, 1000.0 / ((990 * (1000.0 / 120.0) + 10 * (1000.0 / 30.0)) / 1000.0), 0.5), "average FPS");
    Check(Near(mixed.low1Fps, 30.0, 1.0), "1% low picks the slow frames");
    Check(Near(mixed.low01Fps, 30.0, 1.0), "0.1% low picks the slow frames");
    Check(Near(mixed.p50Ms, 1000.0 / 120.0, 0.05), "median frame time");
    Check(mixed.stutters == 10, "stutters above twice the median");

    // A steady 60 FPS stream has no stutters and 1% low ~= average.
    const FrameStats steady = ComputeFrameStats(std::vector<float>(600, 1000.0f / 60.0f));
    Check(steady.stutters == 0, "steady stream has no stutters");
    Check(Near(steady.low1Fps, 60.0, 0.2), "steady 1% low equals the average");
    Check(Near(steady.p50Ms, 16.67, 0.05), "steady median");

    // Too little data is reported as empty rather than as a fake low.
    const FrameStats empty = ComputeFrameStats({});
    Check(empty.frames == 0 && empty.low1Fps == 0.0f, "single frame has no statistics");

    std::printf("Frame statistics checks: %s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
