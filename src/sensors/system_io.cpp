#include "system_io.h"

#include <ws2def.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <winioctl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

namespace {
constexpr double kMB = 1024.0 * 1024.0;

std::string DiskProduct(const std::byte* bytes, size_t size, const STORAGE_DEVICE_DESCRIPTOR& d) {
    if (!d.ProductIdOffset || d.ProductIdOffset >= size) return {};
    const char* start = reinterpret_cast<const char*>(bytes + d.ProductIdOffset);
    const size_t maxLen = size - d.ProductIdOffset;
    const size_t len = strnlen_s(start, maxLen);
    return std::string(start, len);
}

std::string BusTypeName(STORAGE_BUS_TYPE bus) {
    switch (bus) {
    case BusTypeScsi: return "SCSI";
    case BusTypeAtapi: return "ATAPI";
    case BusTypeAta: return "ATA";
    case BusType1394: return "1394";
    case BusTypeUsb: return "USB";
    case BusTypeRAID: return "RAID";
    case BusTypeSas: return "SAS";
    case BusTypeSata: return "SATA";
    case BusTypeSd: return "SD";
    case BusTypeMmc: return "MMC";
    case BusTypeVirtual: return "虚拟";
    case BusTypeNvme: return "NVMe";
    case BusTypeUfs: return "UFS";
    default: return {};
    }
}
}

DiskReading ReadDiskTemperature() {
    DiskReading out;
    out.status = "No storage temperature exposed by Windows";
    for (int index = 0; index < 16; ++index) {
        const std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(index);
        HANDLE disk = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_EXISTING, 0, nullptr);
        if (disk == INVALID_HANDLE_VALUE) continue;

        STORAGE_PROPERTY_QUERY query{};
        query.QueryType = PropertyStandardQuery;
        std::array<std::byte, 1024> buffer{};
        DWORD bytes = 0;
        query.PropertyId = StorageDeviceProperty;
        std::string product;
        STORAGE_BUS_TYPE busType = BusTypeUnknown;
        if (DeviceIoControl(disk, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                            buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, nullptr) &&
            bytes >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
            const auto& descriptor = *reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
            product = DiskProduct(buffer.data(), bytes, descriptor);
            busType = descriptor.BusType;
        }

        query.PropertyId = StorageDeviceSeekPenaltyProperty;
        buffer.fill(std::byte{});
        bytes = 0;
        if (DeviceIoControl(disk, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                            buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, nullptr) &&
            bytes >= sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR) &&
            reinterpret_cast<const DEVICE_SEEK_PENALTY_DESCRIPTOR*>(buffer.data())->IncursSeekPenalty) {
            CloseHandle(disk);
            continue;
        }
        if (out.name.empty()) {
            out.name = product;
            out.bus = BusTypeName(busType);
            GET_LENGTH_INFORMATION length{};
            DWORD returned = 0;
            if (DeviceIoControl(disk, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &length, sizeof(length), &returned,
                                nullptr))
                out.capacityGB =
                    static_cast<float>(double(length.Length.QuadPart) / (1024.0 * 1024.0 * 1024.0));
            if (!out.capacityGB) { // the physical-drive size needs read access; fall back to the system volume
                ULARGE_INTEGER total{};
                wchar_t windows[MAX_PATH] = {};
                if (GetWindowsDirectoryW(windows, MAX_PATH) >= 3) {
                    const wchar_t root[4] = {windows[0], ':', '\\', 0};
                    if (GetDiskFreeSpaceExW(root, nullptr, &total, nullptr))
                        out.capacityGB = static_cast<float>(double(total.QuadPart) / (1024.0 * 1024.0 * 1024.0));
                }
            }
        }

        query.PropertyId = StorageDeviceTemperatureProperty;
        buffer.fill(std::byte{});
        bytes = 0;
        const bool ok = DeviceIoControl(disk, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                                        buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, nullptr) != FALSE;
        CloseHandle(disk);
        if (!ok || bytes < offsetof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR, TemperatureInfo) +
                            sizeof(STORAGE_TEMPERATURE_INFO)) continue;
        const auto& descriptor = *reinterpret_cast<const STORAGE_TEMPERATURE_DATA_DESCRIPTOR*>(buffer.data());
        if (!descriptor.InfoCount) continue;
        const SHORT celsius = descriptor.TemperatureInfo[0].Temperature;
        if (celsius < -20 || celsius > 150) continue;
        if (!product.empty()) out.name = product;
        out.temperature = static_cast<float>(celsius);
        out.status = "Windows storage temperature";
        return out;
    }
    return out;
}

DiskSpeed::DiskSpeed() {
    if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) return;
    if (PdhAddEnglishCounterW(query_, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", 0, &read_) != ERROR_SUCCESS ||
        PdhAddEnglishCounterW(query_, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0, &write_) != ERROR_SUCCESS) {
        PdhCloseQuery(query_);
        query_ = nullptr;
    }
}

DiskSpeed::~DiskSpeed() { if (query_) PdhCloseQuery(query_); }

bool DiskSpeed::Sample(float& readMBps, float& writeMBps) {
    if (!query_ || PdhCollectQueryData(query_) != ERROR_SUCCESS) return false;
    if (!primed_) { primed_ = true; return false; }
    PDH_FMT_COUNTERVALUE read{}, write{};
    if (PdhGetFormattedCounterValue(read_, PDH_FMT_DOUBLE, nullptr, &read) != ERROR_SUCCESS ||
        PdhGetFormattedCounterValue(write_, PDH_FMT_DOUBLE, nullptr, &write) != ERROR_SUCCESS) return false;
    readMBps = static_cast<float>(std::max(0.0, read.doubleValue) / kMB);
    writeMBps = static_cast<float>(std::max(0.0, write.doubleValue) / kMB);
    return true;
}

bool NetworkSpeed::Sample(float& downMBps, float& upMBps) {
    MIB_IF_TABLE2* table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR) return false;
    unsigned long long down = 0, up = 0;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2& row = table->Table[i];
        if (row.OperStatus != IfOperStatusUp || row.Type == IF_TYPE_SOFTWARE_LOOPBACK ||
            row.Type == IF_TYPE_TUNNEL) continue;
        down += row.InOctets;
        up += row.OutOctets;
    }
    FreeMibTable(table);
    const auto now = std::chrono::steady_clock::now();
    if (lastTime_.time_since_epoch().count() == 0 || down < lastDown_ || up < lastUp_) {
        lastDown_ = down;
        lastUp_ = up;
        lastTime_ = now;
        return false;
    }
    const double seconds = std::chrono::duration<double>(now - lastTime_).count();
    if (seconds < 0.1) return false;
    downMBps = static_cast<float>((down - lastDown_) / seconds / kMB);
    upMBps = static_cast<float>((up - lastUp_) / seconds / kMB);
    lastDown_ = down;
    lastUp_ = up;
    lastTime_ = now;
    return true;
}
