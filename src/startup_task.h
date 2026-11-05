#pragma once
#include <string>

struct StartupState {
    bool enabled = false;
    bool currentPath = true;
    std::string error;
};

// Only called when settings opens or the user changes the switch. No background service.
StartupState QueryStartup();
bool SetStartup(bool enabled, std::string& error);

// Pure XML generation, also used for schema validation without registering a task.
std::wstring StartupTaskXml(const std::wstring& executable, const std::wstring& userSid);
