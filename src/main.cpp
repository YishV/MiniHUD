// FPS Monitor: a topmost, click-through overlay that shows FPS, CPU and GPU
// usage/temperature without injecting anything into games.

#include "config.h"
#include "overlay_ui.h"
#include "overlay_window.h"
#include "pawnio_setup.h"
#include "resource.h"
#include "sensor_hub.h"
#include "sensors/cpu_pawnio.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <thread>
#include <vector>

namespace {

constexpr UINT WM_APP_SENSORS = WM_APP + 1;
constexpr UINT WM_APP_TRAY = WM_APP + 2;
constexpr UINT WM_APP_OPEN_SETTINGS = WM_APP + 3;
constexpr UINT WM_APP_PAWNIO_DONE = WM_APP + 4; // wParam = success, lParam = std::string* error
constexpr UINT WM_APP_FOREGROUND = WM_APP + 5;
constexpr int kHotkeyId = 1;
constexpr UINT_PTR kTopmostTimer = 1;
constexpr UINT kTrayIconId = 1;
enum : UINT { ID_TRAY_SETTINGS = 100, ID_TRAY_EXIT, ID_TRAY_TOGGLE };

const wchar_t kTrayWindowClass[] = L"MiniHUDTrayWindow";

struct App {
    std::wstring configPath;
    Config cfg;
    Config applied; // last config pushed to the window / sensors / hotkey
    OverlayWindow overlay;
    HWND trayWnd = nullptr;
    HICON icon = nullptr;
    HWINEVENTHOOK foregroundHook = nullptr;
    UINT taskbarCreatedMsg = 0;
    SensorHub sensors;
    SensorSnapshot snapshot;
    SettingsState settings;
    std::vector<MonitorInfo> monitors;
    uint32_t metricMask = 0;         // sensors being polled (MetricBits)
    ImVec2 statsSize = ImVec2(0, 0); // stats panel size from the last frame
    HWND previousForeground = nullptr;
    bool hotkeyRegistered = false;
    bool hudVisible = true; // manual visibility; hotkey and tray work even while hidden
    bool trayIconAdded = false;
    bool dirty = true;
    HANDLE graphTimer = nullptr; // paces graph redraws while fast graph updates are on
    bool graphTimerArmed = false;
    bool running = true;
};

App* g_app = nullptr;

// The app icon (resources/app.ico) at the tray's small-icon size for the current DPI.
HICON LoadTrayIcon(HINSTANCE instance) {
    const UINT dpi = GetDpiForSystem();
    const int cx = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    const int cy = GetSystemMetricsForDpi(SM_CYSMICON, dpi);
    HICON icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, cx, cy, 0);
    return icon ? icon : CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
}

