// Downloads the pinned PawnIO installer and verifies its SHA-256. Installs nothing, needs no admin.

#include "pawnio_setup.h"

#include <windows.h>

#include <cstdio>
#include <string>

int main() {
    wchar_t temp[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temp)) return 2;
    const std::wstring path = std::wstring(temp) + L"minihud_pawnio_probe.exe";
    std::string error;
    if (!DownloadPawnIOInstaller(path, error)) {
        std::printf("FAILED: %s\n", error.c_str());
        return 1;
    }
    LARGE_INTEGER size{};
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        GetFileSizeEx(file, &size);
        CloseHandle(file);
    }
    std::printf("OK: %lld bytes, SHA-256 matches %s\n", static_cast<long long>(size.QuadPart), kPawnIOInstallerSha256);
    DeleteFileW(path.c_str());
    return 0;
}
