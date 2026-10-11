// SPDX-License-Identifier: GPL-2.0-only
// R3 actual transport: an unfinished attempt retires at sleep. Synthetic OS/radio only.
#define main LegacyAdapterMain
#include "test_libnx_input_adapter.cpp"
#undef main
namespace cb=amazon_remote::central_bond;
// Saved peer prepared and one connection requested that never completes.
void Pending(LibnxSession &r){
    f=FakeState{};f.system=f.native_requests=true;f.bond.generation=7;
    assert(!r.InitializeIdentityTest(f.target));
    assert(!r.PrepareKnownReconnect(f.now,f.now+60000));
    f.peer.phase=cb::PeerPhase::Ready;f.peer.device_type=2;f.peer.address_type=f.peer.existed=0;
    f.now+=100;assert(!r.Poll(f.now));
    assert(!r.ConfirmKnownReconnect(f.now,f.peer.capture_deadline_ms));
    f.now+=200;assert(!r.Poll(f.now) && !r.Ready() && r.LinkAttempted());
    // The fake links instantly; system sleep teardown removes it before retirement.
    f.connected=f.context_active=false;
    f.wake.serial=1;f.wake.ready=1;f.wake.remaining=6;f.wake.reserved=cb::WakeKindPending;
    f.wake.nonce=f.peer.nonce;f.wake.connection=0;f.wake.owner_epoch=0;
}
void Connected(LibnxSession &r,std::uint64_t now=0){
    f=FakeState{};f.system=f.native_requests=true;f.bond.generation=7;
    if(now)f.now=now; // a fresh session after wake never runs the clock backwards
    assert(!r.InitializeIdentityTest(f.target));
    assert(!r.PrepareKnownReconnect(f.now,f.now+60000));
    f.peer.phase=cb::PeerPhase::Ready;f.peer.device_type=2;f.peer.address_type=f.peer.existed=0;
    f.now+=100;assert(!r.Poll(f.now));
    assert(!r.ConfirmKnownReconnect(f.now,f.peer.capture_deadline_ms));
    f.context_active=true;assert(!Advance(r,200));
    f.bond.phase=cb::Phase::Ready;f.bond.encrypted=1;f.bond.key_bytes=16;f.bond.native_status=0;
    assert(!Advance(r) && r.Ready());
}
int main(){
    // Mode 0 uses the realistic companion (deferred reload, kind reverts to 0);
    // mode 9 the immediate reload. Both must reach exactly one wake command.
    for(unsigned mode=0;mode<10;++mode){
        Sandbox s;LibnxSession r(Log,true,true,true,true,true,true);Pending(r);
        if(mode==0)f.deferred_reload=true;
        const auto native=f.native_calls,disconnects=f.disconnects,dereg=f.deregisters;
        assert(!r.RetireForSleep());                         // not a READY session
        assert(r.RetirePendingForSleep() && !r.RetirePendingForSleep());
        // Sleep retirement makes no radio call and never sends companion Cancel.
        assert(!f.bond_cancels && f.native_calls==native && f.disconnects==disconnects && f.deregisters==dereg);
        assert(f.system_opens==f.system_closes && f.user_opens==f.user_closes);
        if(mode==1)++f.wake.nonce;
        if(mode==2)f.wake.reserved=0;                        // companion reports completed kind
        if(mode==3)f.wake.connection=4;
        if(mode==4)f.wake.ready=0;
        if(mode==5)f.wake_result=Invalid();
        if(mode==6)f.preconnect_variant=1;                   // target present in system view
        if(mode==7)f.connected=true;                         // target present in user view
        if(mode==8)f.wake.owner_epoch=9;
        bool ready{};auto rc=r.ResumeAfterSleep(f.now,ready);
        assert(!ready && !f.wake_calls);
        if(mode==1 || mode==2 || mode==3 || mode==8){assert(rc);continue;}
        assert(!rc);f.now+=249;assert(!r.ResumeAfterSleep(f.now,ready) && !ready && !f.wake_calls);
        ++f.now;rc=r.ResumeAfterSleep(f.now,ready);
        if(mode==5){assert(rc && !ready && f.wake_calls==1);continue;}
        if(mode==4 || mode==6 || mode==7){
            assert(!rc && !ready && !f.wake_calls);f.now+=10000;
            assert(r.ResumeAfterSleep(f.now,ready) && !ready && !f.wake_calls);continue;
        }
        if(mode==0){
            assert(!rc && !ready && f.wake_calls==1 && !f.wake.reserved);
            for(unsigned k=0;k<4 && !ready;++k){f.now+=250;rc=r.ResumeAfterSleep(f.now,ready);assert(!rc);}
        }
        assert(!rc);assert(ready);assert(f.wake_calls==1);assert(!f.bond_cancels);assert(Logged("SETUP_R3 unfinished attempt admitted"));
        assert(f.system_opens==f.system_closes && f.user_opens==f.user_closes);
        assert(!r.Ready() && !r.LinkAttempted() && !r.KnownReconnectPrepared());
        Connected(r,f.now+1000);                             // fresh session completes after wake
        r.Close();
    }
    {   // A READY session never takes the pending path.
        Sandbox s;LibnxSession r(Log,true,true,true,true,true,true);Connected(r);
        assert(!r.RetirePendingForSleep());r.Close();
    }
    {   // Nothing prepared yet: no pending retirement.
        Sandbox s;LibnxSession r(Log,true,true,true,true,true,true);
        f=FakeState{};f.system=f.native_requests=true;f.bond.generation=7;
        assert(!r.InitializeIdentityTest(f.target) && !r.RetirePendingForSleep());r.Close();
    }
    std::puts("PASS R3 production transport: unfinished-attempt retirement without radio calls or Cancel, peer-nonce/kind binding, dual-view absence, timeout and fresh-session completion; synthetic only");
}
