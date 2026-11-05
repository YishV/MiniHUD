// Prints the hardware identifiers the settings page shows, to verify the parsing on this machine.

#include "hardware_info.h"
#include "sensors/system_io.h"

#include <cstdio>

int main() {
    const CpuInfo cpu = ReadCpuInfo();
    std::printf("CPU: vendor=%s cores=%d threads=%d base=%.0f MHz max=%.0f MHz\n", cpu.vendor.c_str(), cpu.cores,
                cpu.threads, cpu.baseMhz, cpu.maxMhz);

    const auto modules = ReadRamModules();
    std::printf("RAM modules: %zu\n", modules.size());
    for (const RamModule& module : modules) {
        std::printf("  slot=%s vendor=%s part=%s type=%s %u MB %u MT/s\n", module.slot.c_str(), module.vendor.c_str(),
                    module.partNumber.c_str(), module.type.c_str(), module.capacityMB, module.speedMt);
    }

    const DiskReading disk = ReadDiskTemperature();
    std::printf("Disk: name=%s bus=%s capacity=%s temp=%s\n", disk.name.c_str(), disk.bus.c_str(),
                disk.capacityGB ? std::to_string(*disk.capacityGB).c_str() : "n/a",
                disk.temperature ? std::to_string(*disk.temperature).c_str() : "n/a");
    std::printf("Hardware probe: %s\n", (cpu.cores > 0 && cpu.threads > 0) ? "PASSED" : "FAILED");
    return (cpu.cores > 0 && cpu.threads > 0) ? 0 : 1;
}
