// SPDX-License-Identifier: GPL-2.0-only
// Source-owned supplemental image. No standalone process entry, constructors,
// Bluetooth operation, firmware bytes, IPC server or native binding is here.
#include "deferred_endpoint.hpp"
namespace d=amazon_remote::deferred_capability;
namespace { constinit d::Slot owned_slot; }
extern "C" unsigned capture_envelope(const d::BorrowedEnvelope *envelope) {
    return static_cast<unsigned>(d::CaptureEnvelope(owned_slot,envelope));
}
extern "C" bool read_record(d::Record *output,std::size_t size) {
    return d::ReadRecord(owned_slot,output,size);
}
// Format identification only, not boot identity or runtime attestation.
extern "C" const std::uint32_t endpoint_format[4] __attribute__((used)) = {
    d::RecordMagic,d::RecordVersion,sizeof(d::Record),d::FirstCompleteOrError
};
