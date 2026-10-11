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
}