void AddTrayIcon(App& app) {
    if (app.trayIconAdded || !app.hudVisible) return;
    NOTIFYICONDATAW nid = {sizeof(nid)};
    nid.hWnd = app.trayWnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = app.icon;
    wcscpy_s(nid.szTip, L"MiniHUD");
    app.trayIconAdded=Shell_NotifyIconW(NIM_ADD, &nid)!=FALSE;
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void RemoveTrayIcon(App& app) {
    if (!app.trayIconAdded) return;
    NOTIFYICONDATAW nid = {sizeof(nid)};
    nid.hWnd = app.trayWnd;
    nid.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    app.trayIconAdded=false;
}

void RegisterHudHotkey(App& app) {
    if (app.hotkeyRegistered) UnregisterHotKey(app.trayWnd, kHotkeyId);
    app.hotkeyRegistered = false;
    UINT mods = 0, vk = 0;
    if (ParseHotkey(app.cfg.hotkey, mods, vk))
        app.hotkeyRegistered = RegisterHotKey(app.trayWnd, kHotkeyId, mods | MOD_NOREPEAT, vk) != FALSE;
    if (!app.hotkeyRegistered) {
        app.hudVisible=true;
        AddTrayIcon(app); // avoid an invisible app with no working way to summon it
    }
}

RECT MonitorRect(App& app) {
    if (app.monitors.empty()) app.monitors = EnumerateMonitors();
    if (app.monitors.empty()) return {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    int idx = app.cfg.monitor < (int)app.monitors.size() ? app.cfg.monitor : 0;
    return app.monitors[idx].rect;
}

// Where the overlay window should be. Normally it is exactly the stats panel, anchored to the
// configured corner. A window covering the whole monitor would make Windows treat it as a
// full-screen app (auto "Do not disturb") and keep games from direct-flip presentation.
RECT DesiredBounds(App& app) {
    RECT mon = MonitorRect(app);
    if (app.settings.open) {
        mon.bottom -= 1; // room for the settings panel, but deliberately not "full-screen"
        return mon;
    }
    const int w = std::max(1, (int)std::ceil(app.statsSize.x));
    const int h = std::max(1, (int)std::ceil(app.statsSize.y));
    const ImVec2 pivot = CornerPivot(app.cfg.corner);
    const int x = pivot.x == 0.0f ? mon.left + app.cfg.offsetX : mon.right - app.cfg.offsetX - w;
    const int y = pivot.y == 0.0f ? mon.top + app.cfg.offsetY : mon.bottom - app.cfg.offsetY - h;
    return {x, y, x + w, y + h};
}

void UpdateBounds(App& app) {
    RECT want = DesiredBounds(app);
    if (EqualRect(&want, &app.overlay.Bounds())) return;
    app.overlay.SetBounds(want);
    app.dirty = true; // redraw at the new size
}

// The settings window stays accessible; manual HUD visibility and game filters control the card.
bool OverlayVisible(const App& app) {
    if (app.settings.open) return true; // the settings window stays usable while the HUD is hidden
    if (!app.hudVisible) return false;
    if (!app.cfg.onlyWithGame) return true;
    if (!app.snapshot.gameTracked) return false;
    if (!app.cfg.onlyWhenFocused) return true;
    DWORD fgPid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &fgPid);
    return fgPid == app.snapshot.foregroundPid; // the FPS target, i.e. the tracked game
}

// Foreground changes wake the main loop, which re-checks OverlayVisible (ShowOnlyWhenGameFocused).
// An out-of-context hook: events arrive as messages on this thread, nothing is loaded into other apps.
void CALLBACK OnForegroundChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (g_app) PostMessageW(g_app->overlay.Hwnd(), WM_APP_FOREGROUND, 0, 0);
}

void SetForegroundHook(App& app, bool enabled) {
    if (enabled == (app.foregroundHook != nullptr)) return;
    if (enabled) {
        app.foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                                             OnForegroundChanged, 0, 0,
                                             WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    } else {
        UnhookWinEvent(app.foregroundHook);
        app.foregroundHook = nullptr;
    }
}

// The sensors to poll: only the metrics that are shown.
uint32_t MetricMask(const Config& c) {
    uint32_t m = 0;
    if (c.showFps || c.showFrameTime || c.showLowFps || c.onlyWithGame) m |= kMetricFps; // games are detected by their frames
    if (c.showCpuUsage) m |= kMetricCpuUsage;
    if (c.showCpuTemp) m |= kMetricCpuTemp;
    if (c.showCpuPower) m |= kMetricCpuPower;
    if (c.showGpuUsage) m |= kMetricGpuUsage;
    if (c.showGpuTemp) m |= kMetricGpuTemp;
    if (c.showGpuPower) m |= kMetricGpuPower;
    if (c.showVram) m |= kMetricVram;
    if (c.showRam) m |= kMetricRam;
    if (c.showDiskTemp) m |= kMetricDiskTemp;
    if (c.showDiskSpeed) m |= kMetricDiskSpeed;
    if (c.showNetworkSpeed) m |= kMetricNetworkSpeed;
    if (c.showFrameGraph) m |= kMetricFps | kMetricFrameGraph;
    if (c.showFrameGraph && c.fastFrameGraph) m |= kMetricFastGraph;
    return m;
}

