#pragma once

#include <cstddef>
#include <string>

// App version shown in logs and diagnostics (the resource version is generated separately).
inline constexpr const char* kAppVersion = "1.0";

// Appends "[yyyy-mm-dd hh:mm:ss] message" to %APPDATA%\MiniHUD\MiniHUD.log. Rotates to
// MiniHUD.log.1 once the file passes ~512 KB. Safe to call from any thread.
void DiagLog(const std::string& message);

// %APPDATA%\MiniHUD (created); empty when the known folder is unavailable.
std::wstring DiagDirectory();

// Writes the diagnostics text to %APPDATA%\MiniHUD\diagnostics\MiniHUD-diagnostics-<stamp>.txt.
bool WriteDiagnostics(const std::string& body, std::wstring& path);

// Last `maxChars` characters of the log (empty if there is none).
std::string ReadLogTail(size_t maxChars);
