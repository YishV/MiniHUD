#pragma once

#include <windows.h>
#include <pdh.h>
#include <chrono>
#include <optional>
#include <string>

struct DiskReading {
    std::optional<float> temperature;
    std::optional<float> capacityGB;
    std::string name;
    std::string bus;    // NVMe / SATA / USB ...
    std::string status;
};

// Uses Windows storage IOCTLs. No vendor service or kernel driver is installed.
DiskReading ReadDiskTemperature();

class DiskSpeed {
public:
    DiskSpeed();
    ~DiskSpeed();
    DiskSpeed(const DiskSpeed&) = delete;
    DiskSpeed& operator=(const DiskSpeed&) = delete;
    bool Sample(float& readMBps, float& writeMBps);
private:
    PDH_HQUERY query_ = nullptr;
    PDH_HCOUNTER read_ = nullptr;
    PDH_HCOUNTER write_ = nullptr;
    bool primed_ = false;
};

class NetworkSpeed {
public:
    bool Sample(float& downMBps, float& upMBps);
private:
    unsigned long long lastDown_ = 0;
    unsigned long long lastUp_ = 0;
    std::chrono::steady_clock::time_point lastTime_{};
};
