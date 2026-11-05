// Feeds the recorder synthetic data and checks the generated CSV and report.

#include "recorder.h"
#include "util.h"

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

int main() {
    PerformanceRecorder recorder;
    recorder.Start();

    // ~2 s of 120 FPS frames with a few slow ones, delivered as one window.
    std::vector<float> frames(236, 1000.0f / 120.0f);
    frames.push_back(40.0f);
    frames.push_back(45.0f);
    recorder.AddFrameTimes(frames, Qpc());

    for (int second = 0; second < 3; ++second) {
        RecordSample sample;
        sample.elapsed = second;
        sample.fps = 118.0f;
        sample.frameTimeMs = 8.5f;
        sample.low1 = 92.0f;
        sample.low01 = 78.0f;
        sample.cpuUsage = 42.0f;
        sample.cpuTemp = 68.0f;
        sample.gpuUsage = 97.0f;
        sample.gpuTemp = 86.0f;
        sample.vramUsedGB = 7.6f;
        sample.vramTotalGB = 8.0f;
        recorder.AddSample(sample);
    }
    recorder.Stop();

    std::string message;
    const bool saved = recorder.Save(message);
    std::printf("save=%s message=%s\n", saved ? "ok" : "failed", message.c_str());
    if (!saved) return 1;

    // The report and CSV live under %APPDATA%\MiniHUD\records; the message does not carry the name,
    // so list the newest pair and check its content.
    wchar_t appdata[MAX_PATH];
    if (!GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH)) return 2;
    const std::wstring dir = std::wstring(appdata) + L"\\MiniHUD\\records";
    // Pick the newest report: earlier runs may have left files behind.
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((dir + L"\\*.md").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        std::printf("FAILED: no report file\n");
        return 1;
    }
    std::wstring report = dir + L"\\" + data.cFileName;
    FILETIME newest = data.ftLastWriteTime;
    while (FindNextFileW(find, &data)) {
        if (CompareFileTime(&data.ftLastWriteTime, &newest) > 0) {
            newest = data.ftLastWriteTime;
            report = dir + L"\\" + data.cFileName;
        }
    }
    FindClose(find);

    HANDLE file = CreateFileW(report.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 2;
    char text[8192] = {};
    DWORD read = 0;
    ReadFile(file, text, sizeof(text) - 1, &read, nullptr);
    CloseHandle(file);

    const std::string content(text, read);
    const bool hasLow = content.find("1% low") != std::string::npos;
    const bool hasVerdict = content.find("整体流畅") != std::string::npos;
    const bool hasStats = content.find("卡顿帧") != std::string::npos;
    std::printf("report=%s\n", std::string(report.begin(), report.end()).c_str());
    std::printf("has 1%% low=%d verdict=%d stutters=%d\n", hasLow, hasVerdict, hasStats);
    std::printf("Recorder checks: %s\n", (hasLow && hasVerdict && hasStats) ? "PASSED" : "FAILED");
    return (hasLow && hasVerdict && hasStats) ? 0 : 1;
}
