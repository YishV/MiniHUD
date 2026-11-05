#include "hardware_info.h"

#include "util.h"

#include <windows.h>
#include <powrprof.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

std::string Narrow(const wchar_t* text) {
    if (!text || !*text) return {};
    char out[512];
    const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, out, sizeof(out), nullptr, nullptr);
    return n > 1 ? std::string(out, size_t(n - 1)) : std::string();
}

std::string RegString(HKEY key, const wchar_t* name) {
    wchar_t buffer[512] = {};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return {};
    return Narrow(buffer);
}

DWORD RegDword(HKEY key, const wchar_t* name) {
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) return 0;
    return value;
}

// SMBIOS strings are 1-based, stored after the formatted structure and terminated by a double NUL.
std::string SmbiosString(const unsigned char* table, size_t tableLength, size_t stringsOffset, unsigned char index) {
    if (!index) return {};
    size_t offset = stringsOffset;
    unsigned current = 1;
    while (offset < tableLength) {
        const char* text = reinterpret_cast<const char*>(table + offset);
        const size_t length = strnlen_s(text, tableLength - offset);
        if (!length) return {};
        if (current == index) return std::string(text, length);
        ++current;
        offset += length + 1;
    }
    return {};
}

const char* MemoryTypeName(unsigned char type) {
    switch (type) {
    case 0x12: return "DDR2";
    case 0x18: return "DDR3";
    case 0x1A: return "DDR4";
    case 0x1B: return "LPDDR";
    case 0x1C: return "LPDDR2";
    case 0x1D: return "LPDDR3";
    case 0x1E: return "LPDDR4";
    case 0x22: return "DDR5";
    case 0x23: return "LPDDR5";
    default: return "";
    }
}

// PROCESSOR_POWER_INFORMATION is not in the public SDK headers; the layout is stable.
struct ProcessorPowerInfo {
    ULONG NumberOfProcessors;
    ULONG CurrentMhz;
    ULONG MaxMhz;
    ULONG MhzLimit;
    ULONG MaxIdleState;
    ULONG CurrentIdleState;
};
constexpr POWER_INFORMATION_LEVEL kProcessorInformation = static_cast<POWER_INFORMATION_LEVEL>(11);

} // namespace

CpuInfo ReadCpuInfo() {
    CpuInfo info;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &key) ==
        ERROR_SUCCESS) {
        info.vendor = RegString(key, L"VendorIdentifier");
        info.baseMhz = static_cast<float>(RegDword(key, L"~MHz"));
        RegCloseKey(key);
    }
    if (info.vendor == "GenuineIntel") info.vendor = "Intel";
    else if (info.vendor == "AuthenticAMD") info.vendor = "AMD";

    // Max clock comes from the power manager (same value Task Manager shows).
    ProcessorPowerInfo power[64] = {};
    if (CallNtPowerInformation(kProcessorInformation, nullptr, 0, power, sizeof(power)) == 0) {
        for (const auto& entry : power) info.maxMhz = std::max(info.maxMhz, static_cast<float>(entry.MaxMhz));
    }

    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
    if (length) {
        std::vector<unsigned char> buffer(length);
        if (GetLogicalProcessorInformationEx(
                RelationProcessorCore,
                reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &length)) {
            size_t offset = 0;
            while (offset + 8 <= length) {
                const auto* entry =
                    reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
                if (entry->Size < 8) break;
                if (entry->Relationship == RelationProcessorCore) ++info.cores;
                offset += entry->Size;
            }
        }
    }

    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    info.threads = static_cast<int>(system.dwNumberOfProcessors);
    return info;
}

std::vector<RamModule> ReadRamModules() {
    std::vector<RamModule> modules;
    const DWORD size = GetSystemFirmwareTable('RSMB', 0, nullptr, 0);
    if (size < 8) return modules;

    std::vector<unsigned char> buffer(size);
    if (GetSystemFirmwareTable('RSMB', 0, buffer.data(), size) != size) return modules;

    DWORD tableLength = 0;
    std::memcpy(&tableLength, buffer.data() + 4, sizeof(tableLength)); // "RSMB" header holds the length
    tableLength = std::min<DWORD>(tableLength, size);

    size_t offset = 8;
    while (offset + 4 <= tableLength) {
        const unsigned char* structure = buffer.data() + offset;
        const unsigned char type = structure[0];
        const unsigned char length = structure[1];
        if (length < 4 || offset + length > tableLength) break;
        if (type == 127) break; // end-of-table

        if (type == 17 && length > 0x0D) { // Memory Device
            unsigned capacityMB = 0;
            uint16_t sizeWord = 0;
            std::memcpy(&sizeWord, structure + 0x0C, sizeof(sizeWord));
            if (sizeWord == 0x7FFF && length >= 0x20) {
                uint32_t extended = 0;
                std::memcpy(&extended, structure + 0x1C, sizeof(extended));
                capacityMB = extended & 0x7FFFFFFFu; // megabytes
            } else if (sizeWord != 0xFFFF) {
                capacityMB = sizeWord; // megabytes
            }
            if (capacityMB) {
                RamModule module;
                const size_t strings = offset + length;
                if (length > 0x10) module.slot = Trim(SmbiosString(buffer.data(), tableLength, strings, structure[0x10]));
                if (length > 0x12) module.type = MemoryTypeName(structure[0x12]);
                if (length > 0x16) std::memcpy(&module.speedMt, structure + 0x15, sizeof(uint16_t));
                if (length > 0x17) module.vendor = Trim(SmbiosString(buffer.data(), tableLength, strings, structure[0x17]));
                if (length > 0x1A)
                    module.partNumber = Trim(SmbiosString(buffer.data(), tableLength, strings, structure[0x1A]));
                if (length > 0x22) { // configured clock speed wins over the maximum rating
                    uint16_t configured = 0;
                    std::memcpy(&configured, structure + 0x20, sizeof(configured));
                    if (configured) module.speedMt = configured;
                }
                module.capacityMB = capacityMB;
                modules.push_back(std::move(module));
            }
        }

        // Skip the string table: it ends with two NUL bytes.
        size_t next = offset + length;
        while (next + 1 < tableLength && !(buffer[next] == 0 && buffer[next + 1] == 0)) ++next;
        next += 2;
        offset = next;
    }
    return modules;
}
