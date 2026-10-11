// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstddef>
#include "../amazon-remote-module/protocol.hpp"
#include "../amazon-remote-module/startup.hpp"
#include "../../mc_mitm/source/amazon_remote/enrollment_context.hpp"
namespace arnativebond {
// Metadata only. READY is meaningful only with the pinned Nav17 file hash and
// live PM process check performed by the real backend. Context must be produced
// by that same instance's verified connected-session getter, not scan results.
inline bool ConnectedReady(const armodule::Status &s,const armodule::StartupRecord &startup,std::uint64_t now){
    // link_ready is scan admission only: the pinned producer clears it outside
    // SCANNING. It neither proves nor disproves an existing READY connection.
    return armodule::FreshStatus(s,now) && armodule::ValidStartup(startup) &&
        startup.stage==unsigned(armodule::StartupStage::Status) && !startup.result &&
        s.instance==startup.instance && startup.now_ms<=s.now_ms && s.state==unsigned(armodule::State::Ready) &&
        !s.result && s.output_attached && !s.buttons && !s.acknowledged_buttons &&
        !s.pad_cleanup_result && s.successful_writes;
}
inline bool ReadyBinding(const armodule::Status &s,const armodule::StartupRecord &startup,
    const amazon_remote::EnrollmentContextRecord &c,std::uint64_t now,std::uint32_t current_handle){
    if(!ConnectedReady(s,startup,now) || !amazon_remote::ValidEnrollmentContext(c) ||
       c.instance!=s.instance || c.before_binding || c.after_binding || c.expected_handle!=current_handle)return false;
    const auto &v=c.snapshot;
    if(!v.begin_ms || v.end_ms<v.begin_ms || v.end_ms-v.begin_ms>2000 ||
       v.end_ms>now || now-v.end_ms>60000 || v.user_open ||
       v.user_connections.list.result || v.user_connections.list.invalid ||
       !v.user_connections.list.count || v.user_connections.list.count>=16 || v.user_connections.list.matches!=1 ||
       v.user_connections.target_handle!=current_handle)return false;
    return current_handle && current_handle!=0xffffffff;
}
// A native address-based driver request is not scoped to the Trial app's
// btmu connection list. Require a fresh exact system view AND the module's
// saved user/system agreement; neither a cached handle nor ARUID spoofing is
// a substitute for a current target lookup. This strengthens ReadyBinding.
inline bool SystemReadyBinding(const armodule::Status &s,const armodule::StartupRecord &startup,
    const amazon_remote::EnrollmentContextRecord &c,std::uint64_t now,std::uint32_t current_handle){
    if(!ReadyBinding(s,startup,c,now,current_handle))return false;
    const auto &v=c.snapshot;const auto &q=v.system_connections;
    return !v.btm_open && !q.list.result && !q.list.invalid && q.list.count>0 &&
        q.list.count<16 && q.list.matches==1 && q.target_handle==current_handle;
}
}
