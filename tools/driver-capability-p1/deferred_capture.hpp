// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../../mc_mitm/source/amazon_remote/controller_passive.hpp"
#include <atomic>

namespace amazon_remote::deferred_capability {
namespace p=controller_passive;
enum class Capture { Ignored, NotClaimed, Published, Rejected };
enum class Read { Empty, Busy, Ready };

// Offline replacement for direct startup-event publication. The integrator
// must provide genuinely owned, correctly initialized storage for this object.
// No native binding, allocation, queue, service, mutex, reset, retry, timestamp
// or radio operation is supplied here. Nothing may reuse the object in-place.
//
// Exactly one complete capability prefix or explicit controller error is
// retained for this object lifetime. An incomplete success or invalid envelope
// releases the unpublished claim, so it cannot hide a later complete reply.
// Later observations are ignored, not counted or compared. This is NOT the 64-event
// companion history, a reset epoch, a live-state view or wake support evidence.
class Slot {
public:
    Capture TryCapture(std::uint16_t opcode,const std::uint8_t *payload,std::uint16_t length) {
        if(opcode!=controller_capabilities::VendorCapabilities)return Capture::Ignored;
        std::uint32_t expected=Empty;
        // One weak attempt: spurious/contention failure drops this observation.
        // Native build must additionally verify the generated implementation
        // has no outlined helper, hidden lock or retry branch.
        if(!state_.compare_exchange_weak(expected,Writing,std::memory_order_acquire,
                                         std::memory_order_relaxed))return Capture::NotClaimed;
        p::Event event{};event.parameter_length=length;
        if(payload && length>=1 && length<=252) {
            const auto status=payload[0];
            if(status){event.copied=1;event.prefix[0]=status;}
            else if(length>=9 && length!=10 && payload[6]<=1) {
                event.copied=9;
                // Explicit bounded prefix copy, also under freestanding builds.
                // Do not introduce an out-of-line memcpy or borrowed pointer.
                event.prefix={0,payload[1],payload[2],payload[3],payload[4],
                              payload[5],payload[6],payload[7],payload[8],0,0,0};
            }
        }
        if(!event.copied){
            // No event bytes have been published or changed. This is not a
            // reset of a published record or an internal capture retry.
            state_.store(Empty,std::memory_order_release);
            return Capture::Rejected;
        }
        event_=event;
        state_.store(Published,std::memory_order_release);
        return Capture::Published;
    }
    Read ReadFirst(p::Event &out) const {
        const auto state=state_.load(std::memory_order_acquire);
        if(state!=Published)return state==Empty?Read::Empty:Read::Busy;
        out=event_; // Immutable after release publication; never read while Writing.
        return Read::Ready;
    }
    Slot()=default;
    Slot(const Slot&)=delete;
    Slot& operator=(const Slot&)=delete;
private:
    static constexpr std::uint32_t Empty=0,Writing=1,Published=2;
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    std::atomic<std::uint32_t> state_{Empty};
    p::Event event_{};
};
static_assert(sizeof(Slot)==36 && alignof(Slot)>=alignof(std::atomic<std::uint32_t>));
}
