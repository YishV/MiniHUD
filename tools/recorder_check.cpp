#include "recorder.h"
#include "util.h"
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <iterator>
int main() {
    int failures=0;
    auto check=[&](bool ok,const char* name){ if(!ok){ std::printf("FAIL: %s\n",name); ++failures; } };
    const auto dir=AppDirectory()+L"recorder-check-"+std::to_wstring(GetCurrentProcessId());
    auto read=[](const std::wstring& path){ std::ifstream in{std::filesystem::path(path),std::ios::binary}; return std::string(std::istreambuf_iterator<char>(in),{}); };
    PerformanceRecorder recorder(dir);
    check(recorder.Start(),"start recorder");
    std::vector<float> frames(236,1000.0f/120); frames.push_back(40); frames.push_back(45);
    const auto end=Qpc()+3*QpcFrequency();
    recorder.AddFrameTimes(frames,end); recorder.AddFrameTimes(frames,end);
    for(int i=0;i<3;++i) {
        RecordSample sample; sample.elapsed=i; sample.fps=118.0f; sample.cpuUsage=42.0f;
        sample.cpuTemp=68.0f; sample.gpuUsage=97.0f; sample.gpuTemp=86.0f;
        sample.vramUsedGB=7.6f;
        recorder.AddSample(sample);
    }
    check(read(recorder.CsvPath()).find("118.000")!=std::string::npos,"CSV is written during recording");
    check(recorder.BufferedBytes()<200000,"bounded session statistics memory");
    recorder.Stop();
    const double seconds=recorder.Seconds(); Sleep(5);
    check(recorder.Seconds()==seconds,"stopped duration is frozen");
    std::string message;
    check(recorder.Save(message),"save report");
    auto report=read(recorder.ReportPath());
    check(report.find("238 帧")!=std::string::npos,"overlapping frame windows are deduplicated");
    check(report.find("整体流畅")!=std::string::npos && report.find("1% low")!=std::string::npos,"report contains frame statistics");
    check(recorder.BufferedBytes()==0,"frame histogram released after export");
    check(recorder.Save(message) && read(recorder.ReportPath())==report,"repeated stop/save preserves report");
    check(report.find("7.6 GB")!=std::string::npos,"VRAM report keeps decimal precision");
    DeleteFileW(recorder.CsvPath().c_str()); DeleteFileW(recorder.ReportPath().c_str());
    check(recorder.Start(),"start missing-data case");
    recorder.AddSample(RecordSample{}); recorder.Stop(); check(recorder.Save(message),"save missing-data report");
    report=read(recorder.ReportPath());
    check(report.find("无数据")!=std::string::npos && report.find("0% / 0℃")==std::string::npos,"missing metrics never become zero readings");
    DeleteFileW(recorder.CsvPath().c_str()); DeleteFileW(recorder.ReportPath().c_str()); RemoveDirectoryW(dir.c_str());
    std::printf("Recorder streaming, memory and missing-data checks: %s (%d failures)\n",failures?"FAILED":"PASSED",failures);
    return failures?1:0;
}
