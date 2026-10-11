// SPDX-License-Identifier: GPL-2.0-only
#include "../tools/driver-capability-p1/deferred_endpoint.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
namespace d=amazon_remote::deferred_capability;
namespace c=amazon_remote::controller_capabilities;
int main() {
    const std::uint8_t prefix[9]={0,8,0,0x34,0x12,0,1,16,0};
    const auto *unreadable=reinterpret_cast<const std::uint8_t*>(1);
    d::Slot slot;d::Record output{};d::Read state=d::Read::Ready;c::Reply reply{};
    reply.status=123;
    assert(d::CaptureEnvelope(slot,nullptr)==d::Capture::Rejected);
    for(unsigned opcode=0;opcode<=65535;++opcode) {
        if(opcode==c::VendorCapabilities)continue;
        d::BorrowedEnvelope envelope{static_cast<std::uint16_t>(opcode),65535,0xffffffff,unreadable};
        assert(d::CaptureEnvelope(slot,&envelope)==d::Capture::Ignored);
    }
    assert(!d::ReadRecord(slot,nullptr,sizeof output));
    for(std::size_t size=0;size<=256;++size) {
        if(size==sizeof output)continue;
        std::memset(&output,0xa5,sizeof output);auto before=output;
        assert(!d::ReadRecord(slot,&output,size) && !std::memcmp(&output,&before,sizeof output));
    }
    assert(!d::ReadRecord(slot,&output,std::numeric_limits<std::size_t>::max()));
    alignas(d::Record) std::uint8_t misaligned[68];std::memset(misaligned,0xa5,sizeof misaligned);
    for(unsigned offset=1;offset<alignof(d::Record);++offset)
        assert(!d::ReadRecord(slot,reinterpret_cast<d::Record*>(misaligned+offset),sizeof output));
    for(auto byte:misaligned)assert(byte==0xa5);
    assert(d::ReadRecord(slot,&output,sizeof output));
    assert(d::DecodeRecord(&output,sizeof output,state,reply) && state==d::Read::Empty && reply.status==123);
    auto empty=output;
    // Untrusted empty/busy replies must not carry stale event bytes.
    for(unsigned index=32;index<64;++index) {
        auto bad=empty;reinterpret_cast<std::uint8_t*>(&bad)[index]=1;
        assert(!d::DecodeRecord(&bad,sizeof bad,state,reply));
    }
    auto busy=empty;busy.state=1;
    assert(d::DecodeRecord(&busy,sizeof busy,state,reply) && state==d::Read::Busy && reply.status==123);
    d::BorrowedEnvelope envelope{c::VendorCapabilities,1,0xffffffff,prefix};
    assert(d::CaptureEnvelope(slot,&envelope)!=d::Capture::Published);
    assert(d::ReadRecord(slot,&output,sizeof output) && output.state==0);
    envelope.length=9;
    const auto result=d::CaptureEnvelope(slot,&envelope);
    if(result==d::Capture::NotClaimed) {
        std::puts("PASS endpoint bounds and rejected envelope; weak claim dropped complete sample");return 0;
    }
    assert(result==d::Capture::Published);
    assert(d::ReadRecord(slot,&output,sizeof output));
    assert(d::DecodeRecord(&output,sizeof output,state,reply) && state==d::Read::Ready &&
           reply.outcome==c::Outcome::Reported && reply.max_filters==16);
    std::uint8_t unaligned[65];std::memcpy(unaligned+1,&output,sizeof output);
    assert(d::DecodeRecord(unaligned+1,sizeof output,state,reply));
    const auto valid=output;
    envelope.payload=unreadable;
    assert(d::CaptureEnvelope(slot,&envelope)==d::Capture::NotClaimed);
    assert(d::ReadRecord(slot,&output,sizeof output) && !std::memcmp(&valid,&output,sizeof output));
    for(unsigned index=0;index<8;++index) {
        auto bad=valid;std::uint32_t word;std::memcpy(&word,reinterpret_cast<const std::uint8_t*>(&bad)+index*4,4);
        word=~word;std::memcpy(reinterpret_cast<std::uint8_t*>(&bad)+index*4,&word,4);
        state=d::Read::Busy;reply.status=123;
        assert(!d::DecodeRecord(&bad,sizeof bad,state,reply) && state==d::Read::Busy && reply.status==123);
    }
    for(std::size_t size=0;size<=128;++size)if(size!=sizeof output)
        assert(!d::DecodeRecord(&valid,size,state,reply));
    assert(!d::DecodeRecord(nullptr,sizeof output,state,reply));
    auto invalid=valid;invalid.event.copied=0;
    assert(!d::DecodeRecord(&invalid,sizeof invalid,state,reply));
    invalid=valid;invalid.event.prefix[6]=2;
    assert(!d::DecodeRecord(&invalid,sizeof invalid,state,reply));
    std::puts("PASS deferred endpoint: exact envelope ABI, all unselected opcodes, fixed-size read, canonical empty/busy, immutable reply, strict wire decoding; no device integration");
}
