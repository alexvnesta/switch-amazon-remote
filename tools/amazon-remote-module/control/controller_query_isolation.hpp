// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "controller_query_report.hpp"
#include <cstring>
#include "../../../mc_mitm/source/amazon_remote/capture_diagnostics.hpp"
namespace arqueryq2 {
namespace cq=amazon_remote::controller_query;
using Result=arquery::Result;
constexpr std::uint32_t NotAttempted=UINT32_MAX, ProcessNotFound=0x20f, DisabledResult=0x1759;
constexpr char PausePath[]=ARMODULE_NAV15_CONFIG "/enable.before-q2.txt";
constexpr char IntentPath[]="sdmc:/switch/amazon-remote-capabilities-q1/isolation-q2.bin";
constexpr char ExpectedEnable[]="enable-experimental-module=1\n";
struct Intent {
    std::uint32_t magic{0x32495141},version{1};
    std::uint64_t previous_instance{},staging_tag{};
    std::uint32_t reserved{},checksum{};
};
static_assert(sizeof(Intent)==32 && offsetof(Intent,checksum)==28);
template<class T> std::uint32_t Checksum(const T &r) {
    const auto *p=reinterpret_cast<const std::uint8_t *>(&r);std::uint32_t h=2166136261;
    for(std::size_t i=0;i<offsetof(T,checksum);++i)h=(h^p[i])*16777619;
    return h;
}
inline bool ValidIntent(const Intent &i) {
    return i.magic==0x32495141 && i.version==1 && i.previous_instance && i.staging_tag &&
        !i.reserved && i.checksum==Checksum(i);
}
inline bool IsolatedBoot(const Intent &i,const armodule::StartupRecord &b,std::uint64_t now) {
    // Fresh staged isolation, not a historical stopped/error status. The pinned
    // input module exits at Enable before PSC/Bluetooth setup when disabled.
    return ValidIntent(i) && armodule::ValidStartup(b) && b.instance!=i.previous_instance &&
        b.now_ms<=now && b.stage==unsigned(armodule::StartupStage::Enable) && b.result==DisabledResult;
}
enum class Stage : std::uint32_t {
    Initial, IsolatedBoot, ProcessAbsent, UnusedLease, CloseRequested,
    ClosedLease, IsolationRechecked, Prepared
};
struct Record {
    std::uint32_t magic{0x32525141},version{1};
    Result result{};std::uint32_t ipc_result{};
    std::uint64_t started_ms{},finished_ms{};
    armodule::StartupRecord startup{};
    Intent intent{};
    amazon_remote::CaptureDiagnosticsWire before{},after{};
    cq::Wire wire{};
    std::uint64_t input_pid{};
    std::uint32_t pm_result{NotAttempted},end_result{NotAttempted},nonce{};
    Stage stage{};
    std::uint32_t reserved{},checksum{};
};
static_assert(sizeof(Record)==352 && offsetof(Record,checksum)==348);
inline bool ClosedUnused(const Record &r,std::uint64_t now) {
    const auto &v=r.after;
    return amazon_remote::ValidCaptureDiagnostics(v,now) && v.state==3 &&
        v.first_close_reason==unsigned(amazon_remote::CaptureCloseReason::ExplicitEnd) &&
        v.flags==(amazon_remote::CaptureSnapshotStable|amazon_remote::CaptureCloseRecorded) &&
        v.first_close_ms>=r.started_ms && v.now_boot_ms>=r.before.now_boot_ms;
}
// Only EndLease mutates companion metadata. It is admitted only for an UNUSED
// lease and an absent input process. It never starts a scan/connect or resets a
// consumed capture. The existing Q1 runtime still requires an ended lease.
template<class Backend> bool Prepare(Backend &sdk,Record &r) {
    if(!sdk.ReadIsolation(r.intent,r.startup) || !IsolatedBoot(r.intent,r.startup,sdk.Now()))return false;
    r.stage=Stage::IsolatedBoot;
    r.pm_result=sdk.ProcessId(r.input_pid);
    if(r.pm_result!=ProcessNotFound || r.input_pid)return false;
    r.stage=Stage::ProcessAbsent;
    r.ipc_result=sdk.ReadLease(r.before);
    if(r.ipc_result || !amazon_remote::CaptureAvailable(r.before,sdk.Now()))return false;
    r.stage=Stage::UnusedLease;
    r.stage=Stage::CloseRequested;r.end_result=sdk.EndLease();
    if(r.end_result)return false;
    r.ipc_result=sdk.ReadLease(r.after);
    if(r.ipc_result || !ClosedUnused(r,sdk.Now()))return false;
    r.stage=Stage::ClosedLease;
    Intent intent{};armodule::StartupRecord startup{};
    if(!sdk.ReadIsolation(intent,startup) || std::memcmp(&intent,&r.intent,sizeof intent) ||
        std::memcmp(&startup,&r.startup,sizeof startup) || !IsolatedBoot(intent,startup,sdk.Now()))return false;
    r.stage=Stage::IsolationRechecked;
    r.pm_result=sdk.ProcessId(r.input_pid);
    if(r.pm_result!=ProcessNotFound || r.input_pid)return false;
    r.stage=Stage::Prepared;return true;
}
inline bool Valid(const Record &r) {
    if(r.magic!=0x32525141 || r.version!=1 || r.reserved || !r.started_ms ||
       r.finished_ms<r.started_ms || unsigned(r.result)<1 || unsigned(r.result)>unsigned(Result::AppClosed) ||
       unsigned(r.stage)>unsigned(Stage::Prepared) || r.checksum!=Checksum(r) || !arquery::ValidWire(r.wire))return false;
    return r.result!=Result::Completed ||
        (r.stage==Stage::Prepared && IsolatedBoot(r.intent,r.startup,r.started_ms) &&
         r.pm_result==ProcessNotFound && !r.input_pid && !r.end_result &&
         arquery::FreshWire(r.wire,r.nonce,r.started_ms,r.finished_ms) &&
         r.wire.phase!=cq::Phase::Waiting && r.wire.phase!=cq::Phase::Idle);
}
}
