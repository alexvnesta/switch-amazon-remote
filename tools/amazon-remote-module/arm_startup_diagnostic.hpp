// SPDX-License-Identifier: GPL-2.0-only
// Presentation/evidence only. No reset, rearm, retry or permission extension.
#pragma once
#include "protocol.hpp"
#include "../../mc_mitm/source/amazon_remote/capture_lease.hpp"
namespace armodule {
enum class ArmStage : std::uint32_t { ReadBefore=1, RequireAvailable, ArmCapture, ReadAfter, RequireLive, InitializeIdentity, StartScan };
inline constexpr char ArmDiagnosticPath[]=ARMODULE_NAV15_CONFIG "/arm-startup.bin";
inline constexpr char ArmDiagnosticTemp[]=ARMODULE_NAV15_CONFIG "/arm-startup.tmp";
struct ArmDiagnostic {
    std::uint32_t magic{0x41524133},version{1},stage{},result{};
    std::uint64_t instance{},now_ms{},lease_now_ms{},lease_deadline_ms{};
    std::uint32_t lease_state{},lease_valid{},reserved{},checksum{};
};
static_assert(sizeof(ArmDiagnostic)==64 && offsetof(ArmDiagnostic,checksum)==60);
static_assert(std::is_trivially_copyable_v<ArmDiagnostic>);
inline std::uint32_t ArmChecksum(const ArmDiagnostic &r) {
    std::uint32_t hash=2166136261u;const auto *bytes=reinterpret_cast<const std::uint8_t *>(&r);
    for(unsigned i=0;i<60;++i)hash=(hash^bytes[i])*16777619u;
    return hash;
}
inline bool ValidArmDiagnostic(const ArmDiagnostic &r) {
    if(r.magic!=0x41524133 || r.version!=1 || r.stage<1 || r.stage>7 || !r.result ||
       !r.instance || !r.now_ms || r.reserved || r.lease_valid>1 || r.checksum!=ArmChecksum(r))return false;
    if(!r.lease_valid)return !r.lease_state && !r.lease_now_ms && !r.lease_deadline_ms;
    if(r.lease_state>4 || !r.lease_now_ms || r.lease_now_ms>r.now_ms)return false;
    if(r.lease_state==0 || r.lease_state==3 || r.lease_state==4)return !r.lease_deadline_ms;
    return r.lease_deadline_ms>60000;
}
inline bool CaptureClosed(const ArmDiagnostic &r,const Status &s) {
    using amazon_remote::CaptureLeaseState;
    return ValidArmDiagnostic(r) && r.instance==s.instance && r.result==s.result &&
        r.now_ms<=s.now_ms && s.state==unsigned(State::Stopped) && !s.output_attached &&
        !s.buttons && !s.acknowledged_buttons && !s.successful_writes && !s.capture_deadline &&
        !s.pad_cleanup_result && !s.link_ready &&
        r.lease_valid && (r.stage==unsigned(ArmStage::RequireAvailable) || r.stage==unsigned(ArmStage::RequireLive)) &&
        (r.lease_state==unsigned(CaptureLeaseState::Ended) ||
         (r.lease_state==unsigned(CaptureLeaseState::Expired) && r.lease_now_ms>=r.lease_deadline_ms));
}
inline const char *ArmStageName(ArmStage stage) {
    constexpr const char *names[]{"invalid","read_before","require_available","arm_capture","read_after","require_live","initialize_identity","start_scan"};
    const auto n=unsigned(stage);return n<8?names[n]:names[0];
}
// This is the original ordered seven-step startup, with a checkpoint after
// each existing operation. No extra IPC, fallback, retry or lease extension.
// Logging failure stops further commands rather than silently losing evidence.
template<class Backend> std::uint32_t RunArmStartup(Backend &b) {
    using amazon_remote::CaptureLeaseState;
    amazon_remote::CaptureLeaseWire lease{};bool valid=false;
    const auto step=[&](ArmStage stage,std::uint32_t rc) {
        return b.Checkpoint(stage,rc,lease,valid)?rc:b.Invalid();
    };
    auto rc=b.Read(lease);valid=rc==0;rc=step(ArmStage::ReadBefore,rc);if(rc)return rc;
    rc=step(ArmStage::RequireAvailable,lease.state==unsigned(CaptureLeaseState::Available)?0:b.Invalid());if(rc)return rc;
    rc=step(ArmStage::ArmCapture,b.Arm());if(rc)return rc;
    rc=b.Read(lease);valid=rc==0;rc=step(ArmStage::ReadAfter,rc);if(rc)return rc;
    rc=step(ArmStage::RequireLive,b.Live(lease)?0:b.Invalid());if(rc)return rc;
    rc=step(ArmStage::InitializeIdentity,b.Initialize());if(rc)return rc;
    return step(ArmStage::StartScan,b.Scan());
}
}
