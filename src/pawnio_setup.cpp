#include "pawnio_setup.h"

#include "resource.h"
#include "util.h"

#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <winhttp.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace {

const wchar_t kUninstallKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PawnIO";

constexpr size_t kMaxInstallerBytes = 32u * 1024u * 1024u;

bool FindSetupResource(const void*& data, DWORD& size) {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_PAWNIO_SETUP), RT_RCDATA);
    HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
    data = mem ? LockResource(mem) : nullptr;
    size = res ? SizeofResource(nullptr, res) : 0;
    return data && size;
}

// Unique directory under %TEMP%, removed by the caller.
bool MakeTempDirectory(std::wstring& directory, std::string& error) {
    wchar_t tempRoot[MAX_PATH], generated[MAX_PATH];
    const DWORD length = GetTempPathW(MAX_PATH, tempRoot);
    if (!length || length >= MAX_PATH || !GetTempFileNameW(tempRoot, L"MHD", 0, generated)) {
        error = "无法创建安装器临时目录";
        return false;
    }
    DeleteFileW(generated);
    if (!CreateDirectoryW(generated, nullptr)) {
        error = "无法创建安装器临时目录";
        return false;
    }
    directory = generated;
    return true;
}

// Runs "PawnIO_setup.exe -install -silent" elevated and waits for it.
bool RunInstallerFile(const std::wstring& path, std::string& error) {
    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas"; // no prompt when we are already elevated
    sei.lpFile = path.c_str();
    sei.lpParameters = L"-install -silent";
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        error = GetLastError() == ERROR_CANCELLED ? "已取消安装" : "无法启动 PawnIO 安装器";
        return false;
    }
    const DWORD wait = WaitForSingleObject(sei.hProcess, 120000);
    DWORD code = 1;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    if (wait != WAIT_OBJECT_0) {
        error = "安装等待超时，请稍后检查驱动状态";
        return false;
    }
    if (code != 0 && code != ERROR_SUCCESS_REBOOT_REQUIRED) {
        error = "安装器退出代码：" + std::to_string(code);
        return false;
    }
    return true;
}

bool FileSha256(const std::wstring& path, std::string& hex, std::string& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "无法读取下载的安装器";
        return false;
    }
    std::vector<unsigned char> data;
    unsigned char buffer[64 * 1024];
    bool ok = true;
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) {
            ok = false;
            break;
        }
        if (!read) break;
        data.insert(data.end(), buffer, buffer + read);
        if (data.size() > kMaxInstallerBytes) {
            ok = false;
            break;
        }
    }
    CloseHandle(file);
    if (!ok) {
        error = "读取下载的安装器失败";
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        error = "无法初始化 SHA-256";
        return false;
    }
    unsigned char hash[32] = {};
    const NTSTATUS status = BCryptHash(algorithm, nullptr, 0, data.data(), static_cast<ULONG>(data.size()), hash,
                                       sizeof(hash));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) {
        error = "计算 SHA-256 失败";
        return false;
    }

    static const char digits[] = "0123456789abcdef";
    hex.clear();
    for (unsigned char byte : hash) {
        hex += digits[byte >> 4];
        hex += digits[byte & 0x0F];
    }
    return true;
}

