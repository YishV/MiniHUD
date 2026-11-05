#pragma once

#include <windows.h>

#include <string>
#include "metric_rules.h"

enum class Corner { TopLeft = 0, TopRight, BottomLeft, BottomRight };

// Value ranges enforced by Config::Clamp, which are also the ranges of the settings panel controls.
constexpr int kMaxOffset = 4000;
constexpr float kMinFontScale = 0.5f;
constexpr float kMaxFontScale = 4.0f;
constexpr int kMinUpdateIntervalMs = 100;
constexpr int kMaxUpdateIntervalMs = 5000;

struct Config {
    // [Overlay]
    Corner corner = Corner::TopLeft;
    bool singleLine = true;
    bool englishMetrics = false;
    bool startupDefaultApplied = false; // enable once, then respect the user's startup switch
    int itemsPerRow = 4;
    float moduleGap = 16.0f;
    float rowGap = 4.0f;
    bool alertsEnabled = true;
    std::array<float, kMetricCount> alertLimits = kDefaultLimits;
    int offsetX = 0;
    int offsetY = 0;
    int monitor = 0;              // 0 = primary, then the remaining monitors in enumeration order
    float fontScale = 1.0f;
    float backgroundAlpha = 0.92f;
    int updateIntervalMs = 500;
    std::string hotkey = "Ctrl+Shift+F12";
    std::string recordHotkey = "Ctrl+Shift+F11";
    std::array<int,kMetricCount> metricOrder = kDefaultMetricOrder;
    bool hideFromCapture = false;

    // [Metrics]
    bool showFps = true;
    bool showFrameTime = false;
    bool showLowFps = false; // 1% low FPS
    bool showLow01Fps = false;
    bool showCpuUsage = false;
    bool showCpuTemp = true;
    bool showGpuUsage = false;
    bool showGpuTemp = true;
    bool showCpuPower = false;
    bool showGpuPower = false;
    bool showVram = true;
    bool showRam = false;
    bool showDiskTemp = false;
    bool showDiskSpeed = false;
    bool showNetworkSpeed = false;
    bool showFrameGraph = false;
    bool fastFrameGraph = false; // redraw the graph ~30 times a second instead of every update interval
    bool fahrenheit = false;
    bool trackGame = false; // keep showing a game's FPS while another app is focused
    bool onlyWithGame = false; // hide the overlay while no game is tracked (needs trackGame)
    bool onlyWhenFocused = false; // ...and while the tracked game is not in the foreground (needs onlyWithGame)

    // [Sensors]
    bool offerPawnIOInstall = true; // offer once when needed; installation still requires confirmation

    void Clamp();
};

// %APPDATA%\MiniHUD\config.ini (the app folder if the known folder is unavailable).
std::wstring DefaultConfigPath();

// Returns false if the file did not exist (cfg keeps defaults for missing keys either way).
bool LoadConfig(const std::wstring& path, Config& cfg);
bool SaveConfig(const std::wstring& path, const Config& cfg);

// Loads `path`. When it is missing, imports the pre-0.1.3 config.ini from the app folder and
// removes that file. Always leaves a config on disk; true if settings came from an existing file.
bool LoadConfigOrMigrate(const std::wstring& path, Config& cfg);

const char* CornerToString(Corner c);

// "Ctrl+Shift+F12" -> MOD_CONTROL|MOD_SHIFT, VK_F12. Returns false on unknown syntax.
bool ParseHotkey(const std::string& text, UINT& mods, UINT& vk);
