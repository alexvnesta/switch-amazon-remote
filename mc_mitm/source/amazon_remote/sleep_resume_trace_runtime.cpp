// SPDX-License-Identifier: GPL-2.0-only
#include <stratosphere.hpp>
#include <switch.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "sleep_resume_trace.hpp"
#include "sleep_resume_trace_runtime.hpp"
namespace ams::mc::sleep_resume_trace {
namespace {
::amazon_remote::sleep_trace::Trace g_trace;
::amazon_remote::sleep_trace::Report g_report; // Main-owned; never placed on its4KiB stack.
bool g_initialized{},g_attempted{},g_enabled{}; // Enabled is immutable before producers start.
std::uint8_t g_readback[256];
constexpr char ModePath[]="sdmc:/config/amazon-remote/sleep-resume-mode.txt";
std::uint64_t Now(){return static_cast<std::uint64_t>(os::GetSystemTick().ToTimeSpan().GetMilliSeconds());}
static_assert(sizeof(BtdrvBleAdvertiseFilter)==62 && offsetof(BtdrvBleAdvertiseFilter,mask_size)==61);
using FilterView=decltype(BtdrvBleEventInfo{}.scan_filter);
using ScanView=decltype(BtdrvBleEventInfo{}.scan_result);
static_assert(BtdrvBleEventType_ScanFilter==7 && sizeof(FilterView)==8 && offsetof(FilterView,action)==4);
static_assert(BtdrvBleEventType_ScanResult==6 && offsetof(ScanView,address)==7);
bool Save(const char *path) {
    if(R_FAILED(fs::CreateFile(path,sizeof g_report)))return false; // Exclusive, no overwrite or retry.
    {
        fs::FileHandle file{};if(R_FAILED(fs::OpenFile(&file,path,fs::OpenMode_Write)))return false;
        ON_SCOPE_EXIT{fs::CloseFile(file);};
        if(R_FAILED(fs::WriteFile(file,0,&g_report,sizeof g_report,fs::WriteOption::Flush)))return false;
    }
    fs::FileHandle file{};if(R_FAILED(fs::OpenFile(&file,path,fs::OpenMode_Read)))return false;
    ON_SCOPE_EXIT{fs::CloseFile(file);};
    s64 size{};if(R_FAILED(fs::GetFileSize(&size,file)) || size!=sizeof g_report)return false;
    for(std::size_t offset=0;offset<sizeof g_report;offset+=sizeof g_readback) {
        const auto length=std::min(sizeof g_readback,sizeof g_report-offset);std::size_t actual{};
        if(R_FAILED(fs::ReadFile(&actual,file,offset,g_readback,length)) || actual!=length ||
           std::memcmp(g_readback,reinterpret_cast<const std::uint8_t *>(&g_report)+offset,length))return false;
    }
    return true;
}
}
void Initialize() {
    if(g_initialized)return;
    g_initialized=true;
    fs::FileHandle file{};if(R_FAILED(fs::OpenFile(&file,ModePath,fs::OpenMode_Read)))return;
    ON_SCOPE_EXIT{fs::CloseFile(file);};
    constexpr char expected[]="sleep-resume-s1\n";char mode[sizeof expected-1]{};
    s64 size{};std::size_t actual{};
    if(R_FAILED(fs::GetFileSize(&size,file)) || size!=sizeof mode ||
       R_FAILED(fs::ReadFile(&actual,file,0,mode,sizeof mode)) || actual!=sizeof mode ||
       std::memcmp(mode,expected,sizeof mode))return;
    g_enabled=true;g_trace.Start(true,Now());
}
void ObserveRequest(std::uint32_t command,std::uint64_t program,const void *data,std::size_t size){if(g_enabled)g_trace.Intent(command,program,data,size,Now());}
void ObserveBle(std::uint32_t type,const void *data,std::size_t size){if(g_enabled)g_trace.Ble(type,data,size,Now());}
void PowerRequest(std::uint32_t state,std::uint32_t flags,std::uint32_t result){if(g_enabled)g_trace.Request(state,flags,result,Now());}
void PowerAcknowledge(std::uint32_t result){if(g_enabled)g_trace.Acknowledge(result,Now());}
void Poll() {
    if(!g_enabled || g_attempted || !g_trace.Finish(g_report,Now()))return;
    g_attempted=true;
    char path[128]{};std::snprintf(path,sizeof path,"sdmc:/config/amazon-remote/sleep-resume-s1-%llu.bin",
        static_cast<unsigned long long>(g_report.begin_ms));
    if(!Save(path))return; // Preserve partials; no automatic retry.
    // Marker is emitted ONLY after full report readback. No mutation of report.
    char marker[144]{};std::snprintf(marker,sizeof marker,"%s.ok",path);
    const std::array<std::uint32_t,4> receipt{0x4152534f,1,sizeof g_report,g_report.checksum};
    if(R_FAILED(fs::CreateFile(marker,sizeof receipt)))return;
    fs::FileHandle file{};if(R_FAILED(fs::OpenFile(&file,marker,fs::OpenMode_Write)))return;
    ON_SCOPE_EXIT{fs::CloseFile(file);};
    (void)fs::WriteFile(file,0,receipt.data(),sizeof receipt,fs::WriteOption::Flush);
}
}
