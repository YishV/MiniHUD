#include "config.h"
#include "util.h"
#include "sensor_hub.h"
#include <cstdio>
#include <limits>

int main() {
    int failures = 0;
    auto check = [&](bool condition, const char* name) {
        if (!condition) { std::printf("FAIL: %s\n", name); ++failures; }
    };
    const std::wstring path = AppDirectory() + L"config_check.ini";
    Config config;
    check(config.corner == Corner::TopLeft && config.singleLine, "left single-line defaults");
    check(config.offerPawnIOInstall, "fresh installs offer bundled driver setup");
    check(MetricAlert(true, Metric::Fps, 44, 45), "low FPS red");
    check(!MetricAlert(true, Metric::Fps, 45, 45), "FPS boundary normal");
    check(MetricAlert(true, Metric::CpuTemp, 90, 90), "temperature inclusive boundary");
    check(!MetricAlert(false, Metric::CpuTemp, 100, 90), "global disable");
    check(!MetricAlert(true, Metric::CpuTemp, 100, 0), "per-metric disable");
    check(!MetricAlert(true, Metric::Ram, std::numeric_limits<float>::quiet_NaN(), 90), "invalid reading");
    config.singleLine = false;
    config.englishMetrics = true;
    config.startupDefaultApplied = true;
    config.itemsPerRow = 3;
    config.moduleGap = 27.5f;
    config.rowGap = 9.0f;
    config.corner = Corner::BottomRight;
    config.alertsEnabled = false;
    config.showRam = true;
    config.offerPawnIOInstall = false;
    config.recordHotkey="Alt+F9";
    config.showLowFps=false; config.showLow01Fps=true;
    MoveMetric(config.metricOrder,static_cast<int>(Metric::GpuTemp),static_cast<int>(Metric::Fps));
    for (size_t i = 0; i < kMetricCount; ++i) config.alertLimits[i] = static_cast<float>(i) + .5f;
    check(SaveConfig(path, config), "save config");
    Config restored;
    check(LoadConfig(path, restored), "reload config");
    check(!restored.singleLine && restored.itemsPerRow == 3 && restored.corner == Corner::BottomRight,
          "layout persists without repeated migration");
    check(!restored.alertsEnabled && restored.showRam, "toggles persist");
    check(restored.metricOrder==config.metricOrder && restored.metricOrder.front()==static_cast<int>(Metric::GpuTemp),"dragged metric order persists");
    check(restored.recordHotkey=="Alt+F9","record hotkey persists");
    check(!restored.showLowFps && restored.showLow01Fps,"low FPS switches are independent");
    check((RecordingMetrics(0,true)&(kMetricCpuUsage|kMetricGpuUsage|kMetricRam))==(kMetricCpuUsage|kMetricGpuUsage|kMetricRam),"recording collects hidden report metrics");
    check(!(RecordingMetrics(0,true)&kMetricLowFps) && RecordingMetrics(0,false)==0,"low-FPS sorting remains off unless requested");
    Config badOrder; badOrder.metricOrder.fill(999); badOrder.Clamp();
    check(badOrder.metricOrder==kDefaultMetricOrder,"invalid metric order repaired");
    check(!restored.offerPawnIOInstall, "declined driver install remains declined");
    check(restored.englishMetrics, "English metric style persists");
    check(restored.startupDefaultApplied, "explicit startup preference survives restart");
    check(restored.moduleGap == 27.5f && restored.rowGap == 9.0f, "module and row spacing persist");
    for (size_t i = 0; i < kMetricCount; ++i)
        check(restored.alertLimits[i] == config.alertLimits[i], "all metric thresholds persist");
    DeleteFileW(path.c_str());
    WritePrivateProfileStringW(L"Overlay", L"Corner", L"TopRight", path.c_str());
    Config legacy;
    check(LoadConfig(path, legacy) && legacy.corner == Corner::TopLeft && legacy.singleLine, "legacy migration");
    config.itemsPerRow = 99;
    config.alertLimits[0] = std::numeric_limits<float>::quiet_NaN();
    config.alertLimits[1] = -1;
    config.Clamp();
    check(config.itemsPerRow == 8 && config.alertLimits[0] == 45 && config.alertLimits[1] == 0, "invalid config clamped");
    DeleteFileW(path.c_str());
    // Settings belong in the user profile; a pre-0.1.3 file next to the exe is imported once and removed.
    const std::wstring appDataPath = DefaultConfigPath();
    check(appDataPath.find(L"MiniHUD\\config.ini") != std::wstring::npos, "config path lives in the user profile");
    const std::wstring legacyPath = AppDirectory() + L"config.ini";
    const std::wstring migratedPath = AppDirectory() + L"config_migrated.ini";
    DeleteFileW(legacyPath.c_str());
    DeleteFileW(migratedPath.c_str());
    WritePrivateProfileStringW(L"Overlay", L"Corner", L"BottomLeft", legacyPath.c_str());
    WritePrivateProfileStringW(L"Overlay", L"LayoutVersion", L"2", legacyPath.c_str());
    Config migrated;
    check(LoadConfigOrMigrate(migratedPath, migrated) && migrated.corner == Corner::BottomLeft,
          "legacy config next to the exe is imported");
    check(GetFileAttributesW(legacyPath.c_str()) == INVALID_FILE_ATTRIBUTES, "imported legacy file is removed");
    DeleteFileW(migratedPath.c_str());
    std::printf("Config and alert checks: %s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
