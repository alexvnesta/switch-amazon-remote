// SPDX-License-Identifier: GPL-2.0-only
#include "deferred_endpoint.hpp"
namespace d=amazon_remote::deferred_capability;
extern "C" {
// Static initializer only. No startup constructor, reset, queue or radio call.
constinit d::Slot deferred_runtime_slot;
unsigned capture_envelope(const d::BorrowedEnvelope *envelope) {
    return static_cast<unsigned>(d::CaptureEnvelope(deferred_runtime_slot,envelope));
}
bool read_record(d::Record *output,std::size_t size) {
    return d::ReadRecord(deferred_runtime_slot,output,size);
}
extern const std::uint32_t endpoint_format[4] __attribute__((used)) = {
    d::RecordMagic,d::RecordVersion,sizeof(d::Record),d::FirstCompleteOrError
};
}
