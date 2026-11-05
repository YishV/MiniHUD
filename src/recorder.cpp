#include "recorder.h"
#include "util.h"
#include <shlobj.h>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace {
std::wstring RecordsDirectory() {
    PWSTR roaming=nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData,KF_FLAG_CREATE,nullptr,&roaming)) || !roaming) return {};
    std::wstring dir=std::wstring(roaming)+L"\\MiniHUD\\records";
    CoTaskMemFree(roaming);
    return dir;
}
bool WriteBytes(HANDLE file,const std::string& data) {
    DWORD written=0;
    return WriteFile(file,data.data(),static_cast<DWORD>(data.size()),&written,nullptr) && written==data.size();
}
bool WriteReport(const std::wstring& path,const std::string& data) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) return false;
    const bool ok=WriteBytes(file,data); CloseHandle(file); return ok;
}
}
PerformanceRecorder::~PerformanceRecorder() { Stop(); }

bool PerformanceRecorder::Start() {
    Clear();
    const auto dir=directory_.empty()?RecordsDirectory():directory_;
    if (dir.empty()) { error_="无法创建记录目录。"; return false; }
    const int created=SHCreateDirectoryExW(nullptr,dir.c_str(),nullptr);
    if (created!=ERROR_SUCCESS && created!=ERROR_ALREADY_EXISTS && created!=ERROR_FILE_EXISTS) {
        error_="无法创建记录目录。"; return false;
    }
    startQpc_=Qpc();
    SYSTEMTIME time{}; GetLocalTime(&time);
    wchar_t name[150];
    std::swprintf(name,std::size(name),L"MiniHUD-%04u-%02u-%02u_%02u-%02u-%02u-%03u-%lld",
        time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,startQpc_);
    csvPath_=dir+L"\\"+name+L".csv"; reportPath_=dir+L"\\"+name+L".md";
    csv_=CreateFileW(csvPath_.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (csv_==INVALID_HANDLE_VALUE) { error_="无法写入 CSV 文件。"; return false; }
    const std::string header="\xEF\xBB\xBF秒,帧率,帧时间ms,1%low,0.1%low,CPU占用%,CPU温度C,CPU功耗W,GPU占用%,GPU温度C,GPU功耗W,显存GB,内存GB\r\n";
    if (!WriteBytes(csv_,header)) { error_="无法写入 CSV 文件。"; Stop(); return false; }
    active_=true;
    return true;
}
void PerformanceRecorder::Stop() {
    if (startQpc_ && !stoppedQpc_) stoppedQpc_=Qpc();
    active_=false;
    if (csv_!=INVALID_HANDLE_VALUE) { FlushFileBuffers(csv_); CloseHandle(csv_); csv_=INVALID_HANDLE_VALUE; }
}
void PerformanceRecorder::Clear() {
    Stop(); frames_.Clear(); saved_=false; samples_=0; startQpc_=0; stoppedQpc_=0; acceptedEndQpc_=0;
    fps_={}; cpuUsage_={}; cpuTemp_={}; gpuUsage_={}; gpuTemp_={}; vramUsed_={}; vramTotal_={};
    hotRun_=0; longestHotRun_=0; lastElapsed_=-1;
    error_.clear(); csvPath_.clear(); reportPath_.clear();
}
double PerformanceRecorder::Seconds() const {
    return startQpc_ ? double((stoppedQpc_?stoppedQpc_:Qpc())-startQpc_)/double(QpcFrequency()):0;
}
void PerformanceRecorder::AddFrameTimes(const std::vector<float>& framesMs,int64_t lastFrameQpc) {
    if (!active_ || !lastFrameQpc) return;
    const double ticksPerMs=double(QpcFrequency())/1000;
    const double accepted=double(std::max(acceptedEndQpc_,startQpc_));
    double end=double(lastFrameQpc);
    for (size_t i=framesMs.size();i-- >0 && end>accepted+.01*ticksPerMs;) {
        frames_.Add(framesMs[i]);
        end-=double(framesMs[i])*ticksPerMs;
    }
    acceptedEndQpc_=std::max(acceptedEndQpc_,lastFrameQpc);
}
void PerformanceRecorder::AddSample(const RecordSample& sample) {
    if (!active_ || csv_==INVALID_HANDLE_VALUE) return;
    std::ostringstream row; row.imbue(std::locale::classic()); row<<std::fixed<<std::setprecision(3)<<sample.elapsed;
    auto cell=[&](const std::optional<float>& value) { row<<','; if (value && std::isfinite(*value)) row<<*value; };
    cell(sample.fps); cell(sample.frameTimeMs); cell(sample.low1); cell(sample.low01);
    cell(sample.cpuUsage); cell(sample.cpuTemp); cell(sample.cpuPowerW);
    cell(sample.gpuUsage); cell(sample.gpuTemp); cell(sample.gpuPowerW);
    cell(sample.vramUsedGB); cell(sample.ramUsedGB); row<<"\r\n";
    if (!WriteBytes(csv_,row.str())) { error_="写入记录失败，录制已停止。请检查磁盘空间。"; Stop(); return; }
    ++samples_; fps_.Add(sample.fps); cpuUsage_.Add(sample.cpuUsage); cpuTemp_.Add(sample.cpuTemp);
    gpuUsage_.Add(sample.gpuUsage); gpuTemp_.Add(sample.gpuTemp);
    vramUsed_.Add(sample.vramUsedGB); vramTotal_.Add(sample.vramTotalGB);
    hotRun_=sample.gpuTemp && *sample.gpuTemp>=85 ? ((lastElapsed_<0 || sample.elapsed-lastElapsed_<=1.5) ? hotRun_+1:1):0;
    longestHotRun_=std::max(longestHotRun_,hotRun_); lastElapsed_=sample.elapsed;
}
bool PerformanceRecorder::Save(std::string& message) {
    Stop();
    if (saved_) { message="记录已保存。"; return true; }
    if (!error_.empty()) { message=error_; return false; }
    if (!samples_) { message="没有记录到数据，请先开始记录。"; return false; }
    const auto stats=frames_.Summary();
    auto mean=[](const Aggregate& value) { return value.count?std::to_string(std::lround(value.sum/value.count)):std::string("无数据"); };
    auto peak=[](const Aggregate& value) { return value.count?std::to_string(std::lround(value.peak)):std::string("无数据"); };
    std::ostringstream md;
    md<<"# MiniHUD 性能记录\n\n- 时长："<<static_cast<int>(lastElapsed_+1)<<" 秒\n- 样本："<<samples_<<" 个，逐帧样本："<<stats.frames<<" 帧\n\n";
    md<<"## 关键数字\n\n| 指标 | 数值 |\n|---|---|\n";
    md<<"| 平均帧率 | "<<(stats.frames?std::to_string(std::lround(stats.avgFps)):mean(fps_))<<" FPS |\n";
    if (stats.frames>=64) {
        md<<"| 1% low | "<<std::lround(stats.low1Fps)<<" FPS |\n| 0.1% low | "<<std::lround(stats.low01Fps)<<" FPS |\n";
        md<<"| 帧时间 中位 / P95 / P99 | "<<stats.p50Ms<<" / "<<stats.p95Ms<<" / "<<stats.p99Ms<<" ms |\n";
        md<<"| 卡顿帧 | "<<stats.stutters<<"（"<<100.0*stats.stutters/stats.frames<<"%） |\n";
    }
    md<<"| CPU 占用均值 / 温度峰值 | "<<mean(cpuUsage_)<<"% / "<<peak(cpuTemp_)<<"℃ |\n";
    md<<"| GPU 占用均值 / 温度峰值 | "<<mean(gpuUsage_)<<"% / "<<peak(gpuTemp_)<<"℃ |\n";
    md<<"| 显存峰值 | ";
    if (vramUsed_.count) md<<std::fixed<<std::setprecision(1)<<vramUsed_.peak;
    else md<<"无数据";
    md<<" GB |\n\n## 结论\n\n";
    if (stats.frames>=64) {
        const double percent=100.0*stats.stutters/stats.frames;
        md<<(percent<1?"- 整体流畅":percent<5?"- 偶有卡顿":"- 卡顿明显")<<"：卡顿帧约占 "<<percent<<"%，1% low 为 "<<std::lround(stats.low1Fps)<<" FPS。\n";
    } else md<<"- 帧数据太少，无法判断卡顿，建议记录 1 分钟以上。\n";
    if (longestHotRun_>=60) md<<"- 显卡温度连续约 "<<longestHotRun_<<" 秒达到 85℃。\n";
    if (vramTotal_.peak>0 && vramUsed_.peak/vramTotal_.peak>=.9f) md<<"- 显存峰值接近容量上限。\n";
    md<<"\n每秒明细见同目录 CSV。未采集的指标显示为无数据。\n\n";
    md<<"统计说明：平均帧率使用全部逐帧数据的总时长；低帧、分位数和卡顿占比使用有界直方图近似，100ms 内桶宽 0.1ms，1 秒内为 1ms，60 秒内为 10ms，更长帧合并计算。卡顿阈值为 max(33.3ms, 中位数两倍)。\n";
    if (!WriteReport(reportPath_,md.str())) { message="无法写入报告文件。"; return false; }
    frames_.Clear();
    saved_=true;
    message="记录已保存到 %APPDATA%\\MiniHUD\\records（CSV 与报告）。";
    return true;
}
