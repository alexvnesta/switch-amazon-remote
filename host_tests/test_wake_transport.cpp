// SPDX-License-Identifier: GPL-2.0-only
// Actual transport; synthetic OS/radio only. No hardware claim.
#define main LegacyAdapterMain
#include "test_libnx_input_adapter.cpp"
#undef main
namespace cb=amazon_remote::central_bond;
void Connected(LibnxSession &r,bool stale=false){
    f=FakeState{};f.system=f.native_requests=true;f.bond.generation=7;
    if(stale)f.now=30000;
    if(stale){const u8 old_up[]{0x52,0,0};Queue(4,0x1812,0x2a4d,0,old_up,sizeof old_up);}
    assert(!r.InitializeIdentityTest(f.target));
    if(stale)assert(f.events.empty() && Logged("drained=1") && !r.Buttons());
    assert(!r.PrepareKnownReconnect(f.now,f.now+60000));
    f.peer.phase=cb::PeerPhase::Ready;f.peer.device_type=2;f.peer.address_type=f.peer.existed=0;
    f.now+=100;assert(!r.Poll(f.now));
    assert(!r.ConfirmKnownReconnect(f.now,f.peer.capture_deadline_ms));
    f.context_active=true;assert(!Advance(r,200));
    f.bond.phase=cb::Phase::Ready;f.bond.encrypted=1;f.bond.key_bytes=16;f.bond.native_status=0;
    assert(!Advance(r) && r.Ready());
    f.wake.serial=1;f.wake.ready=1;f.wake.remaining=7;
    f.wake.nonce=f.bond.nonce;f.wake.connection=f.bond.connection;f.wake.owner_epoch=f.bond.owner_epoch;
}
int main(){
    for(unsigned mode=0;mode<9;++mode){
        Sandbox s;LibnxSession r(Log,true,true,true,true,true,true);Connected(r);
        const auto native=f.native_calls,disconnects=f.disconnects,dereg=f.deregisters;
        f.connected=f.context_active=false;
        f.now+=500;assert(r.Poll(f.now) && r.CompletedLinkLost() && !r.Buttons());
        assert(r.RetireForSleep() && !r.RetireForSleep());
        assert(!f.bond_cancels && f.native_calls==native && f.disconnects==disconnects && f.deregisters==dereg);
        assert(f.system_opens==f.system_closes && f.user_opens==f.user_closes);
        if(mode==1)++f.wake.nonce;
        if(mode==2)++f.wake.owner_epoch;
        if(mode==3)f.wake.ready=0;
        if(mode==4)f.wake.remaining=0;
        if(mode==5)f.wake_result=Invalid();
        if(mode==6)f.preconnect_variant=1; // target present in system view
        if(mode==7)f.connected=true; // target present in user view
        if(mode==8)f.wake.magic=0;
        bool ready{};auto rc=r.ResumeAfterSleep(f.now,ready);
        assert(!ready && !f.wake_calls);
        if(mode==1 || mode==2 || mode==4 || mode==8){assert(rc);continue;}
        assert(!rc);f.now+=249;assert(!r.ResumeAfterSleep(f.now,ready) && !ready && !f.wake_calls);
        ++f.now;rc=r.ResumeAfterSleep(f.now,ready);
        if(mode==5){assert(rc && !ready && f.wake_calls==1);continue;}
        if(mode==3 || mode==6 || mode==7){
            assert(!rc && !ready && !f.wake_calls);f.now+=10000;
            assert(r.ResumeAfterSleep(f.now,ready) && !ready && !f.wake_calls);continue;
        }
        assert(!rc && ready && f.wake_calls==1 && !f.bond_cancels);
        assert(f.system_opens==f.system_closes && f.user_opens==f.user_closes);
        assert(!r.Ready() && !r.LinkAttempted() && !r.KnownReconnectPrepared());
        // OS connection IDs can recycle. A new client/nonce/security lifetime,
        // not numeric novelty, permits reuse. Old queued input is discarded.
        Connected(r,true);assert(Logged("numeric connection id reused"));
        assert(!r.Buttons() && !r.CompletedLinkLost());
        r.Close();
    }
    std::puts("PASS A3 production transport: sleep retirement without stale-handle writes, dual-view absence, exact session binding, timeout and fresh-session reset; synthetic only");
}
