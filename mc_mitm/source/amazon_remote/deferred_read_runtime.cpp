// SPDX-License-Identifier: GPL-2.0-only
#ifdef ARMODULE_CONTROLLER_DEFERRED_D1
#include <stratosphere.hpp>
#include <switch.h>
#include <new>
#include "deferred_read_runtime.hpp"
#include "../../../tools/driver-capability-p1/deferred_read_state.hpp"
#ifdef ARMODULE_DEFERRED_HOST_TEST
#include <deferred_platform.hpp>
#else
#include "../async/async.hpp"
#include "../mcmitm_initialization.hpp"
#include "../bluetooth_mitm/bluetooth/bluetooth_events.hpp"
#endif

namespace ams::mc::deferred_read_runtime {
namespace {
namespace d=::amazon_remote::deferred_read;
namespace t=::amazon_remote::deferred_transport;
os::SdkMutex mutex;
d::Session session;
std::uint64_t Now(){return armTicksToNs(armGetSystemTick())/1000000;}
Result ReadOnWorker() {
    t::Request request{};
    {std::scoped_lock lock(mutex);if(!session.TakeWork(Now(),request))R_SUCCEED();}
    // The input-only native adapter accepts only this fixed metadata read.
    // It copies a previously captured record. No HCI/config/radio request.
    // Never hold this mutex or the core event mutex across driver IPC.
    const ::Result result=btdrvWriteHidData2(BtdrvAddress{},&request,sizeof request);
    {std::scoped_lock lock(mutex);session.Sent(result,Now());}
    R_SUCCEED();
}
}
bool Begin(std::uint64_t nonce) {
    // Session readiness is after btdrvInitialize; event readiness is only after
    // core/HID/BLE holders have all been linked by the consumer thread.
    const bool ready=mitm::IsInitialized() && bluetooth::events::IsReady();
    {std::scoped_lock lock(mutex);
        if(!session.Begin(nonce,Now(),ready))return false;
        if(!ready)return true;}
    auto *work=new(std::nothrow) async::AsyncFunction([]() -> Result {return ReadOnWorker();});
    if(!work || !async::TryQueueWork(work)) {
        delete work;
        std::scoped_lock lock(mutex);session.QueueFailed(Now());
    }
    return true;
}
bool Read(void *out,std::size_t size) {
    if(!out || size!=sizeof(d::Snapshot))return false;
    std::scoped_lock lock(mutex);const auto snapshot=session.Read(Now());
    if(!d::Valid(snapshot))return false;
    std::memcpy(out,&snapshot,sizeof snapshot);return true;
}
bool Consume(std::uint32_t type,const void *data,std::size_t capacity) {
    if(type!=t::EventType)return false;
    std::scoped_lock lock(mutex);
    // GetEventInfo returns fixed-capacity storage, not a wire-length output.
    // The matched native relay independently admits exactly 80 bytes.
    session.Consume(type,data,capacity>=sizeof(t::Event)?sizeof(t::Event):0,Now());
    return true;
}
// Lock order: core-event -> own mutex. Begin/getter/worker only use own mutex.
// Timeout freezes reports; synchronous driver IPC has no hard cancellation.
}
#endif
