#include "config.h"

#include "util.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <shlobj.h>
#include <sstream>
#include <iomanip>
#include <vector>

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string ReadString(const std::wstring& path, const wchar_t* section, const wchar_t* key, const std::string& def) {
    wchar_t buf[256];
    DWORD n = GetPrivateProfileStringW(section, key, L"\x01", buf, (DWORD)std::size(buf), path.c_str());
    if (n == 1 && buf[0] == L'\x01') return def;
    std::string out;
    for (DWORD i = 0; i < n; ++i) out.push_back(buf[i] < 128 ? (char)buf[i] : '?');
    // Strip inline comments ("Corner=TopRight ; comment").
    size_t semi = out.find(';');
    if (semi != std::string::npos) out.resize(semi);
    return Trim(out);
}

int ReadInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, int def) {
    std::string s = ReadString(path, section, key, "");
    if (s.empty()) return def;
    try { return std::stoi(s); } catch (...) { return def; }
}

float ReadFloat(const std::wstring& path, const wchar_t* section, const wchar_t* key, float def) {
    std::string s = ReadString(path, section, key, "");
    if (s.empty()) return def;
    try { return std::stof(s); } catch (...) { return def; }
}

bool ReadBool(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool def) {
    std::string s = ToLower(ReadString(path, section, key, ""));
    if (s.empty()) return def;
    return s == "1" || s == "true" || s == "yes" || s == "on";
}

Corner ParseCorner(const std::string& s, Corner def) {
    std::string l = ToLower(s);
    if (l == "topleft") return Corner::TopLeft;
    if (l == "topright") return Corner::TopRight;
    if (l == "bottomleft") return Corner::BottomLeft;
    if (l == "bottomright") return Corner::BottomRight;
    if (l == "topcenter") return Corner::TopCenter;
    return def;
}

} // namespace

void Config::Clamp() {
    alertTriggerSeconds=std::isfinite(alertTriggerSeconds)?std::clamp(alertTriggerSeconds,0.0f,10.0f):3.0f;
    alertRecoverySeconds=std::isfinite(alertRecoverySeconds)?std::clamp(alertRecoverySeconds,0.0f,10.0f):3.0f;
    for (auto& color:labelColors) color&=0xFFFFFFu;
    std::array<bool,kMetricCount> seen{};
    std::array<int,kMetricCount> normalized{};
    size_t next=0;
    for (int id:metricOrder) if (id>=0 && id<static_cast<int>(kMetricCount) && !seen[id]) { seen[id]=true; normalized[next++]=id; }
    for (int id:kDefaultMetricOrder) if (!seen[id]) normalized[next++]=id;
    metricOrder=normalized;
    itemsPerRow = std::clamp(itemsPerRow, 1, 8);
    moduleGap = std::isfinite(moduleGap) ? std::clamp(moduleGap, 0.0f, 80.0f) : 16.0f;
    rowGap = std::isfinite(rowGap) ? std::clamp(rowGap, 0.0f, 40.0f) : 4.0f;
    for (size_t i = 0; i < alertLimits.size(); ++i)
        alertLimits[i] = std::isfinite(alertLimits[i]) ? std::clamp(alertLimits[i], 0.0f, 1000000.0f) : kDefaultLimits[i];
    offsetX = std::clamp(offsetX, 0, kMaxOffset);
    offsetY = std::clamp(offsetY, 0, kMaxOffset);
    monitor = std::max(monitor, 0);
    fontScale = std::clamp(fontScale, kMinFontScale, kMaxFontScale);
    backgroundAlpha = std::clamp(backgroundAlpha, 0.0f, 1.0f);
    updateIntervalMs = std::clamp(updateIntervalMs, kMinUpdateIntervalMs, kMaxUpdateIntervalMs);
    onlyWithGame = onlyWithGame && trackGame;
    onlyWhenFocused = onlyWhenFocused && onlyWithGame;
}

// %APPDATA%\MiniHUD\, created on demand; empty if the known folder is unavailable.
std::wstring ConfigDirectory() {
    PWSTR roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &roaming)) || !roaming) return {};
    std::wstring dir(roaming);
    CoTaskMemFree(roaming);
    if (dir.empty()) return {};
    if (dir.back() != L'\\') dir += L'\\';
    dir += L"MiniHUD";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L'\\';
}

