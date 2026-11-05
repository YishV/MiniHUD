#include "hardware_summary.h"
#include <iomanip>
#include <sstream>

std::string BuildHardwareSummary(const SensorSnapshot& s,const DiskReading& disk,
    const std::vector<MonitorInfo>& monitors,double totalRamGB) {
    auto known=[](const std::string& text) { return text.empty()?std::string("无数据"):text; };
    std::ostringstream out;
    out<<std::fixed<<std::setprecision(1)<<"MiniHUD 电脑配置摘要\r\nCPU："<<known(s.cpuName);
    if (s.cpuCores>0 && s.cpuThreads>0) out<<" · "<<s.cpuCores<<" 核 "<<s.cpuThreads<<" 线程";
    out<<"\r\n显卡："<<known(s.gpuName);
    if (s.vramTotalGB && *s.vramTotalGB>0) out<<" · "<<*s.vramTotalGB<<" GB 显存";
    out<<"\r\n内存：";
    if (totalRamGB>0) out<<totalRamGB<<" GB"; else out<<"无数据";
    for (const auto& module:s.ramModules) out<<"\r\n  "<<module;
    out<<"\r\n主板："<<known(s.baseboardVendor+(s.baseboardVendor.empty()||s.baseboardProduct.empty()?"":" ")+s.baseboardProduct);
    out<<"\r\n硬盘："<<known(disk.name);
    if (disk.capacityGB && *disk.capacityGB>0) out<<" · "<<*disk.capacityGB<<" GB";
    if (!disk.bus.empty()) out<<" · "<<disk.bus;
    if (monitors.empty()) out<<"\r\n显示器：无数据";
    for (const auto& monitor:monitors) {
        out<<"\r\n显示器："<<known(monitor.name);
        if (monitor.width>0 && monitor.height>0) out<<" · "<<monitor.width<<"×"<<monitor.height;
        if (monitor.refreshHz>0) out<<" @ "<<monitor.refreshHz<<" Hz";
    }
    return out.str();
}

bool CopyHardwareSummary(HWND owner,const std::string& summary) {
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,summary.c_str(),-1,nullptr,0);
    if (count<=0) return false;
    HGLOBAL block=GlobalAlloc(GMEM_MOVEABLE,size_t(count)*sizeof(wchar_t));
    if (!block) return false;
    void* data=GlobalLock(block);
    if (!data) { GlobalFree(block); return false; }
    const bool converted=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,summary.c_str(),-1,static_cast<wchar_t*>(data),count)>0;
    GlobalUnlock(block);
    if (!converted || !OpenClipboard(owner)) { GlobalFree(block); return false; }
    const bool copied=EmptyClipboard() && SetClipboardData(CF_UNICODETEXT,block)!=nullptr;
    CloseClipboard();
    if (!copied) GlobalFree(block); // on success the clipboard owns this allocation
    return copied;
}
