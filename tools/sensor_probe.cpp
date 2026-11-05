#include "sensors/system_io.h"

#include <windows.h>
#include <cstdio>

int main() {
    const DiskReading disk = ReadDiskTemperature();
    std::printf("Disk model: %s\n", disk.name.c_str());
    std::printf("Disk temperature: %.0f C (%s)\n", disk.temperature.value_or(-1.0f), disk.status.c_str());
    DiskSpeed diskSpeed;
    NetworkSpeed networkSpeed;
    float read = 0, write = 0, down = 0, up = 0;
    diskSpeed.Sample(read, write);
    networkSpeed.Sample(down, up);
    Sleep(1200);
    const bool diskOk = diskSpeed.Sample(read, write);
    const bool networkOk = networkSpeed.Sample(down, up);
    std::printf("Disk throughput: %s R %.2f / W %.2f MB/s\n", diskOk ? "OK" : "N/A", read, write);
    std::printf("Network throughput: %s D %.2f / U %.2f MB/s\n", networkOk ? "OK" : "N/A", down, up);
    return diskOk && networkOk ? 0 : 1;
}