bool HttpDownloadFile(const std::wstring& url, const std::wstring& destination, std::string& error) {
    URL_COMPONENTS parts = {sizeof(parts)};
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts)) {
        error = "下载地址无效";
        return false;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring target(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength) target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    HINTERNET session = WinHttpOpen(L"MiniHUD/1.0 (PawnIO installer)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        error = "无法初始化下载";
        return false;
    }
    WinHttpSetTimeouts(session, 15000, 15000, 15000, 30000);

    HINTERNET connection = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    HINTERNET request =
        connection ? WinHttpOpenRequest(connection, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                   : nullptr;

    bool ok = false;
    if (request) {
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS; // GitHub redirects to a CDN host
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
        if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            DWORD status = 0, statusSize = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
            if (status != 200) {
                error = "下载失败：HTTP " + std::to_string(status);
            } else {
                HANDLE file = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                          FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file == INVALID_HANDLE_VALUE) {
                    error = "无法写入临时安装文件";
                } else {
                    ok = true;
                    size_t total = 0;
                    unsigned char buffer[64 * 1024];
                    for (;;) {
                        DWORD read = 0;
                        if (!WinHttpReadData(request, buffer, sizeof(buffer), &read)) {
                            ok = false;
                            error = "下载中断";
                            break;
                        }
                        if (!read) break;
                        DWORD written = 0;
                        if (!WriteFile(file, buffer, read, &written, nullptr) || written != read) {
                            ok = false;
                            error = "无法写入临时安装文件";
                            break;
                        }
                        total += read;
                        if (total > kMaxInstallerBytes) {
                            ok = false;
                            error = "下载的安装器大小异常";
                            break;
                        }
                    }
                    CloseHandle(file);
                    if (!ok) DeleteFileW(destination.c_str());
                }
            }
        } else {
            error = "无法连接下载服务器";
        }
    } else {
        error = "无法发起下载请求";
    }

    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}

} // namespace

std::string InstalledPawnIOVersion() {
    wchar_t buf[64] = {};
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kUninstallKey, L"DisplayVersion", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
                     nullptr, buf, &size) != ERROR_SUCCESS)
        return {};
    char out[64] = {};
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, sizeof(out), nullptr, nullptr);
    return out;
}

bool PawnIOReady() {
    std::string v = InstalledPawnIOVersion();
    return !v.empty() && std::atoi(v.c_str()) >= 2;
}

bool PawnIOSetupBundled() {
    const void* data;
    DWORD size;
    return FindSetupResource(data, size);
}

bool OpenPawnIODownloadPage() {
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", kPawnIODownloadPageUrl, nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32; // ShellExecute returns a value <= 32 on failure
}

bool DownloadPawnIOInstaller(const std::wstring& path, std::string& error) {
    if (!HttpDownloadFile(kPawnIOInstallerUrl, path, error)) return false;
    std::string hash;
    if (!FileSha256(path, hash, error)) {
        DeleteFileW(path.c_str());
        return false;
    }
    if (_stricmp(hash.c_str(), kPawnIOInstallerSha256) != 0) {
        error = "安装器 SHA-256 校验失败，已取消安装";
        DeleteFileW(path.c_str());
        return false;
    }
    return true;
}

bool InstallPawnIOFromWeb(std::string& error) {
    std::wstring directory;
    if (!MakeTempDirectory(directory, error)) return false;
    const std::wstring path = directory + L"\\PawnIO_setup.exe";
    auto cleanup = [&] {
        DeleteFileW(path.c_str());
        RemoveDirectoryW(directory.c_str());
    };

    if (!DownloadPawnIOInstaller(path, error)) {
        cleanup();
        return false;
    }
    const bool installed = RunInstallerFile(path, error);
    cleanup();
    return installed;
}

bool RunBundledPawnIOSetup(std::string& error) {
    const void* data;
    DWORD size;
    if (!FindSetupResource(data, size)) {
        error = "此版本未内置 PawnIO 安装器";
        return false;
    }

    std::wstring directory;
    if (!MakeTempDirectory(directory, error)) return false;
    const std::wstring path = directory + L"\\PawnIO_setup.exe";
    auto cleanup = [&] {
        DeleteFileW(path.c_str());
        RemoveDirectoryW(directory.c_str());
    };

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "无法写入临时安装文件";
        cleanup();
        return false;
    }
    DWORD written = 0;
    const BOOL writtenOk = WriteFile(file, data, size, &written, nullptr);
    CloseHandle(file);
    if (!writtenOk || written != size) {
        cleanup();
        error = "无法写入临时安装文件";
        return false;
    }

    const bool installed = RunInstallerFile(path, error);
    cleanup();
    return installed;
}
