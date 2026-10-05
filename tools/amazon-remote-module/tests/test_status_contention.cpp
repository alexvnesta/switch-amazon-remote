// SPDX-License-Identifier: GPL-2.0-only
// Deterministic Horizon-style sharing-lock model; NOT hardware FS proof.
#include "../status_storage.hpp"
#include "../status_reader.hpp"
#include "../idle_health.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
namespace {
std::array<unsigned char,88> bytes{};bool present{};
std::uint64_t tick{},reader_until{},writer_until{};unsigned mode{},writes{},read_opens{},write_hold{};
void Reset() { bytes.fill(0xa5);present=false;tick=reader_until=writer_until=0;mode=writes=read_opens=write_hold=0; }
armodule::Status Frame(std::uint64_t now,std::uint64_t instance=7) {
    armodule::Status s{};s.instance=instance;s.now_ms=now;s.reserved=armodule::StatusChecksum(s);return s;
}
}
Result fsFsOpenFile(FsFileSystem *,const char *path,unsigned requested,FsFile *) {
    assert(!std::strcmp(path,armodule::StatusFsPath) && (requested==1 || requested==3));
    if (requested==1) ++read_opens;
    if (!present) return 0x202;
    if ((requested==3 && tick<reader_until) || (requested==1 && tick<writer_until)) return 0xe02;
    mode=requested;return 0;
}
Result fsFsCreateFile(FsFileSystem *,const char *,s64 size,unsigned option) { assert(!present && size==88 && !option);present=true;return 0; }
Result fsFileGetSize(FsFile *,s64 *out) { *out=88;return 0; }
Result fsFileRead(FsFile *,s64 offset,void *out,u64 size,unsigned options,u64 *count) {
    assert(!offset && size==88 && !options);std::memcpy(out,bytes.data(),88);*count=88;return 0;
}
Result fsFileWrite(FsFile *,s64 offset,const void *in,u64 size,unsigned flags) {
    assert(mode==3 && !offset && size==88 && flags==FsWriteOption_Flush);
    ++writes;writer_until=tick+write_hold;std::memcpy(bytes.data(),in,88);return 0;
}
void fsFileClose(FsFile *) { mode=0; }
int main() {
    using namespace armodule;FsFileSystem fs{};Status observed{};
    Reset();tick=1000;StatusWriter first;auto status=Frame(tick);
    assert(!first.WriteRecord(fs,status) && !ReadStatusFrame(fs,observed,tick));
    assert(observed.instance==7 && observed.reserved==status.reserved);
    bytes[20]^=1;assert(ReadStatusFrame(fs,observed,tick)==0x402 && observed.instance==7);
    bytes[20]^=1;assert(ReadStatusFrame(fs,observed,tick+2001)==0x402);
    IdleHealth gate;
    for (u64 now=1000;now<=16000;now+=500) gate.Observe(Frame(now),now);
    assert(gate.Ready(16000) && !gate.Ready(18001));
    gate.Observe(Frame(16000),18001);assert(!gate.Ready(18001));
    gate.Observe(Frame(20000,8),20000);assert(!gate.Ready(20000));
    auto error=Frame(20500,8);error.result=123;error.reserved=StatusChecksum(error);gate.Observe(error,20500);assert(!gate.Ready(20500));
    auto active=Frame(21000,8);active.state=unsigned(State::Scanning);active.reserved=StatusChecksum(active);gate.Observe(active,21000);assert(!gate.Ready(21000));
    for (u64 now=22000;now<=37000;now+=500) gate.Observe(Frame(now,8),now);
    assert(gate.Ready(37000));gate.Observe(Frame(36000,8),37000);assert(!gate.Ready(37000));
    StatusReadSchedule reads;unsigned polls{};for(u64 now=1000;now<2000;++now)polls+=reads.Due(now);assert(polls==4);
    unsigned cases{},locked_attempts{};
    for (unsigned phase=0;phase<250;phase+=10) for (unsigned hold:{0u,20u,80u,180u}) for (unsigned write_delay:{0u,20u,80u}) {
        Reset();write_hold=write_delay;StatusWriter writer;StatusWriteSchedule schedule;
        IdleHealth publisher,ui;Status cache{};bool have_cache{};
        for (tick=1000;tick<=33000;tick+=10) {
            // Other process may hold a read handle during IPC/file reads.
            if ((tick-1000)%250==phase && tick>=writer_until) reader_until=tick+hold;
            if (schedule.Due(tick)) {
                auto frame=Frame(tick);const auto rc=writer.WriteRecord(fs,frame);
                assert(!schedule.Complete(rc,writer.permanent,tick));
                if (!rc) publisher.Observe(frame,tick);else { assert(rc==0xe02);++locked_attempts; }
            }
            // UI cached snapshot is kept ONLY for a sharing lock and freshness.
            if ((tick-1000)%250==phase) {
                Status candidate{};const auto rc=ReadStatusFrame(fs,candidate,tick);
                if (!rc) { cache=candidate;have_cache=true; }
                else if (rc!=0xe02) have_cache=false;
            }
            if(have_cache && FreshStatus(cache,tick))ui.Observe(cache,tick);else ui.Reset();
        }
        assert(publisher.Ready(33000) && ui.Ready(33000) && writes>=35);++cases;
    }
    Reset();tick=1000;StatusWriter blocked;StatusWriteSchedule schedule;
    assert(!blocked.WriteRecord(fs,Frame(tick)));reader_until=100000;
    bool expired{};for(tick=1500;tick<4000 && !expired;tick+=10)if(schedule.Due(tick))expired=schedule.Complete(blocked.WriteRecord(fs,Frame(tick)),blocked.permanent,tick);
    assert(expired && tick<=3550 && writes==1); // sustained lock stops; no endless retry
    assert(StatusWriteSchedule{}.Complete(123,false,1000));
    assert(StatusWriteSchedule{}.Complete(0xe02,true,1000));
    std::printf("PASS %u lock-phase/hold scenarios, %u transient write locks, actual writer/reader/checksum guards, stable-IDLE permission and bounded sustained-lock stop; MODEL ONLY\n",cases,locked_attempts);
}
