// SPDX-License-Identifier: GPL-2.0-only
#include <stratosphere.hpp>
#include <switch.h>
#include "lease_scan_trace_runtime.hpp"
#include "managed_observer_runtime.hpp"
#include "capture_lease.hpp"
#include "intent_trace.hpp"
#include "ble_header_trace.hpp"
#include <cstdio>
#include <cstring>
#include <limits>
namespace ams::mc::lease_scan_trace {
namespace {
// Two existing SPSC types; IPC and BLE each keep their distinct sole producer.
::amazon_remote::IntentTrace g_intents;
::amazon_remote::BleHeaderTrace g_headers;
bool g_opted_in{}; // Immutable after Startup, before producers are launched.
std::atomic<std::uint64_t> g_begin{};
std::atomic<bool> g_suspended{};
bool g_attempted{},g_started{}; // Only Main owns text and storage.
constexpr char ModePath[]="sdmc:/config/amazon-remote/scan-diagnostic-mode.txt";
constexpr char OutputPath[]="sdmc:/config/amazon-remote/scan-diagnostic-v2.txt";
constexpr std::size_t MaxBytes=32768,FooterReserve=1024;
char g_text[MaxBytes],g_line[512];
std::size_t g_size{};
std::uint32_t g_text_drops{};
::amazon_remote::IntentRecord g_intent;
::amazon_remote::BleHeaderRecord g_header;
std::uint64_t Now(){return armTicksToNs(armGetSystemTick())/1000000;}
bool ReadLease(::amazon_remote::CaptureLeaseWire &lease) {
    return amazon_observer::ReadCaptureStatus(&lease,sizeof lease) &&
        lease.magic==::amazon_remote::CaptureLeaseMagic && lease.version==1 && !lease.reserved;
}
bool Live(std::uint64_t now) {
    const auto begin=g_begin.load(std::memory_order_acquire);
    ::amazon_remote::CaptureLeaseWire lease{};
    return g_opted_in && begin && !g_suspended.load(std::memory_order_acquire) &&
        ReadLease(lease) && lease.state==unsigned(::amazon_remote::CaptureLeaseState::Active) &&
        begin<=UINT64_MAX-60000 && lease.deadline_boot_ms==begin+60000 &&
        now>=begin && now<lease.deadline_boot_ms && lease.now_boot_ms<=now &&
        lease.now_boot_ms && now-lease.now_boot_ms<=3000;
}
void AddLine(const char *line,bool footer=false) {
    const auto size=std::strlen(line),limit=footer?MaxBytes:MaxBytes-FooterReserve;
    if(size+1>limit-g_size){if(g_text_drops<UINT32_MAX)++g_text_drops;return;}
    std::memcpy(g_text+g_size,line,size);g_size+=size;g_text[g_size++]='\n';
}
void Drain(unsigned budget) {
    for(unsigned i=0;i<budget && g_intents.Pop(g_intent);++i){
        if(::amazon_remote::FormatIntent(g_line,sizeof g_line,g_intent))AddLine(g_line);
    }
    for(unsigned i=0;i<budget && g_headers.Pop(g_header);++i){
        if(::amazon_remote::FormatBleHeader(g_line,sizeof g_line,g_header))AddLine(g_line);
    }
}
}
void Initialize() {
    fs::FileHandle file{};if(R_FAILED(fs::OpenFile(&file,ModePath,fs::OpenMode_Read)))return;
    ON_SCOPE_EXIT{fs::CloseFile(file);};
    constexpr char expected[]="lease-scan-v2\n";
    char mode[sizeof expected-1]{};s64 size{};std::size_t actual{};
    if(R_FAILED(fs::GetFileSize(&size,file)) || size!=sizeof mode ||
        R_FAILED(fs::ReadFile(&actual,file,0,mode,sizeof mode)) || actual!=sizeof mode)return;
    g_opted_in=std::memcmp(mode,expected,sizeof mode)==0;
}
void Arm(std::uint64_t begin_ms) {
    if(!g_opted_in || !begin_ms || begin_ms>UINT64_MAX-60000 ||
        g_suspended.load(std::memory_order_acquire))return;
    std::uint64_t empty{};
    if(!g_begin.compare_exchange_strong(empty,begin_ms,std::memory_order_release))return;
    g_intents.SetEnabled(true);g_headers.SetEnabled(true);
}
void ObserveRequest(std::uint32_t command,std::uint64_t program,std::uint64_t process,
                    const void *data,std::size_t size) {
    const auto now=Now();
    if(!Live(now) || !process || (program!=::amazon_remote::BtmProgram &&
        program!=::amazon_remote::HidProgram))return;
    // This is the ORIGINAL request before correction or SF forwarding. Actual
    // corrected forwarding results remain in the separate unchanged72B record.
    (void)g_intents.Observe(command,program,process,now,data,size);
}
void ObserveManaged(std::uint32_t type,const void *data,std::size_t size) {
    const auto now=Now();
    if(Live(now)) (void)g_headers.Observe(type,data,size,now);
}
void Poll() {
    const auto begin=g_begin.load(std::memory_order_acquire);
    if(!begin || g_attempted || g_suspended.load(std::memory_order_acquire))return;
    ::amazon_remote::CaptureLeaseWire lease{};if(!ReadLease(lease))return;
    if(!g_started) {
        g_started=true;
        std::snprintf(g_line,sizeof g_line,
            "scan_diagnostic=v2 begin_boot_ms=%llu deadline_boot_ms=%llu passive_only=1 original_requests=1 callback_owner_correlation=none callback_peer_address_name_ad_or_keys_copied=0",
            static_cast<unsigned long long>(begin),static_cast<unsigned long long>(begin+60000));
        AddLine(g_line);
    }
    // Main thread only. Producers never format, allocate, lock, wait or do IO.
    Drain(4);
    if(lease.state!=unsigned(::amazon_remote::CaptureLeaseState::Ended))return;
    g_intents.SetEnabled(false);g_headers.SetEnabled(false);
    Drain(::amazon_remote::BleHeaderTrace::Capacity);
    ::amazon_remote::IntentCounters ic{};::amazon_remote::BleHeaderCounters hc{};
    g_intents.SnapshotInto(ic);g_headers.SnapshotInto(hc);
    std::snprintf(g_line,sizeof g_line,
        "end_boot_ms=%llu intent_seen=%u intent_queued=%u intent_drops=%u intent_rejected=%u header_seen=%u header_queued=%u header_drops=%u header_short=%u text_drops=%u snapshot_approximate=1 late_callbacks_may_be_outside_lease=1",
        static_cast<unsigned long long>(Now()),ic.seen,ic.queued,ic.drops,ic.rejected,
        hc.seen,hc.queued,hc.drops,hc.short_input,g_text_drops);
    AddLine(g_line,true);g_attempted=true;
    // Exclusive, single attempt; retain existing, failed or partial artifacts.
    if(R_FAILED(fs::CreateFile(OutputPath,g_size)))return;
    fs::FileHandle file{};if(R_FAILED(fs::OpenFile(&file,OutputPath,fs::OpenMode_Write)))return;
    ON_SCOPE_EXIT{fs::CloseFile(file);};
    (void)fs::WriteFile(file,0,g_text,g_size,fs::WriteOption::Flush);
}
void Suspend() {
    g_suspended.store(true,std::memory_order_release);
    g_intents.SetEnabled(false);g_headers.SetEnabled(false);
}
}
