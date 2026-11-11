#pragma once
#include "metric_rules.h"
#include <optional>
#include <string>

inline bool LowerIsWorse(Metric metric) {
    return metric==Metric::Fps || metric==Metric::FpsLow1 || metric==Metric::FpsLow01;
}
inline float RecoveryMargin(Metric metric,float limit) {
    float margin=3;
    switch(metric) {
    case Metric::Fps: case Metric::FpsLow1: case Metric::FpsLow01:
    case Metric::CpuUsage: case Metric::GpuUsage: case Metric::Ram: case Metric::Vram:
    case Metric::CpuPower: case Metric::GpuPower: margin=5; break;
    case Metric::FrameTime: margin=2; break;
    case Metric::DiskRead: case Metric::DiskWrite: case Metric::NetDown: case Metric::NetUp: margin=1; break;
    default: break;
    }
    return std::min(margin,limit*.25f);
}
class AlertLatch {
    bool active_=false,pending_=false;
    double since_=0,lastTime_=-1;
    float limit_=-1,trigger_=-1,recover_=-1;
public:
    bool Update(Metric metric,const std::optional<float>& value,float limit,bool enabled,
                double now,float triggerSeconds,float recoverySeconds) {
        if (limit!=limit_ || triggerSeconds!=trigger_ || recoverySeconds!=recover_ || now<lastTime_) {
            active_=false; pending_=false;
        }
        limit_=limit; trigger_=triggerSeconds; recover_=recoverySeconds; lastTime_=now;
        if (!enabled || !value || !std::isfinite(*value) || !std::isfinite(now) || !std::isfinite(limit) || limit<=0) {
            active_=false; pending_=false; return false;
        }
        const float margin=RecoveryMargin(metric,limit);
        const bool recovering=LowerIsWorse(metric) ? *value>=limit+margin : *value<=limit-margin;
        const bool condition=active_ ? recovering : MetricAlert(true,metric,*value,limit);
        if (!condition) pending_=false;
        else {
            if (!pending_) { pending_=true; since_=now; }
            const double delay=active_ ? recoverySeconds : triggerSeconds;
            if (now-since_>=delay) { active_=!active_; pending_=false; }
        }
        return active_;
    }
};

inline const char* ValueWidthSample(Metric metric,bool fahrenheit) {
    switch(metric) {
    case Metric::Fps: case Metric::FpsLow1: case Metric::FpsLow01: return "0000";
    case Metric::FrameTime: return "000.0 ms";
    case Metric::CpuUsage: case Metric::GpuUsage: return "100%";
    case Metric::CpuTemp: case Metric::GpuTemp: case Metric::DiskTemp: return fahrenheit ? "212 F":"100 C";
    case Metric::CpuPower: case Metric::GpuPower: return "999 W";
    case Metric::Ram: case Metric::Vram: return "00.0 / 00.0 GB";
    case Metric::DiskRead: case Metric::DiskWrite: return "0000.0 MB/s";
    case Metric::Time: return "00:00";
    default: return "000.0 MB/s";
    }
}

struct HudRuntime {
    std::array<AlertLatch,kMetricCount> alerts;
    std::array<float,kMetricCount> valueWidths{};
    float fontSize=0;
    std::string notice;
    double noticeUntil=0;
    void Notify(std::string text,double now,double duration=3) { notice=std::move(text); noticeUntil=now+duration; }
    bool NoticeVisible(double now) const { return !notice.empty() && now<noticeUntil; }
};
