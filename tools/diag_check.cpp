// Checks the diagnostics log and export paths without starting the app.

#include "diag.h"

#include <windows.h>

#include <cstdio>
#include <string>

int main() {
    DiagLog("diagnostics check probe");

    const std::string tail = ReadLogTail(8192);
    const bool logged = tail.find("diagnostics check probe") != std::string::npos;

    std::wstring path;
    const bool exported = WriteDiagnostics("MiniHUD diagnostics probe\r\nhardware: test\r\n", path);
    bool wroteContent = false;
    if (exported) {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            char text[256] = {};
            DWORD read = 0;
            ReadFile(file, text, sizeof(text) - 1, &read, nullptr);
            CloseHandle(file);
            wroteContent = std::string(text, read).find("hardware: test") != std::string::npos;
        }
        DeleteFileW(path.c_str());
    }

    std::printf("log=%s export=%s content=%s\n", logged ? "ok" : "failed", exported ? "ok" : "failed",
                wroteContent ? "ok" : "failed");
    std::printf("Diagnostics checks: %s\n", (logged && exported && wroteContent) ? "PASSED" : "FAILED");
    return (logged && exported && wroteContent) ? 0 : 1;
}
