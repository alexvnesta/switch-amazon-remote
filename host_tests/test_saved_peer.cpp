// C1 production transport, SDK calls synthetic. No device/radio proof.
#define main LegacyAdapterMain
#include "test_libnx_input_adapter.cpp"
#undef main
#include "../tools/amazon-remote-module/control/saved_discovery_view.hpp"
namespace cb=amazon_remote::central_bond;
void Initialize(LibnxSession &remote){
    f=FakeState{};f.system=f.native_requests=true;f.bond.generation=7;
    assert(!remote.InitializeIdentityTest(f.target));
}
void Prepare(LibnxSession &remote){
    Initialize(remote);
    assert(!remote.PrepareKnownReconnect(f.now,f.now+60000));
    assert(f.peer_prepares==1 && !remote.KnownReconnectPrepared() && !f.connects);
    assert(!remote.Poll(f.now) && !remote.KnownReconnectPrepared());
    f.peer.phase=cb::PeerPhase::Ready;f.peer.device_type=2;f.peer.address_type=0;f.peer.existed=0;
    f.now+=100;
}
void Gates(unsigned v){
    Sandbox sandbox;LibnxSession remote(Log,true,true,true,true,true,true);Prepare(remote);
    switch(v){
    case 1:f.peer.phase=cb::PeerPhase::Failed;break;
    case 2:f.peer.phase=cb::PeerPhase::Cancelled;break;
    case 3:f.peer.magic=0;break;
    case 4:f.peer.version=2;break;
    case 5:f.peer.nonce++;break;
    case 6:f.peer.generation++;break;
    case 7:f.peer.capture_deadline_ms++;break;
    case 8:f.peer.device_type=1;break;
    case 9:f.peer.address_type=1;break;
    case 10:f.peer.existed=2;break;
    case 11:f.peer.result=1;break;
    case 12:f.peer_query_result=Invalid();break;
    case 13:f.peer.phase=cb::PeerPhase::Waiting;f.now+=3000;break;
    case 14:f.now=f.peer.capture_deadline_ms;break;
    }
    auto rc=remote.Poll(f.now);
    if(v)assert(rc && !remote.KnownReconnectPrepared());
    else assert(!rc && remote.KnownReconnectPrepared() && Logged("C1_NATIVE_PEER_READY"));
    assert(!f.connects && !f.scan_starts && !f.general_starts && !f.bond_begins && !f.pair_calls && !f.native_bonds);
    remote.Close();assert(f.bond_cancels==1);
}
void Connect(unsigned v){
    Sandbox sandbox;LibnxSession remote(Log,true,true,true,true,true,true);Prepare(remote);assert(!remote.Poll(f.now));
    if(v==1)f.peer.phase=cb::PeerPhase::Cancelled;
    if(v==2)f.preconnect_variant=1;
    if(v==3)f.preconnect_delay_ms=61000;
    const auto rc=remote.ConfirmKnownReconnect(f.now,f.peer.capture_deadline_ms);
    if(v){assert(rc && !f.connects);remote.Close();return;}
    assert(!rc && f.connects==1 && !f.bond_begins);
    assert(remote.ConfirmKnownReconnect(f.now,f.peer.capture_deadline_ms) && f.connects==1);
    f.context_active=true;assert(!Advance(remote,200));
    assert(f.bond_begins==1 && Logged("B1_BEGIN mode=STORED_KEY") && !remote.Ready());
    f.bond.phase=cb::Phase::Ready;f.bond.encrypted=1;f.bond.key_bytes=16;f.bond.native_status=0;
    assert(!Advance(remote) && remote.Ready());
    assert(!f.pair_calls && !f.native_bonds && !f.general_starts && !f.scan_starts);
    remote.Close();
}
void WaitingZL(){
    Sandbox sandbox;LibnxSession remote(Log,true,true,true,true,true,true);Initialize(remote);
    const auto deadline=f.now+60000;assert(!remote.PrepareKnownReconnect(f.now,deadline));
    assert(remote.ConfirmKnownReconnect(f.now,deadline) && !f.connects && !remote.LinkAttempted());
    assert(remote.StartLinkOnlyScan(f.now) && !f.general_starts && !f.scan_starts);
    assert(remote.Connect(true,true) && remote.Connect(false,false) && !f.connects && !f.scan_starts && !f.pair_calls);
    remote.Close();
}
void Views(){
    using namespace arsetupview;Input i{};i.fresh=i.idle_passed=true;cb::Status s{};s.store=cb::Store::Saved;
    auto view=[&]{return SelectSavedDiscovery(SelectDisconnectCheck(i),i,0,s,true);};
    assert(std::string(view().status)=="SAVED BOND READY");
    i.state=1;assert(std::string(view().status)=="PREPARING BLUETOOTH");
    i.link_ready=true;assert(std::string(view().status)=="READY TO RECONNECT");
    i.result=1;assert(std::string(view().status)=="SETUP ERROR");
}
#ifdef ARMODULE_NATIVE_HCI_C2
void Hci(unsigned variant){
    Sandbox sandbox;LibnxSession remote(Log,true,true,true,true,true,true);Prepare(remote);assert(!remote.Poll(f.now));
    assert(!remote.ConfirmKnownReconnect(f.now,f.peer.capture_deadline_ms) && f.connects==1);
    f.hci.window_nonce=f.peer.nonce;f.hci.capture_start_ms=f.peer.started_ms;f.hci.count=1;
    f.hci.entries[0]={f.now,12,0};
    if(variant==1)f.hci.window_nonce++;
    if(variant==2)f.hci.count=5;
    if(variant==3)f.hci.entries[0].boot_ms=f.now+10000;
    if(variant==4)f.hci.entries[0].status=256;
    if(variant==5)f.hci_query_result=Invalid();
    if(variant==6)f.hci.entries[0].own_address_type=4;
    if(variant==7){f.hci_query_delay_ms=10;f.hci.entries[0].boot_ms=f.now+25;}
    f.context_active=true;
    assert(!Advance(remote,200));
    assert(Logged(variant && variant!=7?"C2_HCI_TRACE_UNAVAILABLE":"C2_HCI_COMMAND_STATUS"));
    // A diagnostic rejection is NOT a second connect, pair or automatic retry.
    assert(f.connects==1 && !f.pair_calls && !f.native_bonds && !f.scan_starts && !f.general_starts);
    remote.Close();assert(Logged("C2_HCI_SUMMARY"));
}
#endif
int main(){for(unsigned v=0;v<15;++v)Gates(v);for(unsigned v=0;v<4;++v)Connect(v);WaitingZL();Views();
#ifdef ARMODULE_NATIVE_HCI_C2
    for(unsigned v=0;v<8;++v)Hci(v);
#endif
    std::cout<<"PASS C1 production transport: native readiness before physical ZL, stale/failure refusal, no scan or pairing fallback, existing stored-key/input checks. Synthetic only.\n";}
