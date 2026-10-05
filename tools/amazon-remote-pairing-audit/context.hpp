// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "audit.hpp"
namespace araudit {
struct Connection {Address address{};std::uint32_t handle{};};
struct ConnectionQuery {Query list{};std::uint32_t target_handle{};};
struct ContextSnapshot {
    std::uint32_t magic{0x41525033},version{3};std::uint64_t begin_ms{},end_ms{};
    std::uint32_t user_open{NotAttempted};ConnectionQuery user_connections{};Query user_paired{};
    std::uint32_t btm_open{NotAttempted};ConnectionQuery system_connections{};Query system_paired{};
    std::uint32_t sys_open{NotAttempted},pairing_query{NotAttempted},pairing_started{};
    std::uint32_t reserved0{},checksum{},reserved{};
};
static_assert(sizeof(ConnectionQuery)==20 && sizeof(ContextSnapshot)==128 && offsetof(ContextSnapshot,checksum)==120);
inline std::uint32_t Checksum(const ContextSnapshot &s) {
    const auto *bytes=reinterpret_cast<const unsigned char *>(&s);std::uint32_t value=2166136261;
    for(std::size_t i=0;i<offsetof(ContextSnapshot,checksum);++i)value=(value^bytes[i])*16777619;
    return value;
}
inline void CountConnections(ConnectionQuery &q,const Address &target,const std::array<Connection,16> &list,std::uint32_t count) {
    if(q.list.result)return;
    q.list.count=count;
    const auto invalid=[&](){q.list.result=InvalidSnapshot;q.list.invalid=1;q.target_handle=0;};
    // A completely filled buffer cannot exclude truncation. Do not adopt it.
    if(count>=list.size()){invalid();return;}
    for(unsigned i=0;i<count;++i) {
        if(!list[i].handle || list[i].handle==0xffffffff){invalid();return;}
        for(unsigned j=0;j<i;++j)if(list[i].handle==list[j].handle || list[i].address==list[j].address){invalid();return;}
        if(list[i].address==target){++q.list.matches;q.target_handle=list[i].handle;}
    }
}
// A sequential getter snapshot, not connection ownership or a bond test.
// Uses the existing clients/parameter/count reducer, no additional service.
template<class Backend> ContextSnapshot CollectContext(Backend &b,const Address &target) {
    ContextSnapshot out{};out.begin_ms=b.Now();Parameter parameter{};
    out.user_open=b.OpenBle();
    if(!out.user_open) {
        std::array<Connection,16> connections{};std::uint32_t count{};
        out.user_connections.list.result=b.Connections(false,connections,count);
        CountConnections(out.user_connections,target,connections,count);
        std::array<Address,10> addresses{};count=0;
        out.user_paired.result=b.Paired(parameter,addresses,count);Count(out.user_paired,target,addresses,count);
        b.CloseBle();
    }
    out.btm_open=b.OpenHid();
    if(!out.btm_open) {
        std::array<Connection,16> connections{};std::uint32_t count{};
        out.system_connections.list.result=b.Connections(true,connections,count);
        CountConnections(out.system_connections,target,connections,count);
        std::array<Address,10> addresses{};count=0;
        out.system_paired.result=b.SystemPaired(parameter,addresses,count);Count(out.system_paired,target,addresses,count);
        b.CloseHid();
    }
    out.sys_open=b.OpenSystem();
    if(!out.sys_open) {
        bool started=false;out.pairing_query=b.PairingStarted(started);
        if(!out.pairing_query)out.pairing_started=started;
        b.CloseSystem();
    }
    out.end_ms=b.Now();out.checksum=Checksum(out);return out;
}
enum class ConnectionRelation {Indeterminate,Neither,UserOnly,SystemOnly,SameHandle,DifferentHandle};
inline ConnectionRelation Compare(const ContextSnapshot &s) {
    if(!s.begin_ms || s.end_ms<s.begin_ms || s.end_ms-s.begin_ms>2000 || s.user_open || s.btm_open ||
       s.user_connections.list.result || s.system_connections.list.result)return ConnectionRelation::Indeterminate;
    const auto &u=s.user_connections,&b=s.system_connections;
    if(!u.list.matches && !b.list.matches)return ConnectionRelation::Neither;
    if(!b.list.matches)return ConnectionRelation::UserOnly;
    if(!u.list.matches)return ConnectionRelation::SystemOnly;
    return u.target_handle==b.target_handle?ConnectionRelation::SameHandle:ConnectionRelation::DifferentHandle;
}
}
