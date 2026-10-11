// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../current_status.hpp"
#include "../../../mc_mitm/source/amazon_remote/controller_query.hpp"
namespace arquery {
namespace cq = amazon_remote::controller_query;
enum class Result : std::uint32_t {
    Pending, PreconditionFailed, BeginFailed, ReadFailed, Completed,
    LocalTimeout, ExistingQuery, NoService, AppClosed
};
struct Record {
    std::uint32_t magic{0x31525141}, version{1};
    Result result{};
    std::uint32_t ipc_result{};
    std::uint64_t started_ms{}, finished_ms{};
    armodule::StartupRecord startup{};
    armodule::Status status{};
    cq::Wire wire{};
    std::uint32_t nonce{}, checksum{};
};
static_assert(sizeof(Record)==288);
inline bool SafeStopped(const armodule::Status &s, const armodule::StartupRecord &boot,
                        std::uint64_t now) {
    return armodule::CurrentStatus(s,boot,now) &&
        (s.state==unsigned(armodule::State::Stopped) || s.state==unsigned(armodule::State::Disconnected)) &&
        !s.result && !s.pad_cleanup_result && !s.link_ready && !s.buttons &&
        !s.output_attached && !s.acknowledged_buttons;
}
inline bool ValidWire(const cq::Wire &w) {
    return w.magic==cq::Magic && w.version==cq::Version &&
        unsigned(w.phase)<=unsigned(cq::Phase::Conflict) && !w.reserved0 && !w.reserved1 &&
        w.issued<=1 && w.submission_done<=1 && w.reply_count<=1 &&
        unsigned(w.reply.outcome)<=unsigned(amazon_remote::controller_capabilities::Outcome::Conflicting);
}
inline bool FreshWire(const cq::Wire &w, std::uint32_t nonce, std::uint64_t start, std::uint64_t now) {
    return ValidWire(w) && w.nonce==nonce && nonce && w.started_ms>=start && w.started_ms<=now &&
        w.started_ms<=UINT64_MAX-cq::WindowMs && w.deadline_ms==w.started_ms+cq::WindowMs &&
        (w.phase==cq::Phase::Waiting ? !w.finished_ms :
            w.finished_ms>=w.started_ms && w.finished_ms<=now);
}
inline std::uint32_t Checksum(const Record &r) {
    auto p=reinterpret_cast<const std::uint8_t *>(&r); std::uint32_t h=2166136261;
    for(std::size_t i=0;i<offsetof(Record,checksum);++i) h=(h^p[i])*16777619;
    return h;
}
inline bool Valid(const Record &r) {
    return r.magic==0x31525141 && r.version==1 && r.started_ms && r.finished_ms>=r.started_ms &&
        unsigned(r.result)>=1 && unsigned(r.result)<=unsigned(Result::AppClosed) &&
        r.checksum==Checksum(r) && ValidWire(r.wire) &&
        (r.result!=Result::Completed || (FreshWire(r.wire,r.nonce,r.started_ms,r.finished_ms) &&
             r.wire.phase!=cq::Phase::Waiting && r.wire.phase!=cq::Phase::Idle));
}
}
