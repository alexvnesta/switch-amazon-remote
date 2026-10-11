// SPDX-License-Identifier: GPL-2.0-only
#include "../mc_mitm/source/amazon_remote/power_lifecycle.hpp"
#include "../tools/amazon-remote-module/control/capture_preflight.hpp"
#include <cassert>
#include <cstdio>
#include <thread>
using namespace amazon_remote;
struct Adapter {
    OneShotCaptureLease lease;
    CaptureCloseTrace close;
    unsigned stopped{},resumed{};
    bool live{},attempted{};
    bool PauseUnused(){return lease.PauseUnusedForPower() && !live && !attempted;}
    bool ResumeUnused(){if(live || attempted)return false;++resumed;return lease.ResumeUnusedAfterPower();}
    void Close(CaptureCloseReason reason){++stopped;lease.End();close.Record(reason,1000);}
};
void Event(PowerLifecycle &power,Adapter &adapter,unsigned state,std::uint64_t now=1000) {
    power.Request(state,0x1234,0,now,adapter);power.Acknowledge(0,adapter);
}
PowerHistoryWire Read(const PowerLifecycle &p,std::uint64_t now=2000) {
    PowerHistoryWire w;assert(p.Read(w,now));assert(ValidPowerHistory(w,now));return w;
}
int main() {
    // Reproduce an early unarmed sleep request. Its later awake transition
    // restores only availability; no Arm, connection or bond request is made.
    for(bool full:{false,true}) {
        Adapter a;PowerLifecycle p;
        Event(p,a,2);assert(a.lease.Status(1001)==CaptureLeaseState::PowerPaused);
        assert(!a.lease.Arm(1001) && !a.stopped && !a.resumed);
        Event(p,a,1);assert(a.lease.Status(1002)==CaptureLeaseState::PowerPaused);
        if(!full)Event(p,a,4);
        p.Request(full?0:1,7,0,1100,a);
        assert(a.lease.Status(1100)==CaptureLeaseState::PowerPaused); // Before ACK.
        p.Acknowledge(0,a);
        assert(a.resumed==1 && !a.stopped && a.lease.Status(1101)==CaptureLeaseState::Available);
        assert(a.close.Read().reason==CaptureCloseReason::None);
        assert(Read(p).gate==0);
        assert(a.lease.Arm(1101)); // Still requires this explicit call.
        Event(p,a,2,1200);Event(p,a,4,1300);Event(p,a,1,1400);Event(p,a,0,1500);
        assert(a.lease.Status(1501)==CaptureLeaseState::Ended && !a.lease.Arm(1501));
        assert(Read(p).gate==2 && a.resumed==1);
    }
    for(unsigned state:{2u,3u,4u}) {
        Adapter a;PowerLifecycle p;Event(p,a,state);
        Event(p,a,state);assert(a.lease.Status(1100)==CaptureLeaseState::PowerPaused);
        Event(p,a,0);assert(a.lease.Status(1100)==CaptureLeaseState::Available);
    }
    for(unsigned state:{5u,6u,999u}) {
        Adapter a;PowerLifecycle p;Event(p,a,state);Event(p,a,0);
        assert(a.lease.Status(1100)==CaptureLeaseState::Ended && !a.resumed);
        assert(a.close.Read().reason==(state==5?CaptureCloseReason::ShutdownRequest:CaptureCloseReason::PowerError));
    }
    for(bool get_error:{false,true}) {
        Adapter a;PowerLifecycle p;Event(p,a,2);
        p.Request(0,9,get_error?0xbeef:0,1100,a);p.Acknowledge(get_error?UINT32_MAX:0xdead,a);
        Event(p,a,0,1200);assert(Read(p).gate==2 && !a.resumed && !a.lease.Arm(1300));
    }
    for(unsigned condition=0;condition<4;++condition) {
        Adapter a;PowerLifecycle p;
        if(condition==0)a.lease.End();
        if(condition==1){assert(a.lease.Arm(100));assert(!a.lease.Active(60100));}
        if(condition==2)a.live=true;
        if(condition==3)a.attempted=true;
        Event(p,a,2,61000);Event(p,a,0,62000);
        assert(Read(p,63000).gate==2 && !a.resumed && !a.lease.Arm(63000));
    }
    { // A link arriving during idle pause must not be resumed or adopted.
        Adapter a;PowerLifecycle p;Event(p,a,2);a.live=true;Event(p,a,0);
        assert(Read(p).gate==2 && a.lease.Status(1100)==CaptureLeaseState::Ended);
    }
    { // Explicit End during pause also stays terminal after waking.
        Adapter a;PowerLifecycle p;Event(p,a,2);a.lease.End();Event(p,a,0);
        assert(Read(p).gate==2 && !a.lease.Arm(1100));
    }
    { // Bounded history retains chronology, flags, results and the current gate.
        Adapter a;PowerLifecycle p;
        for(unsigned i=0;i<100;++i)Event(p,a,0,1000+i);
        auto w=Read(p);assert(w.count==16 && w.total==100 && w.sequence==200);
        assert(w.events[0].boot_ms==1084 && w.events[15].boot_ms==1099);
        assert(w.events[15].flags==0x1234 && !w.events[15].get_result && !w.events[15].ack_result);
        auto bad=w;bad.count=17;assert(!ValidPowerHistory(bad,2000));
        bad=w;bad.events[1].boot_ms=1;assert(!ValidPowerHistory(bad,2000));
        assert(!ValidPowerHistory(w,3501) && !ValidPowerHistory(w,1999));
    }
    { // Concurrent read is a coherent snapshot or an explicitly failed read.
        Adapter a;PowerLifecycle p;std::atomic<bool> done{};
        std::thread writer([&]{for(unsigned i=0;i<10000;++i)Event(p,a,0,1000+i);done=true;});
        while(!done.load()){PowerHistoryWire w;if(p.Read(w,20000))assert(ValidPowerHistory(w,20000));}
        writer.join();assert(Read(p,20000).total==10000);
    }
    for(unsigned i=0;i<1000;++i) {
        OneShotCaptureLease l;bool paused{},armed{};
        std::thread a([&]{paused=l.PauseUnusedForPower();if(!paused)l.End();});
        std::thread b([&]{armed=l.Arm(1000);});a.join();b.join();
        if(paused){assert(!armed);assert(l.ResumeUnusedAfterPower());assert(l.Status(1001)==CaptureLeaseState::Available);}
        else {assert(l.Status(1001)==CaptureLeaseState::Ended);assert(!l.ResumeUnusedAfterPower());}
    }
    { // Paused UI is distinct from disabled configuration and terminal sleep.
        Adapter a;PowerLifecycle p;Event(p,a,2);
        auto lease=a.lease.ReadStatus(1100);
        auto wire=DescribeCapture(lease,lease,a.close.Read(),true);
        assert(ValidCaptureDiagnostics(wire,1100) && !CaptureAvailable(wire,1100));
        arsetupview::Input input{};
        auto view=arsetupview::SelectCapturePreflight(input,arsetupview::Select(input),wire,0,1100);
        assert(!std::strcmp(view.status,"WAITING FOR AWAKE"));
    }
    std::puts("PASS power lifecycle: idle-only awake recovery, ACK gate, consumed/live/expired/error/shutdown fail-closed, 1000 Arm races, atomic bounded history; no device evidence.");
}
