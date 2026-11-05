#pragma once

#include "config.h"
#include "overlay_window.h"
#include "sensor_hub.h"
#include "sensors/system_io.h"
#include "startup_task.h"
#include "hud_state.h"

#include <imgui.h>

#include <string>
#include <vector>

enum class UiAction { None, CloseSettings, ExitApp, InstallPawnIO, OpenPawnIODownload, EnableStartup, DisableStartup,
                      StartRecording, StopRecording, OpenRecords, ExportDiagnostics, CopyHardwareSummary };

struct SettingsState {
    bool open = false;
    char hotkeyBuf[64] = {};
    char recordHotkeyBuf[64] = {};
    bool recordHotkeyRegistered = true;
    bool pawnioInstalling = false;
    std::string pawnioMessage; // result of the last install attempt
    std::string diagMessage;   // result of the last diagnostics export
    std::string copyMessage;
    bool diskInfoLoaded = false;
    DiskReading diskInfo;
    StartupState startup;
    std::string startupMessage;
};

// Pivot of the stats panel for a corner: (0,0) = top-left ... (1,1) = bottom-right.
ImVec2 CornerPivot(Corner c);

// The always-visible stats panel, placed at `pos` (client coordinates) with `pivot`.
// Returns the panel size (zero if every metric is hidden).
ImVec2 DrawStats(const Config& cfg, const SensorSnapshot& snap, ImVec2 pos, ImVec2 pivot, HudRuntime* runtime=nullptr);

// The settings panel (only while the overlay is interactive). Edits `cfg` live.
UiAction DrawSettings(Config& cfg, const SensorSnapshot& snap, const std::vector<MonitorInfo>& monitors,
                      SettingsState& state);
