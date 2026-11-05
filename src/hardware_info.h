#pragma once

#include <string>
#include <vector>

// CPU identity and topology, read from the registry and the Windows processor APIs.
struct CpuInfo {
    std::string vendor; // "Intel" / "AMD" / the raw vendor string
    int cores = 0;
    int threads = 0;
    float baseMhz = 0.0f;
    float maxMhz = 0.0f;
};

// One installed memory module from the SMBIOS tables (Windows mirrors them for user mode).
struct RamModule {
    std::string slot;
    std::string vendor;
    std::string partNumber;
    std::string type; // DDR4 / DDR5 / LPDDR4 ... empty when the firmware does not say
    unsigned capacityMB = 0;
    unsigned speedMt = 0; // MT/s
};

CpuInfo ReadCpuInfo();
std::vector<RamModule> ReadRamModules();
