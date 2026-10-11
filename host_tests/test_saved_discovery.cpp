// Compile the real transport with the deployed central-bond flags, not just
// generic reconnect flags. Reuse its SDK fixture without running its legacy main.
#define main LegacyAdapterMain
#include "test_libnx_input_adapter.cpp"
#undef main
#include "../tools/amazon-remote-module/control/saved_discovery_view.hpp"
namespace cb=amazon_remote::central_bond;
void SavedScan(unsigned variant) {
    Sandbox sandbox;f=FakeState{};f.system=f.native_requests=true;
    LibnxSession remote(Log,true,true,true,true,true,true);
    assert(!remote.InitializeIdentityTest(f.target) && remote.HasSavedBond());
    assert(!remote.StartLinkOnlyScan(f.now) && remote.ScanActive());
    assert(f.general_starts==1 && !f.smart_starts && !f.connects && !f.bond_begins);
    assert(!remote.Poll(f.now) && !f.connects && !f.bond_begins);
    auto sighting=Sighting();auto deadline=f.now+60000;
    switch(variant){
        case 1:sighting={};break;
        case 2:sighting.captured_boot_ms-=3001;break;
        case 3:sighting.captured_boot_ms++;break;
        case 4:sighting.raw[7]^=1;break;
        case 5:sighting.raw[15]^=1;break;
        case 6:sighting.raw[323]=11;break;
        case 7:deadline=f.now;break;
        case 8:f.marker_log_delay_ms=251;break;
        case 9:f.preconnect_variant=1;break;
        case 10:{std::ofstream marker("sdmc:/config/amazon-remote/link-test-pending.txt");marker<<"preserved";break;}
    }
    const auto rc=remote.ConfirmObservedLink(sighting,f.now,deadline);
    assert(f.connects==unsigned(variant==0) && !f.bond_begins);
    if(!variant){assert(!rc && remote.ConfirmObservedLink(sighting,f.now,deadline));assert(f.connects==1);}
    remote.Close();
    assert(!f.pair_calls && !f.native_bonds && !f.bond_begins && !f.writes && !f.security_reads);
    assert(f.bond_opens==1 && f.bond_closes==1 && f.general_stops==1);
}
void StoreAdmission(unsigned variant) {
    Sandbox sandbox;f=FakeState{};f.system=f.native_requests=true;
    switch(variant){
        case 0:f.bond.store=cb::Store::Empty;break;
        case 1:f.bond.store=cb::Store::Unknown;break;
        case 2:f.bond.store=cb::Store::Error;break;
        case 3:f.bond.magic=0;break;
        case 4:f.bond.version=2;break;
        case 5:f.bond_query_result=Invalid();break;
    }
    LibnxSession remote(Log,true,true,true,true,true,true);
    assert(remote.InitializeIdentityTest(f.target));
    assert(!f.general_starts && !f.connects && !f.bond_begins && !f.pair_calls && !f.native_bonds);
    assert(f.bond_opens==1 && f.bond_closes==1);
}
void SecurityGate(unsigned variant) {
    Sandbox sandbox;f=FakeState{};f.system=f.native_requests=true;
    LibnxSession remote(Log,true,true,true,true,true,true);Start(remote);
    assert(!f.bond_begins); // Accepted request is not a connection/security completion.
    f.context_active=true; // Both fixture views now expose this new connection.
    if(variant==1)f.bond.store=cb::Store::Empty;
    const auto rc=Advance(remote,200);
    assert(!remote.Ready() && !f.security_reads && !f.writes);
    if(variant==1){assert(rc && !f.bond_begins);}
    else{
        assert(!rc && f.bond_begins==1 && Logged("B1_BEGIN mode=STORED_KEY"));
        assert(!Logged("ENROLL_ONCE"));
        f.bond.phase=variant==2?cb::Phase::Failed:cb::Phase::Ready;
        f.bond.encrypted=1;f.bond.key_bytes=16;f.bond.native_status=0;
        if(variant==3)f.bond.nonce++;
        const auto completed=Advance(remote);
        if(variant==2 || variant==3)assert(completed && !remote.Ready() && !f.writes);
        else assert(!completed && remote.Ready() && f.writes==2 && f.security_reads==1);
        assert(f.bond_begins==1);
    }
    remote.Close();assert(!f.pair_calls && !f.native_bonds && f.bond_closes==1);
}
void NoHandle() {
    Sandbox sandbox;f=FakeState{};f.system=f.native_requests=true;
    LibnxSession remote(Log,true,true,true,true,true,true);Start(remote);
    f.connected=f.discovery=false; // IPC accepted, but no connection established.
    assert(Advance(remote,1600) && !remote.LinkConfirmed() && !remote.Ready());
    assert(f.connects==1 && !f.bond_begins && !f.security_reads && !f.writes);
    remote.Close();assert(!f.pair_calls && !f.native_bonds);
}
void Views() {
    using namespace arsetupview;
    Input i{};i.fresh=i.idle_passed=true;
    cb::Status bond{};bond.store=cb::Store::Saved;
    auto view=[&]{return SelectSavedDiscovery(SelectDisconnectCheck(i),i,0,bond);};
    assert(std::string(view().status)=="SAVED BOND READY");
    i.state=1;assert(std::string(view().status)=="SEARCHING SAVED REMOTE");
    i.link_ready=true;assert(std::string(view().status)=="REMOTE SEEN");
    i.state=2;assert(std::string(view().status)=="CONNECTING TO REMOTE");
    bond.phase=cb::Phase::Encrypting;assert(std::string(view().status)=="SECURING SAVED LINK");
    i.result=1;assert(std::string(view().status)=="SETUP ERROR");i.result=0;
    i.state=6;assert(std::string(view().status)=="DISCONNECTING");
    i.state=7;i.link_ready=false;assert(std::string(view().status)=="DISCONNECTED");
    i.state=0;bond.store=cb::Store::Empty;assert(std::string(view().status)=="SAVED BOND NOT AVAILABLE");
    const View power{"POWER NOT READY","Exit","Preserved",Tone::Error};
    assert(SelectSavedDiscovery(power,i,0,bond).status==power.status);
}
int main(){
    for(unsigned v=0;v<11;++v)SavedScan(v);
    for(unsigned v=0;v<6;++v)StoreAdmission(v);
    for(unsigned v=0;v<4;++v)SecurityGate(v);
    NoHandle();
    Views();
    std::cout<<"PASS B3: actual saved-mode scan and freshness gate, no address-only shortcut, metadata admission, one stored-key request after link, encryption gate and UI precedence. NOT hardware proof.\n";
}
