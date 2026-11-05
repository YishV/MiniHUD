#include "startup_task.h"
#include <windows.h>
#include <sddl.h>
#include <taskschd.h>
#include <wrl/client.h>
#include <cstdio>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
struct Bstr {
    BSTR value;
    explicit Bstr(const std::wstring& text) : value(SysAllocString(text.c_str())) {}
    ~Bstr() { SysFreeString(value); }
    operator BSTR() const { return value; }
};
std::wstring Escape(const std::wstring& text) {
    std::wstring out;
    for (wchar_t ch : text) {
        switch (ch) {
        case L'&': out += L"&amp;"; break;
        case L'<': out += L"&lt;"; break;
        case L'>': out += L"&gt;"; break;
        case L'\"': out += L"&quot;"; break;
        case L'\'': out += L"&apos;"; break;
        default: out += ch;
        }
    }
    return out;
}
std::wstring Executable() {
    std::vector<wchar_t> path(32768);
    const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    return n && n < path.size() ? std::wstring(path.data(), n) : std::wstring();
}
std::string Error(HRESULT hr) {
    char code[24]; std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(hr));
    return std::string("启动任务操作失败（") + code + "），请确认 MiniHUD 具有管理员权限，且 Windows 任务计划服务可用。";
}
struct Scheduler {
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HRESULT result = S_OK;
    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> folder;
    std::wstring sid, name;
    Scheduler() {
        if (FAILED(init) && init != RPC_E_CHANGED_MODE) { result=init; return; }
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) { result=HRESULT_FROM_WIN32(GetLastError()); return; }
        DWORD size=0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::vector<BYTE> data(size);
        const BOOL ok=GetTokenInformation(token, TokenUser, data.data(), size, &size);
        const DWORD error=GetLastError();
        CloseHandle(token);
        if (!ok) { result=HRESULT_FROM_WIN32(error); return; }
        LPWSTR text=nullptr;
        if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&text)) {
            result=HRESULT_FROM_WIN32(GetLastError()); return;
        }
        sid=text; LocalFree(text);
        name=L"MiniHUD.Startup." + sid;
        result=CoCreateInstance(CLSID_TaskScheduler,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&service));
        if (FAILED(result)) return;
        VARIANT empty{};
        result=service->Connect(empty,empty,empty,empty);
        if (FAILED(result)) return;
        result=service->GetFolder(Bstr(L"\\"),&folder);
    }
    ~Scheduler() {
        folder.Reset(); service.Reset();
        if (SUCCEEDED(init)) CoUninitialize();
    }
};
bool Missing(HRESULT hr) { return hr==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr==HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND); }
}

std::wstring StartupTaskXml(const std::wstring& executable, const std::wstring& userSid) {
    const auto slash=executable.find_last_of(L"\\/");
    const std::wstring directory=slash==std::wstring::npos ? L"" : executable.substr(0,slash);
    const auto sid=Escape(userSid);
    return L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
        L"<RegistrationInfo><Author>MiniHUD</Author><Description>MiniHUD 当前用户登录自启动</Description></RegistrationInfo>"
        L"<Triggers><LogonTrigger><Enabled>true</Enabled><UserId>" + sid +
        L"</UserId><Delay>PT8S</Delay></LogonTrigger></Triggers>"
        L"<Principals><Principal id=\"User\"><UserId>" + sid +
        L"</UserId><LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>"
        L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
        L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries><StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
        L"<StartWhenAvailable>true</StartWhenAvailable><Enabled>true</Enabled><ExecutionTimeLimit>PT0S</ExecutionTimeLimit></Settings>"
        L"<Actions Context=\"User\"><Exec><Command>" + Escape(executable) +
        L"</Command><Arguments>--autostart</Arguments><WorkingDirectory>" + Escape(directory) + L"</WorkingDirectory></Exec></Actions></Task>";
}

StartupState QueryStartup() {
    Scheduler scheduler;
    StartupState state;
    if (FAILED(scheduler.result)) { state.error=Error(scheduler.result); return state; }
    ComPtr<IRegisteredTask> task;
    HRESULT hr=scheduler.folder->GetTask(Bstr(scheduler.name),&task);
    if (Missing(hr)) return state;
    if (FAILED(hr)) { state.error=Error(hr); return state; }
    VARIANT_BOOL enabled=VARIANT_FALSE;
    hr=task->get_Enabled(&enabled);
    if (FAILED(hr)) { state.error=Error(hr); return state; }
    state.enabled=enabled!=VARIANT_FALSE;
    ComPtr<ITaskDefinition> definition;
    ComPtr<IActionCollection> actions;
    ComPtr<IAction> action;
    ComPtr<IExecAction> exec;
    hr=task->get_Definition(&definition);
    if (SUCCEEDED(hr)) hr=definition->get_Actions(&actions);
    if (SUCCEEDED(hr)) hr=actions->get_Item(1,&action);
    if (SUCCEEDED(hr)) hr=action.As(&exec);
    BSTR path=nullptr;
    if (SUCCEEDED(hr)) hr=exec->get_Path(&path);
    if (SUCCEEDED(hr)) state.currentPath=path && _wcsicmp(path,Executable().c_str())==0;
    else { state.currentPath=false; state.error=Error(hr); }
    SysFreeString(path);
    BSTR arguments=nullptr;
    if (SUCCEEDED(hr)) hr=exec->get_Arguments(&arguments);
    if (SUCCEEDED(hr)) state.currentPath=state.currentPath && arguments && wcscmp(arguments,L"--autostart")==0;
    else { state.currentPath=false; state.error=Error(hr); }
    SysFreeString(arguments);
    return state;
}

bool SetStartup(bool enabled, std::string& error) {
    error.clear();
    Scheduler scheduler;
    if (FAILED(scheduler.result)) { error=Error(scheduler.result); return false; }
    HRESULT hr;
    if (!enabled) {
        hr=scheduler.folder->DeleteTask(Bstr(scheduler.name),0);
        if (Missing(hr)) hr=S_OK;
    } else {
        const auto executable=Executable();
        if (executable.empty()) { error="无法取得当前程序路径，未创建启动任务。"; return false; }
        ComPtr<IRegisteredTask> task;
        VARIANT empty{}, user{};
        user.vt=VT_BSTR; user.bstrVal=SysAllocString(scheduler.sid.c_str());
        hr=scheduler.folder->RegisterTask(Bstr(scheduler.name),Bstr(StartupTaskXml(executable,scheduler.sid)),
            TASK_CREATE_OR_UPDATE,user,empty,TASK_LOGON_INTERACTIVE_TOKEN,empty,&task);
        VariantClear(&user);
    }
    if (FAILED(hr)) { error=Error(hr); return false; }
    return true;
}
