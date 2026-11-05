#include "sensors/cpu_pawnio.h"

#include <cstdio>

int main() {
    CpuPawnIO cpu;
    FILE* out = nullptr;
    if (fopen_s(&out, "cpu_probe.txt", "w") != 0 || !out) return 2;
    const auto temperature = cpu.Temperature();
    std::fprintf(out, "CPU: %s\n", CpuPawnIO::CpuName().c_str());
    std::fprintf(out, "Status: %s\n", cpu.Status().c_str());
    if (temperature) std::fprintf(out, "Temperature: %.1f C\n", *temperature);
    else std::fprintf(out, "Temperature: N/A\n");
    std::fclose(out);
    return temperature ? 0 : 1;
}
