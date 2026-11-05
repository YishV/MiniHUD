#include "hud_state.h"
#include "hardware_summary.h"
#include <cstdio>
#include <limits>

int main() {
    int failures=0;
    auto check=[&](bool ok,const char* name) { if (!ok) { ++failures; std::printf("FAIL: %s\n",name); } };
    AlertLatch high;
    auto temp=[&](float value,double now) { return high.Update(Metric::CpuTemp,value,90,true,now,3,3); };
    check(!temp(95,0) && !temp(95,2.9),"short temperature spike ignored");
    check(!temp(89,3) && !temp(95,4) && !temp(95,6.9),"normal sample restarts trigger hold");
    check(temp(95,7),"sustained high temperature triggers");
    check(temp(89,8) && temp(88,12),"hysteresis keeps alert near threshold");
    check(temp(87,13) && temp(87,15.9),"recovery requires sustained margin");
    check(temp(88,16) && temp(87,17) && temp(87,19.9) && !temp(87,20),"interrupted recovery restarts hold");
    check(!temp(95,21) && temp(95,24) && !high.Update(Metric::CpuTemp,std::nullopt,90,true,25,3,3),"missing reading clears alert");
    check(!temp(95,26) && temp(95,29),"retrigger after missing reading");
    check(!high.Update(Metric::CpuTemp,100.0f,90,false,30,3,3),"global disable clears latch");
    check(!temp(95,31) && temp(95,34),"re-enable uses full hold");
    check(!high.Update(Metric::CpuTemp,100.0f,91,true,35,3,3),"threshold edit clears latch");
    check(!temp(100,40) && !temp(100,39),"clock rollback restarts hold");
    check(!temp(std::numeric_limits<float>::quiet_NaN(),42),"invalid reading clears state");
    AlertLatch low;
    auto fps=[&](float value,double now) { return low.Update(Metric::Fps,value,45,true,now,3,3); };
    check(!fps(44,0) && fps(44,3),"low FPS triggers after hold");
    check(fps(49,4) && fps(49,8) && fps(50,9) && !fps(50,12),"low FPS recovery uses higher margin");
    AlertLatch instant;
    check(instant.Update(Metric::GpuTemp,90.0f,85,true,0,0,0) &&
        !instant.Update(Metric::GpuTemp,82.0f,85,true,1,0,0),"zero delays switch immediately");
    check(!instant.Update(Metric::GpuTemp,90.0f,0,true,2,0,0),"per-metric disable");
    HudRuntime hud;
    hud.Notify("saved",10);
    check(hud.NoticeVisible(10) && hud.NoticeVisible(12.99) && !hud.NoticeVisible(13),"notice expires at three seconds");
    hud.Notify("started",12);
    check(hud.notice=="started" && hud.NoticeVisible(14) && !hud.NoticeVisible(15),"new notice replaces previous");
    SensorSnapshot s;
    s.cpuName="Test CPU"; s.cpuCores=8; s.cpuThreads=16; s.gpuName="Test GPU"; s.vramTotalGB=12.0f;
    s.baseboardVendor="Test"; s.baseboardProduct="Board"; s.ramModules={"16 GB DDR5 6000 MT/s","16 GB DDR5 6000 MT/s"};
    // Internal diagnostics must not enter the shareable summary.
    s.cpuStatus="PRIVATE DRIVER ERROR"; s.foregroundProcess="PRIVATE GAME"; s.biosVersion="PRIVATE BIOS";
    DiskReading disk; disk.name="Test SSD"; disk.capacityGB=1000.0f; disk.bus="NVMe";
    MonitorInfo monitor; monitor.name="Test Monitor"; monitor.width=2560; monitor.height=1440; monitor.refreshHz=165;
    const auto summary=BuildHardwareSummary(s,disk,{monitor},32);
    check(summary.find("8 核 16 线程")!=std::string::npos && summary.find("12.0 GB 显存")!=std::string::npos &&
        summary.find("32.0 GB")!=std::string::npos && summary.find("Test SSD · 1000.0 GB · NVMe")!=std::string::npos &&
        summary.find("2560×1440 @ 165 Hz")!=std::string::npos,"hardware summary contains useful specs");
    check(summary.find("PRIVATE")==std::string::npos,"summary excludes diagnostics and process names");
    const auto empty=BuildHardwareSummary({}, {}, {}, 0);
    check(empty.find("CPU：无数据")!=std::string::npos && empty.find("内存：无数据")!=std::string::npos &&
        empty.find("显示器：无数据")!=std::string::npos,"summary tolerates missing hardware");
    std::printf("HUD debounce, notice and hardware summary: %s (%d failures)\n",failures?"FAILED":"PASSED",failures);
    return failures?1:0;
}
