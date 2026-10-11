// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "deferred_capture.hpp"
#include <type_traits>

namespace amazon_remote::deferred_capability {
// Borrowed native callback ABI, not a remotely supplied packet. The integrator
// must establish that a non-null envelope and its declared prefix are readable
// for this call. No pointer is retained. Unselected payloads are never touched.
struct BorrowedEnvelope {
    std::uint16_t opcode, length;
    std::uint32_t unused;
    const std::uint8_t *payload;
};
static_assert(sizeof(void*)==8 && sizeof(BorrowedEnvelope)==16);
static_assert(offsetof(BorrowedEnvelope,opcode)==0 && offsetof(BorrowedEnvelope,length)==2 &&
              offsetof(BorrowedEnvelope,payload)==8);
inline Capture CaptureEnvelope(Slot &slot,const BorrowedEnvelope *envelope) {
    if(!envelope)return Capture::Rejected;
    if(envelope->opcode!=controller_capabilities::VendorCapabilities)return Capture::Ignored;
    return slot.TryCapture(envelope->opcode,envelope->payload,envelope->length);
}

// This is a new wire contract, NOT the older event-history/cache snapshot.
// It is the first complete/error record for this slot's lifetime. Empty/Busy
// do not prove absence of callbacks, unsupported capability, or current state.
inline constexpr std::uint32_t RecordMagic=0x31444341,RecordVersion=1;
inline constexpr std::uint32_t FirstCompleteOrError=1;
struct Record {
    std::uint32_t magic{RecordMagic},version{RecordVersion},size{64},state{};
    std::uint32_t policy{FirstCompleteOrError},reserved[3]{};
    controller_passive::Event event{};
};
static_assert(sizeof(Record)==64 && offsetof(Record,event)==32);
static_assert(std::is_trivially_copyable_v<Record> && std::is_standard_layout_v<Record>);
static_assert(static_cast<unsigned>(Read::Empty)==0 && static_cast<unsigned>(Read::Busy)==1 &&
              static_cast<unsigned>(Read::Ready)==2);

// Internal fixed-size getter. A future IPC adapter must supply a validated
// writable buffer. Size mismatch/null leaves it untouched, never partly filled.
// No selector, address, offset, command or radio request comes from a caller.
inline bool ReadRecord(const Slot &slot,Record *output,std::size_t size) {
    if(!output || size!=sizeof(Record) || reinterpret_cast<std::uintptr_t>(output)%alignof(Record))return false;
    Record record{};
    record.event.magic=record.event.version=0;
    record.state=static_cast<std::uint32_t>(slot.ReadFirst(record.event));
    *output=record;
    return true;
}

// Strict receiving-side validation. Failure leaves the decoded reply untouched.
// memcpy permits an unaligned serialized input; this is not used in the native
// capture path. No version fallback to the old cache schema is permitted.
inline bool DecodeRecord(const void *bytes,std::size_t size,Read &state,
                         controller_capabilities::Reply &reply) {
    if(!bytes || size!=sizeof(Record))return false;
    Record record{};std::memcpy(&record,bytes,sizeof record);
    if(record.magic!=RecordMagic || record.version!=RecordVersion || record.size!=sizeof(Record) ||
       record.policy!=FirstCompleteOrError || record.state>static_cast<unsigned>(Read::Ready))return false;
    for(auto value:record.reserved)if(value)return false;
    if(record.state!=static_cast<unsigned>(Read::Ready)) {
        const auto *event=reinterpret_cast<const std::uint8_t*>(&record.event);
        for(std::size_t i=0;i<sizeof record.event;++i)if(event[i])return false;
        state=static_cast<Read>(record.state);return true;
    }
    controller_capabilities::Reply decoded{};
    if(!record.event.copied || !controller_passive::Decode(&record.event,sizeof record.event,decoded) ||
       (decoded.outcome!=controller_capabilities::Outcome::Reported &&
        decoded.outcome!=controller_capabilities::Outcome::ControllerError))return false;
    state=Read::Ready;reply=decoded;return true;
}
}