std::wstring DefaultConfigPath() {
    const std::wstring dir = ConfigDirectory();
    // Settings live in the user profile, so an installed copy in Program Files works and the
    // download folder stays clean. Fall back to the app folder if the known folder is missing.
    return dir.empty() ? AppDirectory() + L"config.ini" : dir + L"config.ini";
}

bool LoadConfigOrMigrate(const std::wstring& path, Config& cfg) {
    if (LoadConfig(path, cfg)) return true;
    const std::wstring legacy = AppDirectory() + L"config.ini";
    if (CompareStringOrdinal(path.c_str(), -1, legacy.c_str(), -1, TRUE) != CSTR_EQUAL && LoadConfig(legacy, cfg)) {
        SaveConfig(path, cfg);
        DeleteFileW(legacy.c_str());
        return true;
    }
    SaveConfig(path, cfg);
    return false;
}

const char* CornerToString(Corner c) {
    switch (c) {
    case Corner::TopLeft: return "TopLeft";
    case Corner::TopRight: return "TopRight";
    case Corner::BottomLeft: return "BottomLeft";
    case Corner::BottomRight: return "BottomRight";
    case Corner::TopCenter: return "TopCenter";
    }
    return "TopRight";
}

const char* ThemeToString(ThemeMode mode) {
    switch (mode) {
    case ThemeMode::Dark: return "Dark";
    case ThemeMode::Light: return "Light";
    case ThemeMode::System: return "System";
    }
    return "System";
}

ThemeMode ParseTheme(const std::string& text, ThemeMode fallback) {
    const std::string lower = ToLower(text);
    if (lower == "dark") return ThemeMode::Dark;
    if (lower == "light") return ThemeMode::Light;
    if (lower == "system") return ThemeMode::System;
    return fallback;
}

bool SystemUsesLightTheme() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                      KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    DWORD value = 0, size = sizeof(value);
    const bool ok =
        RegGetValueW(key, nullptr, L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok && value != 0;
}

bool EffectiveLightTheme(ThemeMode mode) {
    return mode == ThemeMode::Light || (mode == ThemeMode::System && SystemUsesLightTheme());
}