// While hidden with no game tracked, only poll what is needed to notice one. Hidden because the
// game is in the background: keep polling, so the values are current when it comes back.
void UpdateMetricMask(App& app, bool force) {
    const bool visible = OverlayVisible(app);
    uint32_t mask = visible || app.snapshot.gameTracked ? MetricMask(app.cfg) : kMetricFps;
    if (!app.hudVisible && !app.settings.open) mask = 0;
    if (!visible) mask &= ~kMetricFastGraph; // nobody sees the graph

    if (!force && mask == app.metricMask) return;
    app.metricMask = mask;
    app.sensors.SetMetrics(mask);
}

// Push config changes that need more than a redraw.
void ApplyConfig(App& app, bool force) {
    const Config& c = app.cfg;
    Config& a = app.applied;
    if (force || c.hideFromCapture != a.hideFromCapture) app.overlay.SetExcludeFromCapture(c.hideFromCapture);
    if (force || c.updateIntervalMs != a.updateIntervalMs) app.sensors.SetInterval(c.updateIntervalMs);
    if (force || c.trackGame != a.trackGame) app.sensors.SetTrackGame(c.trackGame);
    SetForegroundHook(app, c.onlyWhenFocused);
    UpdateMetricMask(app, force);
    a = c;
}

void RenderFrame(App& app);

void OpenSettings(App& app) {
    if (app.settings.open) return;
    app.settings.startup=QueryStartup();
    // The user may have installed the PawnIO driver from the browser while we were running.
    if (app.snapshot.cpuNeedsDriver) app.sensors.ReinitPawnIO();
    app.previousForeground = GetForegroundWindow();
    app.monitors = EnumerateMonitors();
    std::snprintf(app.settings.hotkeyBuf, sizeof(app.settings.hotkeyBuf), "%s", app.cfg.hotkey.c_str());
    app.settings.open = true;
    UpdateBounds(app);
    RenderFrame(app); // the overlay may be hidden (ShowOnlyWithGame): show it before it takes focus
    app.overlay.SetInteractive(true);
    app.dirty = true;
}

void CloseSettings(App& app) {
    if (!app.settings.open) return;
    app.settings.open = false;
    app.overlay.SetInteractive(false);
    // Shrink the window to the stats panel now: drawn at the full-screen size, the next frame
    // would show the panel at the monitor's top-left corner until the window is resized.
    UpdateBounds(app);
    SaveConfig(app.configPath, app.cfg);
    RegisterHudHotkey(app);

    if (app.previousForeground && IsWindow(app.previousForeground)) SetForegroundWindow(app.previousForeground);
    app.previousForeground = nullptr;
    app.dirty = true;
}

// Runs the PawnIO install off the UI thread; the result arrives as WM_APP_PAWNIO_DONE.
// Bundled builds extract the embedded installer, release builds download the official one.
void StartPawnIOInstall(App& app) {
    if (app.settings.pawnioInstalling) return;
    app.settings.pawnioInstalling = true;
    app.settings.pawnioMessage.clear();
    HWND hwnd = app.overlay.Hwnd();
    const bool bundled = PawnIOSetupBundled();
    std::thread([hwnd, bundled] {
        auto* error = new std::string;
        bool ok = bundled ? RunBundledPawnIOSetup(*error) : InstallPawnIOFromWeb(*error);
        if (!PostMessageW(hwnd, WM_APP_PAWNIO_DONE, ok, reinterpret_cast<LPARAM>(error))) delete error;
    }).detach();
}

