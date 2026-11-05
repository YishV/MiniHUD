#pragma once
#include "sensor_hub.h"
#include "overlay_window.h"
#include "sensors/system_io.h"

std::string BuildHardwareSummary(const SensorSnapshot& snapshot,const DiskReading& disk,
    const std::vector<MonitorInfo>& monitors,double totalRamGB);
bool CopyHardwareSummary(HWND owner,const std::string& summary);
