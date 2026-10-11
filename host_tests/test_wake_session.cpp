// SPDX-License-Identifier: GPL-2.0-only
#include "../mc_mitm/source/amazon_remote/wake_session.hpp"
#include "../mc_mitm/source/amazon_remote/power_lifecycle.hpp"
#include <cassert>
#include <cstdio>
using namespace amazon_remote;
struct UsedAdapter {
    WakeSession wake;
    unsigned closed{},resumed{};
    bool PauseUnused(){return false;}
    bool ResumeUnused(){return false;}
    bool PauseSession(){return wake.Sleep();}
    bool ResumeSession(){++resumed;return wake.Awake();}
    void Close(CaptureCloseReason){++closed;wake.Stop();}
};
int main(){
    WakeSession w;
    assert(!w.Sleep() && !w.Awake() && !w.Ready(true));
    assert(!w.Complete(0,4,100) && !w.Complete(1,0,100) && !w.Complete(1,4,0));
    for(unsigned i=0;i<WakeSession::Capacity;++i){
        const auto nonce=20+i;
        assert(w.Complete(nonce,4,100+i));
        assert(!w.Complete(nonce,4,100+i));
        assert(w.Remaining()==WakeSession::Capacity-i-1);
        if(!w.Remaining()){assert(!w.Sleep() && !w.Awake() && !w.Ready(true));break;}
        assert(w.Sleep() && w.Sleep() && w.Serial()==i+1 && !w.Ready(true));
        assert(w.Awake() && !w.Ready(false) && w.Ready(true));
        assert(!w.Consume(i+2,nonce,4,100+i,true));
        assert(!w.Consume(i+1,nonce+1,4,100+i,true));
        assert(!w.Consume(i+1,nonce,5,100+i,true));
        assert(!w.Consume(i+1,nonce,4,101+i,true));
        assert(!w.Consume(i+1,nonce,4,100+i,false));
        assert(w.Sleep() && !w.Ready(true) && w.Awake());
        assert(w.Consume(i+1,nonce,4,100+i,true) && !w.Ready(true));
        assert(!w.Consume(i+1,nonce,4,100+i,true));
    }
    for(unsigned fail=0;fail<3;++fail){
        UsedAdapter a;PowerLifecycle power;
        assert(a.wake.Complete(20,4,100));
        for(unsigned s:{2u,3u,4u}){power.Request(s,0,0,1000+s,a);power.Acknowledge(0,a);}
        assert(!a.wake.Ready(true) && !a.closed && !a.resumed);
        power.Request(1,0,0,1100,a);power.Acknowledge(0,a);
        assert(!a.wake.Ready(true) && !a.resumed);
        power.Request(fail==2?5:0,0,0,1200,a);
        assert(!a.wake.Ready(true)); // Cannot admit before PSC ACK.
        power.Acknowledge(fail==1?0xdead:0,a);
        if(fail){assert(a.closed && !a.wake.Ready(true));}
        else assert(a.wake.Ready(true) && a.resumed==1 && !a.closed);
        a.wake.Stop();assert(!a.wake.Ready(true) && !a.wake.Awake() && !a.wake.Sleep());
    }
    std::puts("PASS A3 bounded wake admission, duplicate power events, ACK barrier, ownership, terminal Stop and capacity");
#ifdef ARMODULE_SETUP_SLEEP_R3
    {
        WakeSession p;
        assert(!p.SleepPending(0));
        for(unsigned i=0;i+1<WakeSession::Capacity;++i){
            const auto nonce=500+i;
            assert(p.SleepPending(nonce) && p.SleepPending(nonce) && p.Serial()==i+1 && p.Pending() && p.PendingNonce()==nonce);
            assert(!p.SleepPending(nonce+1));                 // another attempt cannot take over
            assert(!p.Sleep() && !p.Complete(nonce,4,100));   // not a completed session
            assert(!p.Ready(true) && p.Awake() && !p.Ready(false) && p.Ready(true));
            assert(!p.Consume(i+1,nonce,0,0,true));           // completed kind refused
            assert(!p.ConsumePending(i+2,nonce,true) && !p.ConsumePending(i+1,nonce+1,true) && !p.ConsumePending(i+1,nonce,false));
            assert(p.SleepPending(nonce) && !p.Ready(true) && p.Awake()); // repeated edge before admission
            assert(p.ConsumePending(i+1,nonce,true) && !p.Ready(true) && !p.Pending());
            assert(!p.ConsumePending(i+1,nonce,true));
        }
        assert(p.Remaining()==1 && !p.SleepPending(900));     // no slot left to resume into
        WakeSession m;                                        // pending then completed, one budget
        assert(m.SleepPending(10) && m.Awake() && m.ConsumePending(1,10,true));
        assert(m.Complete(11,4,100) && !m.SleepPending(12) && m.Sleep() && m.Serial()==2 && m.Awake());
        assert(!m.ConsumePending(2,11,true) && m.Consume(2,11,4,100,true));
        WakeSession s;assert(s.SleepPending(20) && s.Awake());s.Stop();
        assert(!s.Ready(true) && !s.ConsumePending(1,20,true) && !s.SleepPending(20) && !s.Awake());
        struct PendingAdapter{WakeSession wake;unsigned closed{},resumed{};
            bool PauseUnused(){return false;} bool ResumeUnused(){return false;}
            bool PauseSession(){return wake.SleepPending(42);} bool ResumeSession(){++resumed;return wake.Awake();}
            void Close(CaptureCloseReason){++closed;wake.Stop();}};
        PendingAdapter a;PowerLifecycle power;
        for(unsigned st:{2u,3u,4u}){power.Request(st,0,0,1000+st,a);power.Acknowledge(0,a);}
        assert(a.wake.Pending() && a.wake.Serial()==1 && !a.closed);
        power.Request(0,0,0,1200,a);assert(!a.wake.Ready(true));  // ACK barrier
        power.Acknowledge(0,a);assert(a.wake.Ready(true) && a.resumed==1 && !a.closed);
    }
    std::puts("PASS R3 pending wake: idempotent sleep edges, nonce binding, completed-kind refusal, budget reserve, mixed sessions, terminal Stop, ACK barrier");
#endif
}
