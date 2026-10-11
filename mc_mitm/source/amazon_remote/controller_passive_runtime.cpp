// SPDX-License-Identifier: GPL-2.0-only
#ifdef ARMODULE_CONTROLLER_PASSIVE_P1
#include <stratosphere.hpp>
#include <switch.h>
#include "controller_passive_runtime.hpp"
#include "controller_passive_cache.hpp"

namespace ams::mc::controller_passive_runtime {
namespace {
namespace p=::amazon_remote::controller_passive;
os::SdkMutex mutex;
p::Cache cache;
std::uint64_t Now(){return armTicksToNs(armGetSystemTick())/1000000;}
void EnsureStarted(std::uint64_t now){if(!cache.Read().instance_ms)cache.Start(now);}
}
bool Consume(std::uint32_t type,const void *event,std::size_t size) {
    if(type!=p::EventType)return false;
    std::scoped_lock lock(mutex);
    const auto now=Now();EnsureStarted(now);cache.Observe(event,size,now);
    // Owned type is always swallowed, including malformed or exhausted data.
    return true;
}
bool Read(void *out,std::size_t size) {
    if(!out || size!=sizeof(p::Snapshot))return false;
    std::scoped_lock lock(mutex);
    EnsureStarted(Now());const auto snapshot=cache.Read();
    if(!p::Valid(snapshot))return false;
    std::memcpy(out,&snapshot,sizeof snapshot);return true;
}
// Lock order: core event lock -> this mutex; getters take only this mutex.
// No SDK call except the local monotonic tick conversion. No driver session,
// file access, worker, query, pairing, filter update or Bluetooth initialization.
}
#endif
