#include "startup_task.h"
#include "util.h"
#include <taskschd.h>
#include <sddl.h>
#include <wrl/client.h>
#include <vector>
#include <cstdio>

int main() {
    const auto logPath=AppDirectory()+L"startup_check.txt";
    FILE* log=nullptr;
    if (_wfopen_s(&log,logPath.c_str(),L"w") || !log) return 2;
    const HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    const auto before=QueryStartup();
    HANDLE token=nullptr;
    OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token);
    DWORD bytes=0;
    GetTokenInformation(token,TokenUser,nullptr,0,&bytes);
    std::vector<BYTE> buffer(bytes);
    const bool tokenOk=GetTokenInformation(token,TokenUser,buffer.data(),bytes,&bytes)!=FALSE;
    if (token) CloseHandle(token);
    LPWSTR sid=nullptr;
    if (!tokenOk || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,&sid)) {
        std::fclose(log); if (SUCCEEDED(init)) CoUninitialize(); return 3;
    }
    const auto xml=StartupTaskXml(L"D:\\测试 & Tools\\MiniHUD.exe",sid);
    LocalFree(sid);
    bool ok=xml.find(L"测试 &amp; Tools")!=std::wstring::npos &&
        xml.find(L"<Arguments>--autostart</Arguments>")!=std::wstring::npos &&
        xml.find(L"HighestAvailable")!=std::wstring::npos && xml.find(L"<ExecutionTimeLimit>PT0S")!=std::wstring::npos;
    HRESULT hr;
    {
        Microsoft::WRL::ComPtr<ITaskService> service;
        Microsoft::WRL::ComPtr<ITaskDefinition> task;
        VARIANT empty{};
        hr=CoCreateInstance(CLSID_TaskScheduler,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&service));
        if (SUCCEEDED(hr)) hr=service->Connect(empty,empty,empty,empty);
        if (SUCCEEDED(hr)) hr=service->NewTask(0,&task);
        BSTR definition=SysAllocString(xml.c_str());
        if (SUCCEEDED(hr)) hr=task->put_XmlText(definition);
        SysFreeString(definition);
        if (SUCCEEDED(hr)) {
            Microsoft::WRL::ComPtr<IPrincipal> principal;
            Microsoft::WRL::ComPtr<ITaskSettings> settings;
            TASK_RUNLEVEL_TYPE level=TASK_RUNLEVEL_LUA;
            TASK_LOGON_TYPE logon=TASK_LOGON_NONE;
            VARIANT_BOOL battery=VARIANT_TRUE, stop=VARIANT_TRUE;
            hr=task->get_Principal(&principal);
            if (SUCCEEDED(hr)) hr=principal->get_RunLevel(&level);
            if (SUCCEEDED(hr)) hr=principal->get_LogonType(&logon);
            if (SUCCEEDED(hr)) hr=task->get_Settings(&settings);
            if (SUCCEEDED(hr)) hr=settings->get_DisallowStartIfOnBatteries(&battery);
            if (SUCCEEDED(hr)) hr=settings->get_StopIfGoingOnBatteries(&stop);
            ok=ok && level==TASK_RUNLEVEL_HIGHEST && logon==TASK_LOGON_INTERACTIVE_TOKEN &&
                battery==VARIANT_FALSE && stop==VARIANT_FALSE;
            std::fprintf(log,"Parsed settings: runlevel=%d logon=%d battery=%d stop=%d\n",level,logon,battery,stop);
        }
    }
    const auto after=QueryStartup();
    ok=ok && SUCCEEDED(hr) && before.error.empty() && after.error.empty() &&
        before.enabled==after.enabled && before.currentPath==after.currentPath;
    std::fprintf(log,"Startup XML schema and query: %s; HRESULT 0x%08lX\n",ok?"PASSED":"FAILED",static_cast<unsigned long>(hr));
    std::fprintf(log,"Existing startup preference preserved: %s; enabled=%d\n",
        before.enabled==after.enabled?"yes":"no",after.enabled);
    std::fprintf(log,"Query before: %s\nQuery after: %s\n",before.error.c_str(),after.error.c_str());
    std::fclose(log);
    if (SUCCEEDED(init)) CoUninitialize();
    return ok?0:1;
}