bool LoadConfig(const std::wstring& path, Config& cfg) {
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    const wchar_t* ov = L"Overlay";
    cfg.corner = ParseCorner(ReadString(path, ov, L"Corner", ""), cfg.corner);
    cfg.offsetX = ReadInt(path, ov, L"OffsetX", cfg.offsetX);
    cfg.offsetY = ReadInt(path, ov, L"OffsetY", cfg.offsetY);
    cfg.monitor = ReadInt(path, ov, L"Monitor", cfg.monitor);
    cfg.fontScale = ReadFloat(path, ov, L"FontScale", cfg.fontScale);
    cfg.backgroundAlpha = ReadFloat(path, ov, L"BackgroundAlpha", cfg.backgroundAlpha);
    cfg.fixedValueWidth = ReadBool(path, ov, L"FixedValueWidth", cfg.fixedValueWidth);
    // Theme= supersedes the old LightTheme flag; configs without either follow Windows.
    const std::string theme = ReadString(path, ov, L"Theme", "");
    if (!theme.empty()) {
        cfg.themeMode = ParseTheme(theme, ThemeMode::System);
    } else {
        const int legacyLight = ReadInt(path, ov, L"LightTheme", -1);
        cfg.themeMode = legacyLight < 0 ? ThemeMode::System
                                        : (legacyLight ? ThemeMode::Light : ThemeMode::Dark);
    }
    cfg.lightTheme = EffectiveLightTheme(cfg.themeMode);
    cfg.updateIntervalMs = ReadInt(path, ov, L"UpdateIntervalMs", cfg.updateIntervalMs);
    cfg.hotkey = ReadString(path, ov, L"Hotkey", cfg.hotkey);
    cfg.recordHotkey = ReadString(path,ov,L"RecordHotkey",cfg.recordHotkey);
    const auto order=ReadString(path,ov,L"MetricOrder","");
    if (!order.empty()) {
        cfg.metricOrder.fill(-1); std::stringstream input(order); std::string token; size_t i=0;
        while (i<kMetricCount && std::getline(input,token,',')) { try { cfg.metricOrder[i]=std::stoi(token); } catch (...) {} ++i; }
    }
    cfg.hideFromCapture = ReadBool(path, ov, L"HideFromCapture", cfg.hideFromCapture);
    cfg.singleLine = ReadBool(path, ov, L"SingleLine", cfg.singleLine);
    cfg.englishMetrics = ReadBool(path, ov, L"EnglishMetrics", cfg.englishMetrics);
    cfg.startupDefaultApplied = ReadBool(path, ov, L"StartupDefaultApplied", cfg.startupDefaultApplied);
    cfg.itemsPerRow = ReadInt(path, ov, L"ItemsPerRow", cfg.itemsPerRow);
    cfg.moduleGap = ReadFloat(path, ov, L"ModuleGap", cfg.moduleGap);
    cfg.rowGap = ReadFloat(path, ov, L"RowGap", cfg.rowGap);
    if (ReadInt(path, ov, L"LayoutVersion", 0) < 2) {
        cfg.corner = Corner::TopLeft;
        cfg.singleLine = true;
    }
    cfg.alertsEnabled = ReadBool(path, L"Alerts", L"Enabled", cfg.alertsEnabled);
    cfg.alertTriggerSeconds=ReadFloat(path,L"Alerts",L"TriggerSeconds",cfg.alertTriggerSeconds);
    cfg.alertRecoverySeconds=ReadFloat(path,L"Alerts",L"RecoverySeconds",cfg.alertRecoverySeconds);
    for (size_t i = 0; i < kMetricCount; ++i) {
        const std::wstring key(kMetricKeys[i], kMetricKeys[i] + std::strlen(kMetricKeys[i]));
        cfg.alertLimits[i] = ReadFloat(path, L"Alerts", key.c_str(), cfg.alertLimits[i]);
        const auto color=ReadString(path,L"LabelColors",key.c_str(),"");
        if (color.size()==6 && std::all_of(color.begin(),color.end(),[](unsigned char ch){return std::isxdigit(ch)!=0;}))
            cfg.labelColors[i]=static_cast<uint32_t>(std::stoul(color,nullptr,16));
    }

    const wchar_t* me = L"Metrics";
    cfg.showFps = ReadBool(path, me, L"ShowFps", cfg.showFps);
    cfg.showFrameTime = ReadBool(path, me, L"ShowFrameTime", cfg.showFrameTime);
    cfg.showLowFps = ReadBool(path, me, L"ShowLowFps", cfg.showLowFps);
    cfg.showLow01Fps = ReadBool(path, me, L"ShowLow01Fps", cfg.showLowFps); // migrate the old combined switch
    cfg.showTime = ReadBool(path, me, L"ShowTime", cfg.showTime);
    cfg.showCpuUsage = ReadBool(path, me, L"ShowCpuUsage", cfg.showCpuUsage);
    cfg.showCpuTemp = ReadBool(path, me, L"ShowCpuTemp", cfg.showCpuTemp);
    cfg.showGpuUsage = ReadBool(path, me, L"ShowGpuUsage", cfg.showGpuUsage);
    cfg.showGpuTemp = ReadBool(path, me, L"ShowGpuTemp", cfg.showGpuTemp);
    cfg.showCpuPower = ReadBool(path, me, L"ShowCpuPower", cfg.showCpuPower);
    cfg.showGpuPower = ReadBool(path, me, L"ShowGpuPower", cfg.showGpuPower);
    cfg.showVram = ReadBool(path, me, L"ShowVram", cfg.showVram);
    cfg.showRam = ReadBool(path, me, L"ShowRam", cfg.showRam);
    cfg.showDiskTemp = ReadBool(path, me, L"ShowDiskTemp", cfg.showDiskTemp);
    cfg.showDiskSpeed = ReadBool(path, me, L"ShowDiskSpeed", cfg.showDiskSpeed);
    cfg.showNetworkSpeed = ReadBool(path, me, L"ShowNetworkSpeed", cfg.showNetworkSpeed);
    cfg.showFrameGraph = ReadBool(path, me, L"ShowFrameTimeGraph", cfg.showFrameGraph);
    cfg.fastFrameGraph = ReadBool(path, me, L"FastFrameTimeGraph", cfg.fastFrameGraph);
    cfg.fahrenheit = ToLower(ReadString(path, me, L"TemperatureUnit", cfg.fahrenheit ? "F" : "C")) == "f";
    cfg.trackGame = ReadBool(path, me, L"TrackGame", cfg.trackGame);
    cfg.onlyWithGame = ReadBool(path, me, L"ShowOnlyWithGame", cfg.onlyWithGame);
    cfg.onlyWhenFocused = ReadBool(path, me, L"ShowOnlyWhenGameFocused", cfg.onlyWhenFocused);

    cfg.offerPawnIOInstall = ReadBool(path, L"Sensors", L"OfferPawnIOInstall", cfg.offerPawnIOInstall);

    cfg.Clamp();
    return true;
}

