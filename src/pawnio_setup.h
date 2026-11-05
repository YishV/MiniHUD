#pragma once

#include <string>

// Helpers around the official PawnIO installer (embedded as a resource in optional builds).

// DisplayVersion from the PawnIO uninstall registry key, or empty if not installed.
std::string InstalledPawnIOVersion();

// Installed and recent enough (2.x) for the bundled modules.
bool PawnIOReady();

// True if this build embeds PawnIO_setup.exe.
bool PawnIOSetupBundled();

// Extracts the bundled installer next to the executable, runs "-install -silent" (elevating
// if needed), waits for it and deletes it. Blocks for a few seconds; call off the UI thread if possible.
bool RunBundledPawnIOSetup(std::string& error);

// Official installer release page, opened in the browser when this build does not embed it.
constexpr const wchar_t* kPawnIODownloadUrl =
    L"https://github.com/namazso/PawnIO.Setup/releases/tag/2.2.0";

// Opens kPawnIODownloadUrl with the default browser. False if the shell refused the request.
bool OpenPawnIODownloadPage();