void OfferPawnIOInstall(App& app) {
    if (!app.cfg.offerPawnIOInstall || !(app.cfg.showCpuTemp || app.cfg.showCpuPower) || PawnIOReady() ||
        !CpuPawnIO::CpuSupported())
        return;

    std::string installed = InstalledPawnIOVersion();
    std::wstring text = installed.empty()
        ? L"读取 CPU 温度和功耗需要 PawnIO 驱动（来源：pawnio.eu）。\n\n"
        : L"已安装的 PawnIO 驱动版本过旧，无法读取 CPU 温度和功耗。\n\n";
    text += L"现在下载并安装吗？需要管理员权限和网络连接。\n\n选择“否”跳过，可稍后从设置中安装。";
    if (MessageBoxW(nullptr, text.c_str(), L"MiniHUD", MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND) == IDYES) {
        StartPawnIOInstall(app);
    } else {
        app.cfg.offerPawnIOInstall = false;
        app.applied.offerPawnIOInstall = false;
        SaveConfig(app.configPath, app.cfg);
    }
}

void Quit(App& app) {
    app.running = false;
    PostQuitMessage(0);
}

void ToggleHud(App& app) {
    app.hudVisible = !app.hudVisible;
    if (app.hudVisible) AddTrayIcon(app);
    else RemoveTrayIcon(app);
    app.dirty = true;
    UpdateMetricMask(app, true);
    if (!OverlayVisible(app)) app.overlay.SetVisible(false);
}

