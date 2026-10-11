// SPDX-License-Identifier: GPL-2.0-only
#include "../tools/amazon-remote-module/compact_log.hpp"
#include <cassert>
#include <cstdio>
int main(){
    using namespace armodule;
    for(auto s:{"raw notification conn=4 bytes=520000","input report=01 buttons=1 update=0",
                "buttons pressed=Up released=none","HID_METADATA_RAW after_security index=0 bytes=1234",
                "HID_METADATA before_security index=1 handle=10","HID_METADATA after_security index=1 handle=10"})
        assert(RedundantLog(s));
    for(auto s:{"","raw notification","API=failure Result=00001759","HID_METADATA before_security Result=00001759 count=0",
                "HID_METADATA after_security Result=00000000 count=9","user GATT event conn=4 result=5 flags=04",
                "HDLS ACK remote=1","B1_SECURITY_READY","OWNED_DISCONNECT failed","RETRY_A5_EARLY_FAILURE",
                "FINGERPRINT_VERIFIED","INPUT_METADATA_ADMITTED","LATENCY_A6_LOG_IO"})assert(!RedundantLog(s));
    LogTiming t;t.Record(10,15);t.Record(20,20);t.Omit();t.Record(30,29);
    assert(t.writes==3 && t.total_ms==5 && t.max_ms==5 && t.omitted==1 && t.invalid_clocks==1);
    const auto max=std::numeric_limits<std::uint64_t>::max();
    t.total_ms=max-1;t.writes=t.omitted=t.invalid_clocks=max;
    t.Record(1,10);t.Record(2,1);t.Omit();
    assert(t.total_ms==max && t.writes==max && t.omitted==max && t.invalid_clocks==max);
    std::puts("PASS compact diagnostic allowlist and bounded I/O timing counters");
}
