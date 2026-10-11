// SPDX-License-Identifier: GPL-2.0-only
// Compile the ACTUAL LibnxSession implementation against synthetic SDK calls.
// This verifies orchestration, not radio behavior or physical reliability.
#define main LegacyAdapterMain
#include "test_libnx_input_adapter.cpp"
#undef main
namespace cb=amazon_remote::central_bond;
void BeginRetryTest(LibnxSession &r){
    f=FakeState{};f.system=f.native_requests=true;f.bond.generation=7;
    assert(!r.InitializeIdentityTest(f.target));assert(!r.PrepareKnownReconnect(f.now,f.now+60000));
    f.peer.phase=cb::PeerPhase::Ready;f.peer.device_type=2;f.peer.address_type=f.peer.existed=0;
    f.now+=100;assert(!r.Poll(f.now));assert(!r.ConfirmKnownReconnect(f.now,f.peer.capture_deadline_ms));
    assert(f.connects==1 && f.peer_prepares==1 && !f.bond_begins);
}
void EarlyFailure(){
    f.hci.window_nonce=f.peer.nonce;f.hci.capture_start_ms=f.peer.started_ms;
    f.hci.entries[f.hci.count++]={f.now,0,0};
    f.connection_evidence.events[f.connection_evidence.target_count++]={f.now+1,0,4,0,4,0,0};
    f.connection_evidence.events[f.connection_evidence.target_count++]={f.now+97,0x1a,4,2,4,0x3e,0};
    f.now+=200;f.connected=f.discovery=false;
}
int main(){
    for(unsigned mode=0;mode<10;++mode){
        Sandbox s;LibnxSession r(Log,true,true,true,true,true,true);BeginRetryTest(r);
        if(mode==6){f.connected=false;f.now+=30000;assert(r.Poll(f.now));r.Close();assert(f.connects==1);continue;}
        EarlyFailure();
        if(mode==1)f.connected=true;
        if(mode==2)f.preconnect_variant=1;
        if(mode==3)++f.connection_evidence.events[1].client_if;
        if(mode==4)f.evidence_result=Invalid();
        auto rc=r.Poll(f.now);
        if(mode>=1 && mode<=4){assert(rc && f.connects==1);r.Close();continue;}
        assert(!rc && f.connects==1 && Logged("RETRY_A5_EARLY_FAILURE") && !f.bond_begins);
        if(mode==5)f.peer.phase=cb::PeerPhase::Cancelled;
        if(mode==7)f.inject_retry_callback=true;
        if(mode==8){r.BeginOwnedDisconnect(f.now);r.Close();assert(f.connects==1 && !f.owned_disconnects);continue;}
        for(unsigned i=0;i<20 && !rc && f.connects==1;++i){f.now+=100;rc=r.Poll(f.now);}
        if(mode==5 || mode==7){assert(rc && f.connects==1);r.Close();continue;}
        assert(!rc && f.connects==2 && f.peer_prepares==1 && !f.bond_begins);
        if(mode==9){
            EarlyFailure();assert(!r.Poll(f.now));
            for(unsigned i=0;i<20 && !rc && f.connects==2;++i){f.now+=100;rc=r.Poll(f.now);}
            assert(!rc && f.connects==3);EarlyFailure();assert(!r.Poll(f.now));
            for(unsigned i=0;i<20 && !rc;++i){f.now+=100;rc=r.Poll(f.now);}
            assert(rc && f.connects==3);assert(!r.BeginOwnedDisconnect(f.now));
            assert(r.GetDisconnectState()==DisconnectState::Confirmed && !f.owned_disconnects && !f.user_disconnects);
            r.Close();assert(!f.bond_begins);continue;
        }
        // The retry still has to traverse the normal fingerprint and saved-key
        // encryption gate before subscriptions or buttons can become ready.
        f.hci.entries[f.hci.count++]={f.now,0,0};
        f.connection_evidence.events[f.connection_evidence.target_count++]={f.now,0,4,0,4,0,0};
        f.context_active=true;assert(!Advance(r,200));
        assert(f.bond_begins==1 && !r.Ready());
        f.bond.phase=cb::Phase::Ready;f.bond.encrypted=1;f.bond.key_bytes=16;f.bond.native_status=0;
        assert(!Advance(r) && r.Ready() && r.FingerprintObserved());
#ifdef ARMODULE_LATENCY_A6
        assert(!r.InputTiming().sequence); // Setup reads are not input.
        const u8 press[]{0x52,0,0},release[]{0,0,0};
        f.now+=100;const auto pressed_at=f.now;
        Queue(4,0x1812,0x2a4d,0,press,sizeof press);
        assert(!r.Poll(f.now) && r.Buttons()==Up);
        assert(r.InputTiming().sequence==1 && r.InputTiming().received_ms==pressed_at &&
            r.InputTiming().decoded_ms==pressed_at && r.InputTiming().buttons==Up);
        f.now+=100;Queue(4,0x1812,0x2a4d,0,release,sizeof release);
        assert(!r.Poll(f.now) && !r.Buttons());
        assert(r.InputTiming().sequence==2 && r.InputTiming().received_ms==f.now && !r.InputTiming().buttons);
        const u8 home[]{0x23,2,0,0},consumer_release[]{0,0,0,0};
        f.now+=100;Queue(4,0x1812,0x2a4d,0,home,sizeof home);
        assert(!r.Poll(f.now) && r.Buttons()==Home && r.InputTiming().sequence==3);
        assert(r.InputTiming().buttons==Home && r.InputTiming().received_ms==f.now &&
            r.InputTiming().decoded_ms==f.now);
        // A keyboard release must not swallow Consumer Home.
        f.now+=100;Queue(4,0x1812,0x2a4d,0,release,sizeof release);
        assert(!r.Poll(f.now) && r.Buttons()==Home && r.InputTiming().sequence==4);
        assert(r.InputTiming().buttons==Home && r.InputTiming().received_ms==f.now &&
            r.InputTiming().decoded_ms==f.now);
        f.now+=100;Queue(4,0x1812,0x2a4d,0,consumer_release,sizeof consumer_release);
        assert(!r.Poll(f.now) && !r.Buttons() && r.InputTiming().sequence==5);
        assert(!r.InputTiming().buttons && r.InputTiming().received_ms==f.now &&
            r.InputTiming().decoded_ms==f.now);
        // Rejected vendor notification must not become an accepted-input sample.
        f.now+=100;Queue(4,0x180f,0x2a19,0,press,sizeof press);
        assert(r.Poll(f.now) && r.InputTiming().sequence==5);
#endif
        assert(!f.pair_calls && !f.native_bonds && !f.scan_starts && !f.general_starts && f.bond.generation==7);
        r.Close();assert(f.connects==2 && f.peer_prepares==1);
    }
    std::puts("PASS A5 actual transport: early failure/retry/encryption, both absent views, refusal, Stop, timeout, callback race, three-attempt bound and command-free failed-link cleanup; synthetic only");
#ifdef ARMODULE_LATENCY_A6
    std::puts("PASS A6 accepted-input timing: setup excluded, press/release timestamped, rejected vendor input excluded; no extra radio operations");
#endif
}