bool SaveConfig(const std::wstring& path, const Config& cfg) {
    char buf[4096];
    int len = std::snprintf(buf, sizeof(buf),
        "; MiniHUD configuration. Open settings from the tray icon; hotkey toggles the HUD.\r\n"
        "\r\n"
        "[Overlay]\r\n"
        "; TopLeft | TopRight | BottomLeft | BottomRight | TopCenter\r\n"
        "Corner=%s\r\n"
        "OffsetX=%d\r\n"
        "OffsetY=%d\r\n"
        "; 0 = primary monitor, 1.. = other monitors\r\n"
        "Monitor=%d\r\n"
        "FontScale=%.2f\r\n"
        "BackgroundAlpha=%.2f\r\n"
        "; 1 = reserve a fixed width per value (stable, but wider gaps)\r\n"
        "FixedValueWidth=%d\r\n"
        "; Dark | Light | System (follow the Windows app theme)\r\n"
        "Theme=%s\r\n"
        "UpdateIntervalMs=%d\r\n"
        "; Modifiers: Ctrl, Shift, Alt, Win. Keys: F1-F24, A-Z, 0-9, Insert, Delete, Home, End, PageUp, PageDown, Pause, ScrollLock\r\n"
        "Hotkey=%s\r\n"
        "; 1 = hide the overlay from screenshots / recordings / streams\r\n"
        "HideFromCapture=%d\r\n"
        "\r\n"
        "[Metrics]\r\n"
        "ShowFps=%d\r\n"
        "ShowFrameTime=%d\r\n"
        "; 1 = low percentile FPS (slowest 1 and 0.1 percent of frames, last 60 s)\r\n"
        "ShowLowFps=%d\r\n"
        "ShowLow01Fps=%d\r\n"
        "; 1 = clock in HH:MM\r\n"
        "ShowTime=%d\r\n"
        "ShowCpuUsage=%d\r\n"
        "ShowCpuTemp=%d\r\n"
        "ShowGpuUsage=%d\r\n"
        "ShowGpuTemp=%d\r\n"
        "ShowCpuPower=%d\r\n"
        "ShowGpuPower=%d\r\n"
        "ShowVram=%d\r\n"
        "ShowRam=%d\r\n"
        "ShowDiskTemp=%d\r\n"
        "ShowDiskSpeed=%d\r\n"
        "ShowNetworkSpeed=%d\r\n"
        "ShowFrameTimeGraph=%d\r\n"
        "; 1 = redraw the frame time graph about 30 times a second, scrolling smoothly (other values keep UpdateIntervalMs)\r\n"
        "FastFrameTimeGraph=%d\r\n"
        "; C | F\r\n"
        "TemperatureUnit=%s\r\n"
        "; 1 = keep showing a full-screen game's FPS while another app is focused\r\n"
        "TrackGame=%d\r\n"
        "; 1 = only show the overlay while a full-screen game is detected (needs TrackGame=1)\r\n"
        "ShowOnlyWithGame=%d\r\n"
        "; 1 = ...and only while that game is focused (needs ShowOnlyWithGame=1)\r\n"
        "ShowOnlyWhenGameFocused=%d\r\n"
        "\r\n"
        "[Sensors]\r\n"
        "; 1 = offer to download and install the PawnIO driver (needed for CPU temperature and power) on startup\r\n"
        "OfferPawnIOInstall=%d\r\n",
        CornerToString(cfg.corner), cfg.offsetX, cfg.offsetY, cfg.monitor, cfg.fontScale, cfg.backgroundAlpha,
        cfg.fixedValueWidth ? 1 : 0, ThemeToString(cfg.themeMode),
        cfg.updateIntervalMs, cfg.hotkey.c_str(), cfg.hideFromCapture ? 1 : 0,
        cfg.showFps ? 1 : 0, cfg.showFrameTime ? 1 : 0, cfg.showLowFps ? 1 : 0, cfg.showLow01Fps ? 1 : 0,
        cfg.showTime ? 1 : 0,
        cfg.showCpuUsage ? 1 : 0, cfg.showCpuTemp ? 1 : 0,
        cfg.showGpuUsage ? 1 : 0, cfg.showGpuTemp ? 1 : 0, cfg.showCpuPower ? 1 : 0, cfg.showGpuPower ? 1 : 0,
        cfg.showVram ? 1 : 0, cfg.showRam ? 1 : 0,
        cfg.showDiskTemp ? 1 : 0, cfg.showDiskSpeed ? 1 : 0, cfg.showNetworkSpeed ? 1 : 0,
        cfg.showFrameGraph ? 1 : 0, cfg.fastFrameGraph ? 1 : 0,
        cfg.fahrenheit ? "F" : "C", cfg.trackGame ? 1 : 0,
        cfg.onlyWithGame ? 1 : 0, cfg.onlyWhenFocused ? 1 : 0, cfg.offerPawnIOInstall ? 1 : 0);
    if (len <= 0 || len >= (int)sizeof(buf)) return false;
    std::ostringstream extra;
    extra << "\r\n[Alerts]\r\nEnabled=" << cfg.alertsEnabled << "\r\n";
    for (size_t i = 0; i < kMetricCount; ++i) extra << kMetricKeys[i] << '=' << cfg.alertLimits[i] << "\r\n";
    extra<<"TriggerSeconds="<<cfg.alertTriggerSeconds<<"\r\nRecoverySeconds="<<cfg.alertRecoverySeconds<<"\r\n\r\n[LabelColors]\r\n";
    for (size_t i=0;i<kMetricCount;++i) extra<<kMetricKeys[i]<<'='<<std::hex<<std::setw(6)<<std::setfill('0')<<cfg.labelColors[i]<<"\r\n";
    std::string output(buf, len);
    output.insert(output.find("[Metrics]"), "LayoutVersion=2\r\nSingleLine=" + std::to_string(cfg.singleLine) +
                  "\r\nItemsPerRow=" + std::to_string(cfg.itemsPerRow) +
                  "\r\nModuleGap=" + std::to_string(cfg.moduleGap) +
                  "\r\nRowGap=" + std::to_string(cfg.rowGap) +
                  "\r\nEnglishMetrics=" + std::to_string(cfg.englishMetrics) + "\r\n\r\n");
    output.insert(output.find("[Metrics]"), "StartupDefaultApplied=" + std::to_string(cfg.startupDefaultApplied) + "\r\n\r\n");
    std::ostringstream layout;
    layout<<"RecordHotkey="<<cfg.recordHotkey<<"\r\nMetricOrder=";
    for (size_t i=0;i<kMetricCount;++i) { if (i) layout<<','; layout<<cfg.metricOrder[i]; }
    layout<<"\r\n\r\n";
    output.insert(output.find("[Metrics]"),layout.str());
    output.insert(output.find("ShowLowFps="),"ShowLow01Fps="+std::to_string(cfg.showLow01Fps)+"\r\n");
    output += extra.str();

    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(f, output.data(), (DWORD)output.size(), &written, nullptr);
    CloseHandle(f);
    return ok && written == (DWORD)output.size();
}

