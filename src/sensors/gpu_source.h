#pragma once

#include <optional>
#include <string>

// Dedicated video memory, in bytes.
struct GpuMemory {
    double used = 0.0;
    double total = 0.0;
};

// One way of reading GPU sensors: NVML (NVIDIA), ADL (AMD) or the vendor-neutral
// D3DKMT / performance counter fallback.
class GpuSource {
public:
    virtual ~GpuSource() = default;

    virtual bool Available() const = 0;
    const std::string& Name() const { return name_; }
    const std::string& Status() const { return status_; }

    // Vendor derived from the adapter name; empty when none is recognisable.
    virtual std::string Vendor() const {
        if (name_.find("NVIDIA") != std::string::npos) return "NVIDIA";
        if (name_.find("AMD") != std::string::npos || name_.find("Radeon") != std::string::npos) return "AMD";
        if (name_.find("Intel") != std::string::npos) return "Intel";
        return {};
    }

    // Collects fresh data for this poll; call once before the getters below.
    virtual void Update() {}
    virtual std::optional<float> Usage() = 0;       // percent
    virtual std::optional<float> Temperature() = 0; // °C
    virtual std::optional<float> Power() { return std::nullopt; } // watts
    virtual std::optional<GpuMemory> Memory() = 0;

protected:
    std::string name_;
    std::string status_;
};
