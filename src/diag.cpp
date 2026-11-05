#include "diag.h"

#include "util.h"

#include <shlobj.h>

#include <cstdio>
#include <mutex>

namespace {

std::mutex g_logMutex;

std::wstring AppDataDirectory() {
    PWSTR roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &roaming)) || !roaming) return {};
    std::wstring dir(roaming);
    CoTaskMemFree(roaming);
    if (dir.empty()) return {};
    dir += L"\\MiniHUD";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir + L'\\';
}

std::string Timestamp() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char text[32];
    std::snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour,
                  now.wMinute, now.wSecond);
    return text;
}

std::wstring LogPath() {
    const std::wstring dir = AppDataDirectory();
    return dir.empty() ? std::wstring() : dir + L"MiniHUD.log";
}

} // namespace

std::wstring DiagDirectory() { return AppDataDirectory(); }

void DiagLog(const std::string& message) {
    const std::wstring path = LogPath();
    if (path.empty()) return;

    std::lock_guard lock(g_logMutex);
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) &&
        (uint64_t(info.nFileSizeHigh) << 32 | info.nFileSizeLow) > 512u * 1024u) {
        MoveFileExW(path.c_str(), (path + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
    }

    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    const std::string line = "[" + Timestamp() + "] " + message + "\r\n";
    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(file);
}

bool WriteDiagnostics(const std::string& body, std::wstring& path) {
    const std::wstring dir = AppDataDirectory();
    if (dir.empty()) return false;
    const std::wstring diagnostics = dir + L"diagnostics\\";
    SHCreateDirectoryExW(nullptr, diagnostics.c_str(), nullptr);

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[96];
    std::swprintf(name, 96, L"MiniHUD-diagnostics-%04u%02u%02u-%02u%02u%02u.txt", now.wYear, now.wMonth, now.wDay,
                  now.wHour, now.wMinute, now.wSecond);
    path = diagnostics + name;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    DWORD written = 0;
    bool ok = WriteFile(file, bom, sizeof(bom), &written, nullptr) != 0;
    if (ok) ok = WriteFile(file, body.data(), static_cast<DWORD>(body.size()), &written, nullptr) != 0;
    CloseHandle(file);
    return ok;
}

std::string ReadLogTail(size_t maxChars) {
    const std::wstring path = LogPath();
    if (path.empty()) return {};
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};

    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    const uint64_t total = static_cast<uint64_t>(size.QuadPart);
    const uint64_t start = total > maxChars ? total - maxChars : 0;
    LARGE_INTEGER offset{};
    offset.QuadPart = static_cast<LONGLONG>(start);
    SetFilePointerEx(file, offset, nullptr, FILE_BEGIN);

    std::string text;
    text.resize(static_cast<size_t>(total - start));
    DWORD read = 0;
    ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
    text.resize(read);
    CloseHandle(file);
    return text;
}