bool ParseHotkey(const std::string& text, UINT& mods, UINT& vk) {
    mods = 0;
    vk = 0;
    std::stringstream ss(text);
    std::string tok;
    std::vector<std::string> parts;
    while (std::getline(ss, tok, '+')) {
        tok = ToLower(Trim(tok));
        if (!tok.empty()) parts.push_back(tok);
    }
    if (parts.empty()) return false;

    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::string& m = parts[i];
        if (m == "ctrl" || m == "control") mods |= MOD_CONTROL;
        else if (m == "shift") mods |= MOD_SHIFT;
        else if (m == "alt") mods |= MOD_ALT;
        else if (m == "win") mods |= MOD_WIN;
        else return false;
    }

    const std::string& k = parts.back();
    if (k.size() == 1 && std::isalnum((unsigned char)k[0])) {
        vk = (UINT)std::toupper((unsigned char)k[0]);
    } else if (k.size() >= 2 && k[0] == 'f' &&
               std::all_of(k.begin() + 1, k.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        int n = std::atoi(k.c_str() + 1);
        if (n < 1 || n > 24) return false;
        vk = VK_F1 + (n - 1);
    } else {
        static const struct { const char* name; UINT vk; } named[] = {
            {"insert", VK_INSERT}, {"delete", VK_DELETE}, {"home", VK_HOME}, {"end", VK_END},
            {"pageup", VK_PRIOR}, {"pagedown", VK_NEXT}, {"pause", VK_PAUSE}, {"scrolllock", VK_SCROLL},
        };
        for (const auto& n : named)
            if (k == n.name) vk = n.vk;
        if (!vk) return false;
    }
    return true;
}
