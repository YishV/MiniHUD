#include "pawnio_setup.h"

#include "resource.h"
#include "util.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdlib>

namespace {

const wchar_t kUninstallKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PawnIO";

bool FindSetupResource(const void*& data, DWORD& size) {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_PAWNIO_SETUP), RT_RCDATA);
    HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
    data = mem ? LockResource(mem) : nullptr;
    size = res ? SizeofResource(nullptr, res) : 0;
    return data && size;
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

bool RunBundledPawnIOSetup(std::string& error) {
    const void* data;
    DWORD size;
    if (!FindSetupResource(data, size)) {
        error = "此版本未内置 PawnIO 安装器";
        return false;
    }

    wchar_t tempRoot[MAX_PATH], tempDirectory[MAX_PATH];
    const DWORD tempLength=GetTempPathW(MAX_PATH,tempRoot);
    if (!tempLength || tempLength>=MAX_PATH || !GetTempFileNameW(tempRoot,L"MHD",0,tempDirectory)) {
        error="无法创建安装器临时目录";
        return false;
    }
    DeleteFileW(tempDirectory);
    if (!CreateDirectoryW(tempDirectory,nullptr)) {
        error="无法创建安装器临时目录";
        return false;
    }
    const std::wstring path = std::wstring(tempDirectory) + L"\\PawnIO_setup.exe";
    auto cleanup = [&] { DeleteFileW(path.c_str()); RemoveDirectoryW(tempDirectory); };

    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        error = "无法写入临时安装文件";
        cleanup();
        return false;
    }
    DWORD written = 0;
    BOOL ok = WriteFile(f, data, size, &written, nullptr);
    CloseHandle(f);
    if (!ok || written != size) {
        cleanup();
        error = "无法写入临时安装文件";
        return false;
    }

    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas"; // no prompt when we are already elevated
    sei.lpFile = path.c_str();
    sei.lpParameters = L"-install -silent";
    sei.nShow = SW_HIDE;
    bool success = false;
    if (ShellExecuteExW(&sei) && sei.hProcess) {
        const DWORD wait=WaitForSingleObject(sei.hProcess, 120000);
        DWORD code = 1;
        GetExitCodeProcess(sei.hProcess, &code);
        CloseHandle(sei.hProcess);
        success = wait==WAIT_OBJECT_0 && (code==0 || code==ERROR_SUCCESS_REBOOT_REQUIRED);
        if (!success) error = wait==WAIT_TIMEOUT ? "安装等待超时，请稍后检查驱动状态" : "安装器退出代码：" + std::to_string(code);
    } else {
        error = GetLastError() == ERROR_CANCELLED ? "已取消安装" : "无法启动 PawnIO 安装器";
    }

    cleanup();
    return success;
}