void ShowTrayMenu(App& app) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_TRAY_SETTINGS, L"设置...");
    AppendMenuW(menu, MF_STRING | (app.hotkeyRegistered ? 0 : MF_GRAYED), ID_TRAY_TOGGLE, app.hudVisible ? L"隐藏小黑条" : L"显示小黑条");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"退出");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(app.trayWnd); // required so the menu closes when clicking elsewhere
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, app.trayWnd, nullptr);
    PostMessageW(app.trayWnd, WM_NULL, 0, 0); // lets the menu close properly next time (see TrackPopupMenu docs)
    DestroyMenu(menu);
    if (cmd == ID_TRAY_SETTINGS) OpenSettings(app);
    else if (cmd == ID_TRAY_TOGGLE) ToggleHud(app);
    else if (cmd == ID_TRAY_EXIT) Quit(app);
}

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* app = g_app;
    if (app) {
        if (msg == app->taskbarCreatedMsg && msg != 0) {
            app->trayIconAdded=false;
            AddTrayIcon(*app); // Explorer restarted
            return 0;
        }
        switch (msg) {
        case WM_HOTKEY:
            if (wp == kHotkeyId) ToggleHud(*app);
            return 0;
        case WM_APP_OPEN_SETTINGS:
            OpenSettings(*app);
            return 0;
        case WM_APP_TRAY:
            switch (LOWORD(lp)) {
            case WM_LBUTTONDBLCLK:
            case NIN_SELECT:
            case NIN_KEYSELECT:
                OpenSettings(*app);
                break;
            case WM_CONTEXTMENU:
                ShowTrayMenu(*app);
                break;
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

std::optional<LRESULT> OverlayMessage(App& app, HWND, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_APP_SENSORS:
        app.snapshot = app.sensors.Snapshot();
        if (wp != kSnapshotGraphOnly) app.dirty = true; // new graph data is drawn on the graph timer

        return 0;
    case WM_TIMER:
        app.overlay.ReassertTopmost();
        return 0;
    case WM_APP_FOREGROUND:
        return 0; // only wakes the main loop, which re-checks whether the overlay is visible
    case WM_DISPLAYCHANGE:
        app.monitors = EnumerateMonitors();
        UpdateBounds(app);
        app.dirty = true;
        return 0;
    case WM_APP_PAWNIO_DONE: {
        std::unique_ptr<std::string> error(reinterpret_cast<std::string*>(lp));
        app.settings.pawnioInstalling = false;
        app.settings.pawnioMessage = wp ? "PawnIO 安装完成，正在重新连接温度传感器。" : "PawnIO 安装失败：" + *error;
        app.sensors.ReinitPawnIO();
        app.dirty = true;
        return 0;
    }
    case WM_CLOSE:
        return 0; // only closable from the tray / settings panel
    }
    return std::nullopt;
}

void RenderFrame(App& app) {
    const RECT mon = MonitorRect(app);
    app.overlay.NewFrame(ImVec2(float(mon.right - mon.left), float(mon.bottom - mon.top)));

    // Passive: the window *is* the panel, so draw it at the origin. Settings: the window
    // spans the monitor, so anchor the panel to the corner like it will appear afterwards.
    ImVec2 pos(0, 0), pivot(0, 0);
    if (app.settings.open) {
        const RECT& b = app.overlay.Bounds();
        pivot = CornerPivot(app.cfg.corner);
        pos.x = pivot.x == 0.0f ? (float)app.cfg.offsetX : float(b.right - b.left - app.cfg.offsetX);
        pos.y = pivot.y == 0.0f ? (float)app.cfg.offsetY : float(b.bottom - b.top - app.cfg.offsetY);
    }
    if (app.hudVisible) app.statsSize = DrawStats(app.cfg, app.snapshot, pos, pivot);

    UiAction action = UiAction::None;
    if (app.settings.open) action = DrawSettings(app.cfg, app.snapshot, app.monitors, app.settings);
    app.overlay.Render(app.settings.open);
    app.overlay.SetVisible(true); // only rendered while visible
    app.overlay.UpdateHitTest();

    ApplyConfig(app, false);
    UpdateBounds(app);
    if (action == UiAction::CloseSettings) CloseSettings(app);
    else if (action == UiAction::ExitApp) Quit(app);
    else if (action == UiAction::InstallPawnIO) StartPawnIOInstall(app);
    else if (action == UiAction::OpenPawnIODownload) {
        app.settings.pawnioMessage = OpenPawnIODownloadPage()
            ? "已打开 PawnIO 官方下载页。安装完成后关闭并重新打开设置即可重新检测。"
            : "无法打开浏览器，请手动访问 github.com/namazso/PawnIO.Setup/releases";
    }
    else if (action == UiAction::EnableStartup || action == UiAction::DisableStartup) {
        std::string error;
        const bool enable=action==UiAction::EnableStartup;
        const bool ok=SetStartup(enable,error);
        if (ok) {
            app.cfg.startupDefaultApplied=true;
            SaveConfig(app.configPath,app.cfg);
        }
        app.settings.startup=QueryStartup();
        app.settings.startupMessage=ok ? (enable ? "已启用当前用户登录自启动。" : "已关闭登录自启动。") : error;
    }
}

// Fast graph updates redraw the graph at a steady pace, so it scrolls smoothly. A high-resolution
// timer, because normal waits round to the 15.6 ms system tick and would make the steps uneven.
void SetGraphTimer(App& app, bool on) {
    if (on == app.graphTimerArmed || !app.graphTimer) return;
    if (on) {
        LARGE_INTEGER due;
        due.QuadPart = -10000LL * kFastGraphPeriodMs; // relative, in 100 ns units
        SetWaitableTimer(app.graphTimer, &due, kFastGraphPeriodMs, nullptr, nullptr, FALSE);
    } else {
        CancelWaitableTimer(app.graphTimer);
    }
    app.graphTimerArmed = on;
}

bool PumpMessages(App& app, bool wait) {
    MSG msg;
    if (wait) {
        if (GetMessageW(&msg, nullptr, 0, 0) <= 0) return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return app.running;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    bool autoStart=false;
    int argc=0;
    if (LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&argc)) {
        for (int i=1;i<argc;++i) if (_wcsicmp(args[i],L"--autostart")==0) autoStart=true;
        LocalFree(args);
    }
    HANDLE singleInstance = CreateMutexW(nullptr, TRUE, L"Local\\MiniHUDOverlaySingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!autoStart) if (HWND other = FindWindowW(kTrayWindowClass, nullptr)) PostMessageW(other, WM_APP_OPEN_SETTINGS, 0, 0);
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    App app;
    app.hudVisible=true; // both manual launch and login startup show the HUD
    g_app = &app;
    app.configPath = DefaultConfigPath();
    LoadConfigOrMigrate(app.configPath, app.cfg);
    if (!app.cfg.startupDefaultApplied) {
        std::string error;
        if (SetStartup(true,error)) {
            app.cfg.startupDefaultApplied=true;
            SaveConfig(app.configPath,app.cfg);
        } else app.settings.startupMessage=error;
    }
    app.applied = app.cfg;

    // Hidden window that owns the tray icon and the global hotkey.
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.lpszClassName = kTrayWindowClass;
    RegisterClassExW(&wc);
    app.trayWnd = CreateWindowExW(WS_EX_TOOLWINDOW, kTrayWindowClass, L"MiniHUD", WS_POPUP, 0, 0, 0, 0, nullptr,
                                  nullptr, instance, nullptr);
    app.taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
    app.icon = LoadTrayIcon(instance);

    if (!app.overlay.Create(instance, [&app](HWND h, UINT m, WPARAM w, LPARAM l) { return OverlayMessage(app, h, m, w, l); })) {
        MessageBoxW(nullptr, L"无法创建 Direct3D 11 叠加窗口。", L"MiniHUD", MB_ICONERROR);
        return 1;
    }
    ApplyConfig(app, true);
    UpdateBounds(app);
    AddTrayIcon(app);
    RegisterHudHotkey(app);
    if (!app.hotkeyRegistered) {
        app.hudVisible=true; // keep an accessible tray fallback if the shortcut is unavailable
        AddTrayIcon(app);
        std::wstring text = L"显示/隐藏快捷键被占用，请通过托盘菜单切换小黑条。";
        MessageBoxW(nullptr, text.c_str(), L"MiniHUD", MB_ICONWARNING);
    }
    OfferPawnIOInstall(app);
    SetTimer(app.overlay.Hwnd(), kTopmostTimer, 1000, nullptr);
    app.sensors.Start(app.overlay.Hwnd(), WM_APP_SENSORS, app.cfg.updateIntervalMs);

    app.graphTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!app.graphTimer) app.graphTimer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS); // before Windows 10 1803

    bool graphFrameDue = false;
    while (app.running) {
        if (app.settings.open) {
            // Interactive: render continuously (vsync-paced) for responsive UI.
            if (!PumpMessages(app, false)) break;
            RenderFrame(app);
        } else {
            // Passive: draw what changed, then sleep until new sensor data (or another message).
            UpdateMetricMask(app, false);
            if (!OverlayVisible(app)) {
                app.overlay.SetVisible(false);
            } else if (app.dirty || graphFrameDue || !app.overlay.Visible()) {
                // At least two frames: ImGui settles an auto-resized panel one frame late. A graph
                // frame keeps the panel size, so one is enough (unless that frame resized it).
                // After being hidden, the last presented frame is stale.
                const int minFrames = app.dirty || !app.overlay.Visible() ? 2 : 1;
                for (int i = 0; i < 6; ++i) {
                    app.dirty = false;
                    RenderFrame(app);
                    if (i + 1 >= minFrames && !app.dirty) break;
                }
            }
            app.dirty = false;
            SetGraphTimer(app, OverlayVisible(app) && app.cfg.showFrameGraph && app.cfg.fastFrameGraph);

            // Sleep until a message arrives or the next graph frame is due.
            graphFrameDue = false;
            if (app.graphTimerArmed) {
                graphFrameDue = MsgWaitForMultipleObjects(1, &app.graphTimer, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0;
                if (!PumpMessages(app, false)) break;
            } else if (!PumpMessages(app, true)) {
                break;
            }
        }
    }

    app.sensors.Stop();
    SetForegroundHook(app, false);
    KillTimer(app.overlay.Hwnd(), kTopmostTimer);
    SetGraphTimer(app, false);
    if (app.graphTimer) CloseHandle(app.graphTimer);
    if (app.hotkeyRegistered) UnregisterHotKey(app.trayWnd, kHotkeyId);
    RemoveTrayIcon(app);
    app.overlay.Destroy();
    DestroyWindow(app.trayWnd);
    DestroyIcon(app.icon);
    g_app = nullptr;
    if (singleInstance) CloseHandle(singleInstance);
    return 0;
}
