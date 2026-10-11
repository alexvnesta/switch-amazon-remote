// SPDX-License-Identifier: GPL-2.0-only
#include "sleep_sdk/stratosphere.hpp"
#include "../mc_mitm/source/amazon_remote/sleep_resume_trace.hpp"
#include "../mc_mitm/source/amazon_remote/sleep_resume_trace_runtime.hpp"
#include <cassert>
#include <cstdio>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
using namespace amazon_remote::sleep_trace;
namespace runtime=ams::mc::sleep_resume_trace;
void Core() {
    Trace trace;Report report;trace.Start(true,100);
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));
    auto *mem=static_cast<std::uint8_t *>(mmap(nullptr,2*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));
    assert(mem!=MAP_FAILED && mprotect(mem+page,page,PROT_NONE)==0);
    auto *p=mem+page-8;std::memset(p,0,8);
    trace.Ble(7,p,1024,101);trace.Ble(6,p+1,1024,101);
    for(unsigned type=0;type<65536;++type)if(type!=6&&type!=7)trace.Ble(type,mem+page,1024,102);
    for(unsigned command=0;command<100;++command)
        trace.Intent(command,0,mem+page,1024,102); // Foreign callers never read.
    trace.Intent(57,amazon_remote::BtmProgram,mem+page,61,103); // Malformed exact-size gate.
    std::array<std::uint8_t,62> filter;filter.fill(0xea);filter[0]=3;filter[1]=8;filter[2]=0xff;filter[61]=6;
    trace.Intent(57,amazon_remote::BtmProgram,filter.data(),filter.size(),103);
    trace.Request(2,0,0,110);trace.Acknowledge(0,111);assert(!trace.Ready(12000));
    trace.Ble(7,p,8,115); // Existing callback tap remains admitted after declared sleep.
    trace.Request(3,0,0,116);trace.Acknowledge(0,117);
    trace.Request(4,0,0,120);trace.Acknowledge(0,121);assert(!trace.Ready(12000));
    trace.Request(1,0,0,130);assert(!trace.Ready(12000)); // Successful ACK required.
    trace.Acknowledge(0,131);assert(!trace.Ready(10130));assert(trace.Finish(report,10131));
    assert(Valid(report) && report.verdict==1 && report.sleep_ms==110 && report.resume_ms==131);
    assert(report.ble_count==3 && report.intent_count==2 && report.power_count==8);
    assert((report.intents[0].flags==1 && report.intents[0].metadata==std::array<std::uint8_t,8>{}));
    assert(report.intents[1].metadata[0]==3 && report.intents[1].metadata[3]==6);
    for(unsigned i=4;i<8;++i)assert(report.intents[1].metadata[i]==0);
    const auto original=report.checksum;
    for(std::size_t i=0;i<sizeof report;++i) {
        auto *bytes=reinterpret_cast<std::uint8_t *>(&report);bytes[i]^=1;assert(!Valid(report));bytes[i]^=1;
    }
    assert(report.checksum==original && !trace.Finish(report,12000));
    trace.Start(true,12000);assert(!trace.Live(12001));assert(munmap(mem,2*page)==0);
    for(unsigned state=0;state<8;++state)for(unsigned get=0;get<2;++get)for(unsigned ack=0;ack<2;++ack) {
        Trace t;t.Start(true,100);t.Request(state,0,get,101);t.Acknowledge(ack,102);
        assert(!t.Ready(200)); // Cannot finish successfully without a cycle.
        t.Request(0,0,0,110);t.Acknowledge(0,111);
        if(get||ack||state>5)assert(t.Finish(report,112) && report.verdict==3);
        else if(state==5)assert(t.Finish(report,112) && report.verdict==4);
    }
    Trace expiry;expiry.Start(true,100);expiry.Request(2,0,0,110);expiry.Acknowledge(0,111);
    assert(!expiry.Ready(600100));expiry.Request(0,0,0,600110);expiry.Acknowledge(0,600111);
    assert(expiry.Finish(report,600112) && report.verdict==2);
    Trace saturated;saturated.Start(true,100);
    std::thread ble([&]{for(unsigned i=0;i<10000;++i)saturated.Ble(7,nullptr,0,101);});
    std::thread ipc([&]{for(unsigned i=0;i<10000;++i)saturated.Intent(55,amazon_remote::BtmProgram,nullptr,0,101);});
    ble.join();ipc.join();saturated.Request(2,0,0,110);saturated.Acknowledge(0,111);
    saturated.Request(0,0,0,120);saturated.Acknowledge(0,121);
    saturated.Ble(7,nullptr,0,122);saturated.Intent(55,amazon_remote::BtmProgram,nullptr,0,122);
    assert(saturated.Finish(report,10121) && report.ble_count==65 && report.intent_count==65 &&
        report.ble_drops==9936 && report.intent_drops==9936 && Valid(report));
    assert(report.ble_phase_drops[0]==9936 && report.ble_phase_drops[2]==0);
    assert(report.ble[64].flags>>24==2 && report.intents[64].flags>>24==2);
    // Consumer finalization may overlap bounded producers; queue memory is SPSC,
    // not a racy seqlock over ordinary objects. Late events are not success proof.
    for(unsigned round=0;round<30;++round) {
        Trace closing;closing.Start(true,100);
        std::thread b([&]{for(unsigned i=0;i<2000;++i)closing.Ble(7,nullptr,0,101);});
        std::thread c([&]{for(unsigned i=0;i<2000;++i)closing.Intent(55,amazon_remote::BtmProgram,nullptr,0,101);});
        closing.Request(2,0,0,110);closing.Acknowledge(0,111);
        closing.Request(0,0,0,120);closing.Acknowledge(0,121);
        assert(closing.Finish(report,10121));b.join();c.join();assert(Valid(report));
    }
    Trace repeat;repeat.Start(true,100);repeat.Request(2,0,0,110);repeat.Acknowledge(0,111);
    repeat.Request(0,0,0,120);repeat.Acknowledge(0,121);
    repeat.Request(2,0,0,125);repeat.Acknowledge(0,126);assert(!repeat.Ready(12000));
    repeat.Request(4,0,0,130);repeat.Acknowledge(0,131);repeat.Request(1,0,0,140);repeat.Acknowledge(0,141);
    repeat.Request(1,0,0,145);repeat.Acknowledge(0,146);
    assert(repeat.Finish(report,10121) && report.resume_ms==121); // No renewal on second cycle/MinimumAwake.
    Trace reversed;reversed.Start(true,100);reversed.Request(2,0,0,99);reversed.Acknowledge(0,101);
    assert(!reversed.Ready(12000));
    reversed.Request(0,0,0,110);reversed.Acknowledge(0,111);assert(reversed.Finish(report,112) && report.verdict==3);
    puts("PASS S1 source core: one acknowledged cycle,65536 selectors,guarded privacy bounds,all-byte corruption,64 power cases,expiry,saturation,two producers,no rearm");
}
int main(int argc,char **argv) {
    assert(argc==2||argc==3);const std::string mode=argv[1];if(mode=="core"){Core();return 0;}
    constexpr char config[]="sdmc:/config/amazon-remote/sleep-resume-mode.txt";
    constexpr char path[]="sdmc:/config/amazon-remote/sleep-resume-s1-100000.bin";
    if(mode!="disabled") {
        const std::string value=mode=="invalid"?"sleep-resume-s1\r\n":"sleep-resume-s1\n";
        fake_sleep::files[config]={value.begin(),value.end()};
    }
    if(mode=="existing")fake_sleep::files[path]={1,2,3};
    runtime::Initialize();runtime::Initialize();runtime::ObserveBle(7,nullptr,1024);
    runtime::ObserveRequest(55,amazon_remote::BtmProgram,nullptr,0);
    runtime::PowerRequest(2,0,0);runtime::PowerAcknowledge(0);
    fake_sleep::now+=20000;runtime::Poll();assert(fake_sleep::creates==0 && fake_sleep::writes==0);
    runtime::PowerRequest(4,0,0);runtime::PowerAcknowledge(0);
    runtime::PowerRequest(1,0,0);runtime::Poll();assert(fake_sleep::creates==0);
    runtime::PowerAcknowledge(mode=="ack-fail"?1:0);
    fake_sleep::now+=10000;
    fake_sleep::fail_create=mode=="create-fail";fake_sleep::fail_write=mode=="write-fail";
    fake_sleep::fail_open_write=mode=="open-fail";fake_sleep::corrupt_readback=mode=="corrupt";
    fake_sleep::short_readback=mode=="short";
    runtime::Poll();
    if(mode=="disabled"||mode=="invalid"||mode=="ack-fail")assert(fake_sleep::creates==0);
    else if(mode=="existing")assert(fake_sleep::files[path]==std::vector<std::uint8_t>({1,2,3}));
    else if(mode=="success") {
        Report r{};assert(fake_sleep::files[path].size()==sizeof r);
        std::memcpy(&r,fake_sleep::files[path].data(),sizeof r);assert(Valid(r) && r.verdict==1);
        assert(fake_sleep::files[std::string(path)+".ok"].size()==16 && fake_sleep::creates==2);
        if(argc==3) {
            auto *file=std::fopen(argv[2],"wbx");assert(file);
            const auto &bytes=fake_sleep::files[path];assert(std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size());assert(std::fclose(file)==0);
            const auto marker=std::string(argv[2])+".ok";file=std::fopen(marker.c_str(),"wbx");assert(file);
            const auto &ok=fake_sleep::files[std::string(path)+".ok"];assert(std::fwrite(ok.data(),1,ok.size(),file)==ok.size());assert(std::fclose(file)==0);
        }
    } else assert(!fake_sleep::files.count(std::string(path)+".ok"));
    const auto creates=fake_sleep::creates,writes=fake_sleep::writes;runtime::Poll();
    runtime::PowerRequest(2,0,0);runtime::PowerAcknowledge(0);runtime::PowerRequest(0,0,0);runtime::PowerAcknowledge(0);
    fake_sleep::now+=20000;runtime::Poll();
    if(mode=="ack-fail") {
        Report r{};assert(fake_sleep::files[path].size()==sizeof r);
        std::memcpy(&r,fake_sleep::files[path].data(),sizeof r);assert(Valid(r) && r.verdict==3);
    } else assert(creates==fake_sleep::creates && writes==fake_sleep::writes);
    puts("PASS actual S1 runtime: strict opt-in,no FS during sleep/before ACK,post-resume full readback,exclusive/no retry/failure preservation");
}
