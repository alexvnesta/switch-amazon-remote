// SPDX-License-Identifier: GPL-2.0-only
#include "intent_sdk/stratosphere.hpp"
#include "../mc_mitm/source/amazon_remote/intent_trace_runtime.hpp"
#include <cassert>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
namespace runtime=ams::mc::ble_intent_trace;
using namespace amazon_remote;
int main(int argc,char **argv) {
    assert(argc==2);const bool enabled=std::string(argv[1])=="enabled";
    assert(enabled || std::string(argv[1])=="disabled");
    constexpr char mode_path[]="sdmc:/config/amazon-remote/intent-trace-mode.txt";
    if(enabled) {const std::string mode="intents\n";fake_filter::files[mode_path]={mode.begin(),mode.end()};}
    runtime::Initialize();
    assert(runtime::Enabled()==enabled);
    const auto page=std::size_t(::sysconf(_SC_PAGESIZE));
    auto *memory=static_cast<std::uint8_t *>(::mmap(nullptr,2*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));
    assert(memory!=MAP_FAILED && ::mprotect(memory+page,page,PROT_NONE)==0);
    auto *prefix=memory+page-8;
    const auto *unreadable=memory+page;
    const std::array<std::uint8_t,6> target{2,3,4,5,6,7};
    // Exhaustive selector and no address read on any non-scan event.
    for(unsigned type=0;type<65536;++type) {
        assert(IsBleHeaderEvent(type)==(type==6 || type==7));
        if(type!=6)assert(!BleHeaderConfiguredAddressHint(type,unreadable,1024,target));
        if(type!=6 && type!=7)runtime::ObserveManagedHeader(type,unreadable,1024,true);
    }
    BleHeaderRecord record{};
    assert(!runtime::PopManagedHeader(record));
    unsigned expected_records{};
    for(unsigned action=0;action<=4;++action) {
        std::memset(prefix,0,8);prefix[4]=action;
        assert(!BleHeaderConfiguredAddressHint(7,prefix,1024,target));
        runtime::ObserveManagedHeader(7,prefix,1024,true);
        assert(runtime::PopManagedHeader(record)==enabled);
        if(enabled) {
            ++expected_records;
            assert(record.event_type==7 && record.copied_size==8 && !record.configured_address_hint);
            assert(record.prefix[4]==action);
        }
    }
    // Both runtime routes retain errors, short replies, null and original scan prefixes.
    for(unsigned size=0;size<=8;++size) {
        std::memset(prefix,0xa5,8);
        runtime::ObserveManagedHeader(7,prefix+8-size,size,true);
        assert(runtime::PopManagedHeader(record)==enabled);
        if(enabled){++expected_records;assert(record.copied_size==size && !record.configured_address_hint);}
    }
    runtime::ObserveManagedHeader(7,nullptr,1024,true);
    assert(runtime::PopManagedHeader(record)==enabled);
    if(enabled){++expected_records;assert(record.copied_size==0);}
    std::memset(prefix,0,8);prefix[4]=2;
    runtime::ObserveManagedHeader(6,prefix+1,1024,true);
    assert(runtime::PopManagedHeader(record)==enabled);
    if(enabled){++expected_records;assert(record.copied_size==7 && record.configured_address_hint);}
    for(unsigned i=0;i<33;++i)runtime::ObserveManagedHeader(7,prefix,8);
    for(unsigned i=0;i<32;++i)assert(runtime::PopManagedHeader(record)==enabled);
    assert(!runtime::PopManagedHeader(record));
    BleHeaderCounters counters{};runtime::HeaderSnapshotInto(counters);
    assert(counters.seen==(enabled?expected_records+33:0));
    assert(counters.drops==(enabled?1u:0u));
    // Existing absolute120s deadline remains, with no renewal or writes.
    fake_filter::now+=IntentWindowMs;
    runtime::ObserveManagedHeader(7,unreadable,1024);
    assert(!runtime::Enabled() && !runtime::PopManagedHeader(record));
    assert(fake_filter::creates==0 && fake_filter::writes==0);
    assert(::munmap(memory,2*page)==0);
    std::puts("PASS actual intent-header runtime: type7 completions,65536 selectors,guarded prefix-only reads,no address hint,errors/short/null,FIFO saturation,disabled/expiry,no file writes; source fixture only");
}
