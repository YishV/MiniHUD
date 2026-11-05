#pragma once

#include <string>

// Helpers around the official PawnIO installer. Release builds do not embed it: the installer is
// downloaded from the pinned official URL, checked against a pinned SHA-256, then run elevated.

// DisplayVersion from the PawnIO uninstall registry key, or empty if not installed.
std::string InstalledPawnIOVersion();

// Installed and recent enough (2.x) for the bundled modules.
bool PawnIOReady();

// True if this build embeds PawnIO_setup.exe (FPS_MONITOR_BUNDLE_PAWNIO=ON).
bool PawnIOSetupBundled();

// Extracts the bundled installer into %TEMP%, runs "-install -silent" (elevating if needed),
// waits for it and deletes it. Blocks for a few seconds; call off the UI thread if possible.
bool RunBundledPawnIOSetup(std::string& error);

// Downloads the official installer to `path` and verifies its SHA-256. No elevation, no execution.
bool DownloadPawnIOInstaller(const std::wstring& path, std::string& error);

// Downloads the official installer, verifies it and runs "-install -silent" elevated.
bool InstallPawnIOFromWeb(std::string& error);

// Official download page, opened in the browser when the user prefers to install by hand.
constexpr const wchar_t* kPawnIODownloadPageUrl =
    L"https://github.com/namazso/PawnIO.Setup/releases/tag/2.2.0";

// The exact installer build this version knows how to verify.
constexpr const wchar_t* kPawnIOInstallerUrl =
    L"https://github.com/namazso/PawnIO.Setup/releases/download/2.2.0/PawnIO_setup.exe";
constexpr const char* kPawnIOInstallerSha256 =
    "1f519a22e47187f70a1379a48ca604981c4fcf694f4e65b734aaa74a9fba3032";

bool OpenPawnIODownloadPage();
