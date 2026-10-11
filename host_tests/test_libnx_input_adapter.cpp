#define _DARWIN_C_SOURCE 1
#include "../mc_mitm/source/amazon_remote/libnx_session.hpp"
#include "../mc_mitm/source/amazon_remote/k7q3m7_descriptor.hpp"
#include "../mc_mitm/source/amazon_remote/cached_characteristic_getter.hpp"
#include "../mc_mitm/source/amazon_remote/service_uuid_census.hpp"
#include "../mc_mitm/source/amazon_remote/connect_dispatch.hpp"
#ifdef ARMODULE_CENTRAL_BOND_B1
#include "../mc_mitm/source/amazon_remote/central_bond_wire.hpp"
#endif
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <fstream>
#include <unistd.h>
using namespace amazon_remote;
namespace {
enum Scenario { Good, ZeroProperties, DuplicateInstances, SecurityError, SecurityTimeout, TruncatedMetadata, BadDescriptor, BadCccCompletion, PreBindingDelay, PostBindingDelay, PreMetadataDelay,
    DriverRecovered, DriverOpenError, DriverReadError, DriverWrongUuid, DriverRepeatedInstance, DriverUnknownInstance, DriverConflict, DriverBindingDelay, DriverReassigned, DriverZeroSelected, DriverDeviceShape };
struct FakeState {
#ifdef ARMODULE_HID_INFO_H1
    unsigned info_mode{},info_reads{};
    u8 info_flags=3;
#endif
#ifdef ARMODULE_RETRY_A5
    ConnectionEvidenceWire connection_evidence;
    Result evidence_result{};
    bool inject_retry_callback{};
#endif
#ifdef ARMODULE_CENTRAL_BOND_B1
    central_bond::Status bond{central_bond::Magic,central_bond::Version,central_bond::Store::Saved};
    unsigned bond_opens{},bond_closes{},bond_queries{},bond_begins{},bond_cancels{};
    Result bond_query_result{};
#ifdef ARMODULE_WAKE_A3
    central_bond::WakeStatus wake{};
    unsigned wake_calls{};Result wake_result{};
    // Opt-in realistic companion: after WakeCommand the saved record reloads
    // over later polls and WakeStatus reverts to an idle, kind-0 record.
    bool deferred_reload{};unsigned reload_polls{};
#endif
#ifdef ARMODULE_NATIVE_PEER_C1
    central_bond::PeerStatus peer{};
    central_bond::HciTrace hci{central_bond::HciTraceMagic,1,0,0,1};
    unsigned hci_reads{},hci_query_delay_ms{};Result hci_query_result{};
    unsigned peer_prepares{},peer_queries{};
    Result peer_prepare_result{},peer_query_result{};
#endif
#endif
    Scenario scenario=Good; u64 now=10000;
    BtdrvAddress target{{0x02,0xab,0xcd,0x12,0x34,0x56}}, address=target;
    bool connected=false, discovery=false, scan=false, security_requested=false, delayed=false;
    unsigned connects=0, characteristic_reads=0, security_reads=0, descriptor_reads=0, descriptor_getters=0;
    unsigned scan_starts=0,scan_stops=0,scan_getters=0;
    unsigned general_starts{},smart_starts{},general_stops{},smart_stops{};
    Result general_start_result{},general_getter_result{},smart_getter_result{},general_stop_result{};
    u8 general_count{};
    unsigned registers=0, writes=0, deregisters=0, disconnects=0, pre_metadata=0, post_metadata=0;
    unsigned driver_opens=0,driver_closes=0,driver_queries=0;
    bool system=false,fixture=false,native_requests=false,drop_native_events=false;
    Result native_result{};
    unsigned native_fail_command{};
    unsigned native_calls{};
    Result system_open_result{},system_mtu_result{};
    unsigned system_opens{},system_closes{},system_mtu_calls{},system_getters{};
    unsigned pair_variant{},pair_calls{},paired_getters{},intent_calls{};bool listed{},system_enrollment{};
    unsigned user_opens{},user_closes{},sys_opens{},sys_closes{},flag_queries{},system_connection_queries{};
    bool context_active{};unsigned context_variant{};
    unsigned preconnect_variant{},preconnect_system_queries{};
    int preconnect_delay_ms{};
    unsigned physical_log_delay_ms{},marker_log_delay_ms{};
    unsigned census_variant{};u64 cached_aruid=0xffffffffffffffffULL;
    u32 firmware=MAKEHOSVERSION(22,5,0);u8 bond_enabled=1;Result enabled_result{},native_bond_result{};
    unsigned native_bonds{},enabled_calls{},native_intents{},owner_change{};bool persist_native=true;
    bool cleanup_test{},user_present{},system_present{},cleanup_unrelated{};
    Result disconnect_result{},cleanup_user_result{},cleanup_system_result{};
    u32 cleanup_handle=4;unsigned cleanup_variant{},owned_disconnects{},cleanup_queries{};
    unsigned cleanup_user_calls{},cleanup_user_fail_at{};
    unsigned cleanup_system_calls{},cleanup_final_variant{},user_disconnects{};
    std::deque<BtdrvBleEventInfo> events; std::vector<std::string> logs;
} f;
BtdrvGattAttributeUuid U(u16 value) { BtdrvGattAttributeUuid u{}; u.size=2; u.uuid[0]=value&255; u.uuid[1]=value>>8; return u; }
u16 Short(const BtdrvGattAttributeUuid &u) { return u.size==2 ? u.uuid[0]|u16(u.uuid[1])<<8 : 0; }
Result Invalid() { return MAKERESULT(Module_Libnx,LibnxError_BadInput); }
Result Timeout() { return MAKERESULT(Module_Kernel,KernelError_TimedOut); }
void Log(const char *line) {
    f.logs.emplace_back(line);
#ifdef ARMODULE_RETRY_A5
    if(f.inject_retry_callback && std::string(line).starts_with("RETRY_A5_RETRY:"))
        f.connection_evidence.events[f.connection_evidence.target_count++]={f.now,0,5,0,4,0,0};
#endif
    if (std::string(line).starts_with("Physical ZL sighting gate")) f.now+=f.physical_log_delay_ms;
    if (std::string(line).starts_with("Persistent pending marker retained")) f.now+=f.marker_log_delay_ms;
}
bool Logged(const char *text) { return std::any_of(f.logs.begin(),f.logs.end(),[&](const auto &s) { return s.find(text)!=std::string::npos; }); }
void AssertRawChunks() {
    unsigned chunks=0;
    for(const auto &line:f.logs) if(line.starts_with("HID_METADATA_RAW ")) {
        const auto offset=line.find("offset="), bytes=line.find("bytes=");
        assert(offset!=std::string::npos && bytes!=std::string::npos);
        const auto hex=line.substr(bytes+6);
        assert(hex.size()==(line.substr(offset,9)=="offset=0 " ? 64 : 8));
        assert(std::all_of(hex.begin(),hex.end(),[](char c) { return (c>='0' && c<='9') || (c>='a' && c<='f'); }));
        ++chunks;
    }
    assert(chunks==(f.scenario==DriverDeviceShape ? 36U : 16U)); // Two36-byte snapshots, two chunks per record.
}
void Queue(u8 type,u16 service,u16 characteristic,u16 descriptor,const u8 *data,std::size_t size,u32 result=0,u32 connection=4) {
    BtdrvBleEventInfo e{}; auto &n=e.client_notify;
    n.result=result; n.conn_id=connection; n.type=type;
    n.serv_uuid=U(service); n.char_uuid=U(characteristic); n.desc_uuid=U(descriptor);
    assert(size<=sizeof n.data); n.size=size; if(size) std::copy_n(data,size,n.data);
    f.events.push_back(e);
}
struct Sandbox {
    std::filesystem::path previous=std::filesystem::current_path(), path;
    Sandbox() {
        char pattern[]="/tmp/amazon-input-host-XXXXXX"; auto *created=mkdtemp(pattern); assert(created); path=created;
        std::filesystem::current_path(path); std::filesystem::create_directories("sdmc:/config/amazon-remote");
#ifdef ARMODULE_AUTOMATIC_A1
        std::filesystem::create_directories("sdmc:/config/amazon-remote-auto-a1");
#endif
    }
    ~Sandbox() {
        std::filesystem::current_path(previous);
        assert(path.string().starts_with("/tmp/amazon-input-host-") && path!=previous);
        std::filesystem::remove_all(path);
    }
};
SightingWire Sighting() {
    SightingWire wire{}; wire.magic=SightingMagic; wire.version=SightingVersion; wire.captured_boot_ms=f.now;
    wire.raw[4]=2; std::copy_n(f.target.address,6,wire.raw.begin()+7); wire.raw[323]=2;
    wire.raw[13]=3; wire.raw[14]=3; wire.raw[15]=0x12; wire.raw[16]=0x18;
    wire.raw[44]=7; wire.raw[45]=0xff;
    const u8 manufacturer[]{0x71,1,4,0x27,2,0x7d}; std::copy_n(manufacturer,6,wire.raw.begin()+46);
    return wire;
}
void Start(LibnxSession &remote) {
    assert(!remote.InitializeIdentityTest(f.target));
    assert(!remote.StartLinkOnlyScan(f.now));
    assert(!remote.ConfirmObservedLink(Sighting(),f.now,f.now+60000));
    assert(f.connects==1);
    assert(std::filesystem::exists("sdmc:/config/amazon-remote/link-test-pending.txt"));
}
Result Advance(LibnxSession &remote,unsigned polls=200) {
    Result rc=0;
    for(unsigned i=0;i<polls && !rc && !remote.Ready();++i) { f.now+=20; rc=remote.Poll(f.now); }
    return rc;
}
void GoodFlow() {
    Sandbox sandbox; f=FakeState{};
    LibnxSession remote(Log,true,true); Start(remote); assert(!Advance(remote));
    assert(remote.Ready() && remote.FingerprintObserved() && remote.SecurityReadObserved());
    assert(f.pre_metadata==1 && f.post_metadata==1 && f.security_reads==1 && f.characteristic_reads==3);
    assert(f.descriptor_reads==3 && f.registers==2 && f.writes==2 && !f.disconnects);
    assert(Logged("before_security") && Logged("after_security") && Logged("properties=00") && Logged("properties=12"));
    assert(Logged("HID_METADATA_RAW before_security index=0 offset=0 bytes="));
    assert(Logged("HID_METADATA_RAW before_security index=0 offset=32 bytes="));
    assert(!f.driver_opens && !f.driver_queries); // Nonzero BTM cache needs no new driver session.
    AssertRawChunks();
    const u8 keyboard[]{0x52,0x58,0}, released[]{0,0,0}, consumer[]{0x23,2,0,0}, consumer_released[]{0,0,0,0};
    Queue(4,0x1812,0x2a4d,0,keyboard,sizeof keyboard); assert(!Advance(remote,1));
    // Advance stops at Ready; explicit runtime polling is needed for reports.
    f.now+=20; assert(!remote.Poll(f.now)); assert((remote.Buttons()&(Up|Confirm))==(Up|Confirm));
    Queue(4,0x1812,0x2a4d,0,consumer,sizeof consumer); f.now+=20; assert(!remote.Poll(f.now)); assert(remote.Buttons()&Home);
    Queue(4,0x1812,0x2a4d,0,released,sizeof released); f.now+=20; assert(!remote.Poll(f.now)); assert(!(remote.Buttons()&(Up|Confirm)) && (remote.Buttons()&Home));
    Queue(4,0x1812,0x2a4d,0,consumer_released,sizeof consumer_released); f.now+=20;
    assert(!remote.Poll(f.now) && !remote.Buttons()); // Actual report-2 release, not Close masking a stuck Home.
    Queue(4,0x1812,0x2a4d,0,consumer,sizeof consumer); f.now+=20;
    assert(!remote.Poll(f.now) && remote.Buttons()==Home);
    assert(Logged("buttons") && Logged("INPUT_SUBSCRIPTIONS_READY"));
    remote.Close(); assert(!remote.Buttons() && f.deregisters==2 && !f.disconnects);
    assert(std::filesystem::exists("sdmc:/config/amazon-remote/link-test-pending.txt"));
}
void SustainedFlow(unsigned variant) {
    Sandbox sandbox;f=FakeState{};f.system=true;f.native_requests=true;
    if (variant==1) f.scenario=PreMetadataDelay;
    if (variant==2) f.scenario=DriverBindingDelay;
    LibnxSession remote(Log,true,true,true,true,true);Start(remote);
    if (variant==1 || variant==2) {
        assert(Advance(remote) && !remote.Ready() && !f.registers && !f.writes);
        remote.Close();return;
    }
    assert(!Advance(remote) && remote.Ready());const auto ready=f.now;
    f.logs.clear();
    unsigned polls=0;
    for (;f.now<ready+1800000-20;++polls) {
        f.now+=20;assert(!remote.Poll(f.now));
    }
    unsigned health_logs=0;
    for (const auto &line:f.logs) if (line.starts_with("API=btmuBleGetConnectionState(link-only)")) ++health_logs;
    assert(polls>89000 && health_logs<=61 && health_logs>=59);
    assert(f.connects==1 && f.security_reads==1 && f.writes==2);
    const u8 keyboard[]{0x52,0,0};
    Queue(4,0x1812,0x2a4d,0,keyboard,sizeof keyboard);
    assert(!remote.Poll(f.now) && remote.Buttons()==Up);
    f.now=ready+1800000;assert(remote.Poll(f.now));
    assert(Logged("INPUT_STOP_BUDGET"));
    remote.Close();assert(f.deregisters==2 && !f.disconnects && !remote.Buttons());
}
bool EnrollmentIntentPersist(const EnrollmentRecord &r) {
    ++f.intent_calls;
    assert(r.state==unsigned(EnrollmentState::Prepared) && !r.request_issued && r.instance==42 &&
           r.connection==4 && r.requested_ms && !r.before_result && !r.before_matches &&
           r.checksum==EnrollmentChecksum(r) && !f.pair_calls);
    if(f.pair_variant==10)f.address.address[0]^=1;
    if(f.pair_variant==11)f.now+=2100;
    return f.pair_variant!=9;
}
#ifndef ARMODULE_RECONNECT_CHECK
void EnrollmentFlow(unsigned variant,bool system_enrollment=false) {
    Sandbox sandbox;f=FakeState{};f.system=f.native_requests=true;f.pair_variant=variant;
    f.system_enrollment=system_enrollment;
    LibnxSession remote(Log,true,true,true,true,variant!=1,system_enrollment);Start(remote);
    if(variant!=2)assert(!Advance(remote) && remote.Ready());
    assert(!f.pair_calls && !f.paired_getters && !f.intent_calls); // READY itself never pairs.
    if(variant==3){const u8 keyboard[]{0x52,0,0};Queue(4,0x1812,0x2a4d,0,keyboard,sizeof keyboard);f.now+=20;assert(!remote.Poll(f.now));}
    if(variant==4)f.address.address[0]^=1;
    if(variant==5)f.listed=true;
    if(variant==19)f.now+=1800000;
    const auto rc=remote.EnrollOnce(variant==21?0:42,variant==22?nullptr:EnrollmentIntentPersist);
    const bool issued=variant==0 || (variant>=12 && variant<=18);
    assert(f.pair_calls==unsigned(issued));
    if(!issued && variant!=5)assert(rc && remote.Enrollment().state==unsigned(EnrollmentState::Refused));
    else if(variant==5)assert(!rc && remote.Enrollment().state==unsigned(EnrollmentState::AlreadyListed) && !f.intent_calls);
    else if(variant==12)assert(rc==0x5528f && remote.Enrollment().state==unsigned(EnrollmentState::Failed));
    else {
        assert(!rc && remote.Enrollment().state==unsigned(EnrollmentState::Accepted));
        if(variant==13)f.now=remote.Enrollment().deadline_ms;
        else if(variant==15)f.address.address[0]^=1;
        else if(variant==16)remote.Close();
        else if(variant==18)f.now=remote.Enrollment().requested_ms-1;
        else f.now+=20;
        if(variant==18) {
            // The existing session clock guard rejects this before enrollment
            // polling. Cleanup cancels the pending request, without querying.
            const auto getters=f.paired_getters;
            assert(remote.Poll(f.now));remote.Close();
            assert(f.paired_getters==getters);
        } else if(variant!=16)(void)remote.Poll(f.now);
        if(variant==13)assert(remote.Enrollment().state==unsigned(EnrollmentState::TimedOut));
        else if(variant==14 || variant==15)assert(remote.Enrollment().state==unsigned(EnrollmentState::Failed));
        else if(variant==16 || variant==18)assert(remote.Enrollment().state==unsigned(EnrollmentState::Cancelled));
        else assert(remote.Enrollment().state==unsigned(EnrollmentState::Listed));
    }
    const auto count=f.pair_calls;assert(remote.EnrollOnce(42,EnrollmentIntentPersist) && f.pair_calls==count);
    assert(remote.Enrollment().checksum==EnrollmentChecksum(remote.Enrollment()));
    remote.Close();assert(!f.disconnects); // No unpair, bond clearing, fake HID lifecycle or ownership override.
}
void EnrollmentServiceGuards() {
    BtdrvBleAdvertisePacketParameter p{};BtdrvAddress addresses[10]{};u8 count{};
    f=FakeState{};
    ManagedGatt foreground;
    assert(foreground.PairDevice(4,p) && foreground.PairedAddresses(p,addresses,10,&count));
    assert(!foreground.Open());
    assert(foreground.PairDevice(4,p) && foreground.PairedAddresses(p,addresses,10,&count));foreground.Close();
    assert(!f.system_opens && !f.pair_calls && !f.paired_getters);
    f=FakeState{};f.system=true;f.system_open_result=Invalid();
    ManagedGatt failed(true);assert(failed.Open());
    assert(failed.PairDevice(4,p) && failed.PairedAddresses(p,addresses,10,&count));failed.Close();
    assert(!f.system_closes && !f.pair_calls && !f.paired_getters);
    f=FakeState{};f.system=true;
    ManagedGatt system(true);
    assert(system.PairDevice(4,p) && system.PairedAddresses(p,addresses,10,&count));
    assert(!system.Open());
    assert(system.PairDevice(0,p) && system.PairDevice(0xffffffff,p));
    assert(system.PairedAddresses(p,nullptr,10,&count) && system.PairedAddresses(p,addresses,0,&count) &&
           system.PairedAddresses(p,addresses,11,&count) && system.PairedAddresses(p,addresses,10,nullptr));
    system.Close();assert(system.PairDevice(4,p) && system.PairedAddresses(p,addresses,10,&count));
    assert(f.system_opens==1 && f.system_closes==1 && !f.pair_calls && !f.paired_getters);
}
#endif
void SystemFlow(unsigned variant) {
    Sandbox sandbox;f=FakeState{};f.system=true;
    if (variant==1) f.system_open_result=Invalid();
    if (variant==2) f.system_mtu_result=0x5528f;
    f.native_requests=variant>=3;
    if (variant==4) f.native_result=0x5528f;
    if (variant==5) f.drop_native_events=true;
    LibnxSession remote(Log,true,true,true,f.native_requests);
    if (variant==1) {
        assert(remote.InitializeIdentityTest(f.target)==Invalid() && f.system_opens==1 && !f.system_closes && !f.connects);
        return;
    }
    Start(remote);const auto rc=Advance(remote);
    if (variant==2) assert(rc==0x5528f && !remote.Ready() && !f.characteristic_reads && f.system_mtu_calls==1);
    else if (variant==4) assert(rc==0x5528f && !remote.Ready() && f.native_calls==1 && !f.registers && !f.writes);
    else if (variant==5) {
        assert(!remote.Ready() && !f.registers && !f.writes);
        f.now+=30000;assert(remote.Poll(f.now)); // no app-path fallback if native event routing fails
    }
    else {
        assert(!rc && remote.Ready() && remote.FingerprintObserved() && remote.SecurityReadObserved());
        assert(f.system_mtu_calls==1 && f.system_getters>=5 && f.registers==2 && f.writes==2);
        const u8 key[]{0x52,0x58,0};Queue(4,0x1812,0x2a4d,0,key,3);f.now+=20;
        assert(!remote.Poll(f.now) && remote.Buttons()==(Up|Confirm));
    }
    remote.Close();assert(f.system_opens==1 && f.system_closes==1 && !f.disconnects);
}
void NativeRequestFailure(unsigned command) {
    Sandbox sandbox;f=FakeState{};f.system=f.native_requests=true;f.native_fail_command=command;
    LibnxSession remote(Log,true,true,true,true);Start(remote);
    const auto rc=Advance(remote);
    assert(rc==0x5528f && !remote.Ready() && f.native_calls && f.connects==1);
    if(command==90 || command==91 || command==94) assert(!f.writes);
    remote.Close();assert(f.driver_opens==f.driver_closes && !f.disconnects);
}
void NativeWhitelist() {
    f=FakeState{};f.native_requests=true;NativeGattRequests driver;
    assert(!driver.Open());BtdrvGattId hid{},information{},report{},map{},pnp{},ccc{},reference{},vendor{};
    hid.uuid=U(0x1812);information.uuid=U(0x180a);report.uuid=U(0x2a4d);map.uuid=U(0x2a4b);
    pnp.uuid=U(0x2a50);ccc.uuid=U(0x2902);reference.uuid=U(0x2908);vendor.uuid=U(0xff01);
    const u8 bad[]{0,0},enable[]{1,0};
    assert(driver.ReadCharacteristic(4,true,&hid,&vendor,0));
    assert(driver.ReadCharacteristic(4,true,&information,&pnp,1));
    assert(driver.ReadCharacteristic(0,true,&hid,&map,0));
    assert(driver.ReadCharacteristic(4,false,&hid,&map,0));
    assert(driver.ReadCharacteristic(4,true,&hid,&map,2));
    assert(driver.ReadDescriptor(4,true,&hid,&report,&ccc,1));
    assert(driver.Notify(4,true,&hid,&map,true));
    assert(driver.WriteCcc(4,true,&hid,&report,&ccc,bad,2,1));
    assert(driver.WriteCcc(4,true,&hid,&report,&ccc,enable,1,1));
    assert(driver.WriteCcc(4,true,&hid,&report,&vendor,enable,2,1));
    assert(!f.native_calls);driver.Close();assert(f.driver_opens==1 && f.driver_closes==1);
}
void NativeOpenFailure() {
    Sandbox sandbox;f=FakeState{};f.system=f.native_requests=true;f.scenario=DriverOpenError;
    LibnxSession remote(Log,true,true,true,true);
    assert(remote.InitializeIdentityTest(f.target)==Invalid());
    assert(f.system_opens==1 && f.system_closes==1 && f.driver_opens==1 && !f.driver_closes && !f.connects && !f.native_calls);
}
void DriverFlow(Scenario scenario) {
    Sandbox sandbox; f=FakeState{}; f.scenario=scenario;
    LibnxSession remote(Log,true,true); Start(remote);
    const auto rc=Advance(remote);
    assert(f.connects==1 && f.security_reads==1 && f.driver_opens==1);
    assert(Logged("HID_METADATA_RAW after_security index=0 offset=32 bytes="));
    AssertRawChunks();
    if(scenario==DriverRecovered || scenario==DriverDeviceShape) {
        assert(!rc && remote.Ready() && f.registers==2 && f.writes==2 && f.driver_queries==(scenario==DriverDeviceShape ? 5U : 3U));
        assert(f.descriptor_reads==(scenario==DriverDeviceShape ? 5U : 3U));
        const u8 key[]{0x52,0x58,0}; Queue(4,0x1812,0x2a4d,0,key,3); f.now+=20;
        assert(!remote.Poll(f.now) && remote.Buttons()==(Up|Confirm));
        assert(Logged("btm=00 driver=12"));
    } else {
        assert(rc && !remote.Ready() && !f.registers && !f.writes);
        if(scenario!=DriverZeroSelected) assert(!f.descriptor_getters && !f.descriptor_reads);
    }
    assert(f.driver_queries<=(scenario==DriverDeviceShape ? 5U : 3U) && f.driver_closes==(scenario==DriverOpenError ? 0U : 1U));
    remote.Close(); assert(!f.disconnects && std::filesystem::exists("sdmc:/config/amazon-remote/link-test-pending.txt"));
}
void FailureFlow(Scenario scenario) {
    Sandbox sandbox; f=FakeState{}; f.scenario=scenario;
    LibnxSession remote(Log,true,true); Start(remote);
    Result rc=0;
    for(unsigned i=0;i<800 && !rc;++i) { f.now+=20; rc=remote.Poll(f.now); }
    assert(rc && !remote.Ready() && f.connects==1 && f.security_reads==1 && remote.FingerprintObserved());
    if(scenario==SecurityError || scenario==SecurityTimeout) assert(f.post_metadata==0 && !f.registers && !f.writes);
    if(scenario==DuplicateInstances || scenario==TruncatedMetadata) assert(!f.descriptor_getters && !f.registers && !f.writes);
    if(scenario==ZeroProperties || scenario==BadDescriptor) assert(!f.registers && !f.writes);
    if(scenario==ZeroProperties) assert(remote.GetError()==Error::NotifyUnavailable && Logged("INPUT_STOP_NOTIFY_UNAVAILABLE"));
    if(scenario==BadCccCompletion) assert(f.registers==1 && f.writes==1 && !remote.Ready());
    remote.Close(); assert(!remote.Buttons() && !f.disconnects);
}
void NotificationFailure(unsigned variant) {
    Sandbox sandbox; f=FakeState{};
    LibnxSession remote(Log,true,true); Start(remote); assert(!Advance(remote) && remote.Ready());
    const u8 keyboard[]{0x52,0x58,0};
    if(variant==0) Queue(5,0x1812,0x2a4d,0,keyboard,3); // Indication not subscribed.
    if(variant==1) Queue(4,0xfe03,0x2a4d,0,keyboard,3);
    if(variant==2) Queue(4,0x1812,0x2a4d,0,keyboard,2);
    if(variant==3) { f.connected=false; f.now+=501; }
    if(variant==4) { f.address.address[0]^=1; f.now+=501; }
    if(variant==5) f.now+=90000;
    f.now+=20; assert(remote.Poll(f.now));
    remote.Close(); assert(!remote.Buttons() && !f.disconnects);
    if(variant==3 || variant==4) assert(!f.deregisters); // No operations on reassigned/missing handle.
}
void FingerprintUnchanged() {
    Sandbox sandbox; f=FakeState{};
    LibnxSession remote(Log,true); Start(remote);
    for(unsigned i=0;i<30;++i) { f.now+=20; assert(!remote.Poll(f.now)); }
    assert(remote.GetPhase()==Phase::FingerprintVerified && f.characteristic_reads==2 && !f.security_reads && !f.pre_metadata && !f.registers && !f.writes);
    remote.Close(); assert(!f.disconnects);
}
void MetadataDeadline(Scenario scenario) {
    Sandbox sandbox; f=FakeState{}; f.scenario=scenario;
    LibnxSession remote(Log,true,true); Start(remote);
    assert(Advance(remote) && remote.FingerprintObserved() && !remote.Ready());
    assert(!f.descriptor_getters && !f.registers && !f.writes);
    if(scenario==PreBindingDelay) assert(!f.pre_metadata && !f.security_reads);
    if(scenario==PreMetadataDelay) assert(f.pre_metadata==1 && !f.security_reads);
    if(scenario==PostBindingDelay) assert(f.pre_metadata==1 && f.security_reads==1 && !f.post_metadata);
    remote.Close(); assert(!f.disconnects);
}
}
bool serviceIsActive(Service *s) { return s->id!=0; }
void serviceClose(Service *s) {
#ifdef ARMODULE_CENTRAL_BOND_B1
    if(s->id==81){s->id=0;++f.bond_closes;return;}
#endif
    assert(s->id==80); s->id=0; ++f.driver_closes;
}
bool hosversionBefore(unsigned,unsigned,unsigned) { return false; }
u32 hosversionGet(){return f.firmware;}
Result smGetService(Service *s,const char *name) {
#ifdef ARMODULE_CENTRAL_BOND_B1
    if(std::string(name)=="mc"){assert(!s->id);s->id=81;++f.bond_opens;return 0;}
#endif
    assert(std::string(name)=="btdrv" && !s->id); ++f.driver_opens;
    if(f.scenario==DriverOpenError) return Invalid();
    s->id=80;
    return 0;
}
Result FakeServiceDispatch(Service *s,u32 cmd,const void *input,std::size_t input_size,void *output,std::size_t output_size) {
#ifdef ARMODULE_NATIVE_OWNER
    if(cmd==100){assert(s->id==80 && !input && !input_size && output_size==1);++f.enabled_calls;*static_cast<u8*>(output)=f.bond_enabled;return f.enabled_result;}
#endif
    assert(s->id==80 && output_size==sizeof(CachedCharacteristicOut));
    // This seam rejects command0, all radio/event/writing commands and wrong ABI.
    assert(cmd==80 || cmd==81); ++f.driver_queries;
    if(cmd==80) {
        assert(input_size==sizeof(CachedFirstIn)); const auto &in=*static_cast<const CachedFirstIn*>(input);
        assert(in.primary==1 && in.connection==4 && Short(in.service.uuid)==0x1812 && Short(in.filter)==0x2a4d);
        assert(f.driver_queries==1);
    } else {
        assert(input_size==sizeof(CachedNextIn)); const auto &in=*static_cast<const CachedNextIn*>(input);
        assert(in.primary==1 && in.connection==4 && Short(in.service.uuid)==0x1812 && Short(in.filter)==0x2a4d);
        assert(in.previous.instance_id==f.driver_queries-2);
    }
    if(f.scenario==DriverReadError) return Invalid();
    if(f.scenario==DriverBindingDelay) f.now+=90000;
    if(f.scenario==DriverReassigned) f.address.address[0]^=1;
    auto &out=*static_cast<CachedCharacteristicOut*>(output); out={};
    out.id.uuid=U(f.scenario==DriverWrongUuid ? 0x2a4b : 0x2a4d);
    out.id.instance_id=f.scenario==DriverUnknownInstance ? 9 : f.scenario==DriverRepeatedInstance ? 0 : f.driver_queries-1;
    out.properties=f.scenario==ZeroProperties || f.scenario==DriverZeroSelected ? 0 : f.scenario==DriverConflict ? 0x10 : 0x12;
    if(f.scenario==DriverDeviceShape && out.id.instance_id==2) out.properties=0x08;
    return 0;
}
u64 armGetSystemTick() { return f.now*1000000; } u64 armTicksToNs(u64 ticks) { return ticks; }
Result eventWait(Event *event,u64) {
    if(event->id==1 && !f.events.empty()) return 0;
    if(event->id==2 && f.discovery) { f.discovery=false; return 0; }
    return Timeout();
}
void eventClose(Event *event) { event->id=0; }
Result btmuInitialize() { ++f.user_opens;return 0; } void btmuExit() {++f.user_closes;}
Result btdevInitialize() { return 0; } void btdevExit() {}
Result btdevAcquireBleGattOperationEvent(Event *event) { event->id=1; return 0; }
Result btdevAcquireBleServiceDiscoveryEvent(Event *event) { event->id=2; return 0; }
Result btdevAcquireBleConnectionStateChangedEvent(Event *event) { event->id=3; return 0; }
Result btdevAcquireBlePairingEvent(Event *event) { event->id=4; return 0; }
Result btmuAcquireBleConnectionEvent(Event *event) { event->id=3; return 0; }
Result btmuAcquireBleScanEvent(Event *event) { event->id=5; return 0; }
Result btdevAcquireBleScanEvent(Event *event) { event->id=5; return 0; }
Result btdevRegisterGattOperationNotification(const BtdrvGattAttributeUuid*) { return 0; }
Result btdevUnregisterGattOperationNotification(const BtdrvGattAttributeUuid*) { return 0; }
Result btmuStartBleScanForGeneral(BtdrvBleAdvertisePacketParameter parameter) {
    constexpr u8 pattern[]{4,0x27,2,0x7d,0,0};
    assert(parameter.company_id==0x171 && !std::memcmp(parameter.pattern_data,pattern,6));
    ++f.scan_starts;++f.general_starts;
    if(f.general_start_result)return f.general_start_result;
    f.scan=true;return 0;
}
Result btmuStopBleScanForGeneral() {
    ++f.scan_stops;++f.general_stops;
    if(f.general_stop_result)return f.general_stop_result;
    f.scan=false;return 0;
}
Result btmuStartBleScanForSmartDevice(const BtdrvGattAttributeUuid *uuid) { assert(uuid->size==16); ++f.scan_starts;++f.smart_starts;f.scan=true; return 0; }
Result btdevStartBleScanSmartDevice(const BtdrvGattAttributeUuid *uuid) { return btmuStartBleScanForSmartDevice(uuid); }
Result btmuStopBleScanForSmartDevice() { ++f.scan_stops;++f.smart_stops;f.scan=false; return 0; } Result btdevStopBleScanSmartDevice() { return btmuStopBleScanForSmartDevice(); }
Result btdevGetBleConnectionInfoList(BtdrvBleConnectionInfo *info,u8 count,u8 *total) { return btmuBleGetConnectionState(info,count,total); }
Result btmuBleGetConnectionState(BtdrvBleConnectionInfo *info,u8 count,u8 *total) {
    if (f.cleanup_test) {
        ++f.cleanup_queries;
        if (++f.cleanup_user_calls==f.cleanup_user_fail_at) return 0x5528f;
        if (f.cleanup_user_result) return f.cleanup_user_result;
        *total=f.user_present?1:0;
        if (*total) info[0]={f.cleanup_handle,f.address,{}};
        if (f.cleanup_unrelated) info[(*total)++]={9,{{1,2,3,4,5,6}},{}};
        return 0;
    }
    if(!f.delayed && ((f.scenario==PreBindingDelay && Logged("FINGERPRINT_VERIFIED")) ||
                     (f.scenario==PostBindingDelay && Logged("SECURITY_READ_COMPLETED")))) {
        f.delayed=true; f.now+=90000;
    }
    assert(count>=1); *total=f.connected ? 1 : 0;
    if(f.connected) info[0]={4,f.address,{}};
    return 0;
}
Result btmuGetBleScanResultsForGeneral(BtdrvBleScanResult*,u8,u8 *total) { ++f.scan_getters;*total=f.general_count; return f.general_getter_result; }
Result btmuGetBleScanResultsForSmartDevice(BtdrvBleScanResult*,u8,u8 *total) { ++f.scan_getters;*total=0; return f.smart_getter_result; }
Result btmuBleConnect(BtdrvAddress target) { assert(!std::memcmp(&target,&f.target,sizeof target)); ++f.connects; f.connected=f.discovery=true; return 0; }
static Result PairedDevices(BtdrvBleAdvertisePacketParameter p,BtdrvAddress *out,u8 count,u8 *total,bool system) {
    assert((f.context_active || system==f.system_enrollment) && (!system || (f.system_opens==1 && !f.system_closes)));
    assert(count==10 && p.company_id==0x171);
    const u8 pattern[]{4,0x27,2,0x7d,0,0};assert(!std::memcmp(p.pattern_data,pattern,6));++f.paired_getters;
    if(f.pair_variant==8 || (f.pair_variant==14 && f.pair_calls))return 0x5528f;
    *total=f.pair_variant==6?10:f.pair_variant==7?2:f.pair_variant==20?11:f.listed?1:0;
    if(f.listed || f.pair_variant==7)out[0]=f.target;
    if(f.pair_variant==7)out[1]=f.target;
    return 0;
}
static Result PairDevice(u32 connection,BtdrvBleAdvertisePacketParameter p,bool system) {
    assert(system==f.system_enrollment && (!system || (f.system_opens==1 && !f.system_closes)));
    assert(connection==4 && !std::memcmp(&f.address,&f.target,sizeof f.target) &&
           p.company_id==0x171 && f.security_requested && f.intent_calls==1 && !f.pair_calls);
    ++f.pair_calls;
    if(f.pair_variant==12)return 0x5528f;
    f.listed=f.pair_variant!=13;return 0;
}
Result btmuBleGetPairedDevices(BtdrvBleAdvertisePacketParameter p,BtdrvAddress *out,u8 count,u8 *total) {return PairedDevices(p,out,count,total,false);}
Result btmBleGetPairedAddresses(BtdrvBleAdvertisePacketParameter p,BtdrvAddress *out,u8 count,u8 *total) {return PairedDevices(p,out,count,total,true);}
Result btmBleGetConnectionState(BtdrvBleConnectionInfo *out,u8 count,u8 *total) {
    assert(f.system_opens==f.system_closes+1 && count==16);
    if (f.cleanup_test) {
        ++f.cleanup_queries;
        if (++f.cleanup_system_calls==2) {
            switch (f.cleanup_final_variant) {
            case 1:f.system_present=false;break;
            case 2:f.user_present=false;break;
            case 3:f.cleanup_handle=5;break;
            case 4:f.cleanup_variant=1;break;
            case 5:return 0x5528f;
            case 6:f.now+=2001;break;
            case 7:f.user_present=f.system_present=false;break;
            case 8:f.address.address[0]^=1;break;
            }
        }
        if (f.cleanup_system_result) return f.cleanup_system_result;
        *total=f.system_present?1:0;
        if (*total) out[0]={f.cleanup_handle,f.address,{}};
        if (f.cleanup_unrelated) out[(*total)++]={9,{{1,2,3,4,5,6}},{}};
        if (f.cleanup_variant==1) *total=count;
        if (f.cleanup_variant==2) { *total=2;out[1]=out[0]; }
        if (f.cleanup_variant==3) f.now+=5000;
        return 0;
    }
    if (!f.context_active) {
        ++f.preconnect_system_queries;
        *total=f.preconnect_variant==4?16:f.preconnect_variant==5?17:
            (f.preconnect_variant==1 || f.preconnect_variant==2 || f.preconnect_variant==6)?1:0;
        if (*total && *total<count) {
            out[0]={f.preconnect_variant==6?0u:4u,f.target,{}};
            if (f.preconnect_variant==2)out[0].addr.address[0]^=1;
        }
        if (f.preconnect_variant==8)f.now+=60001;
        f.now+=f.preconnect_delay_ms;
        return f.preconnect_variant==3?71:0;
    }
    ++f.system_connection_queries;
    *total=f.context_variant==1?0:f.context_variant==3?16:1;
    out[0]={f.context_variant==2?5u:4u,f.target,{}};
    return f.context_variant==4?71:0;
}
Result btmsysInitialize(){assert(f.context_active);++f.sys_opens;return f.context_variant==5?72:0;}
void btmsysExit(){++f.sys_closes;}
Result btmsysIsGamepadPairingStarted(bool *out) {
    ++f.flag_queries;*out=f.context_variant==7;
    if(f.context_variant==8)f.address.address[0]^=1;
    if(f.context_variant==9)f.now+=2001;
    return f.context_variant==6?73:0;
}
Result btmuBlePairDevice(u32 connection,BtdrvBleAdvertisePacketParameter p) {return PairDevice(connection,p,false);}
Result btmBlePairDevice(u32 connection,BtdrvBleAdvertisePacketParameter p) {return PairDevice(connection,p,true);}
Result btdevConnectToGattServer(BtdrvAddress target) { return btmuBleConnect(target); }
Result btdevDisconnectFromGattServer(u32) { ++f.disconnects; return Invalid(); }
Result btmuBleDisconnect(u32 handle) {
    (void)handle;++f.user_disconnects;return 0x5528f;
}
Result btmBleDisconnect(u32 handle) {
    assert(f.cleanup_test && handle==4 && f.user_present && f.system_present);
    assert(f.system_opens==1 && !f.system_closes && !f.user_disconnects);
    assert(f.cleanup_system_calls>=2 && f.cleanup_user_calls>=4);
    assert(!std::memcmp(&f.address,&f.target,sizeof f.target));
    ++f.owned_disconnects;return f.disconnect_result;
}
Result btdevConfigureBleMtu(u32 connection,u16 mtu) { assert(!f.system && connection==4 && mtu==512); return 0; }
Result btdevGetBleMtu(u32 connection,u16 *mtu) { assert(!f.system && connection==4); *mtu=247; return 0; }
Result btdevGetGattServices(u32 connection,BtdevGattService *services,u8 count,u8 *total) {
    assert(!f.system || f.fixture);
    assert(connection==4 && count>=3); *total=3;
    services[0]={{3,U(0x180a),0x12,4},0,0x24,true};
    services[1]={{3,U(0x1812),0x25,4},0,0x41,true};
    services[2]={{3,U(0x180f),0x26,4},0,0,false}; return 0;
}
Result btdevGattServiceGetCharacteristics(BtdevGattService *service,BtdevGattCharacteristic *chars,u8 count,u8 *total) {
    assert(!f.system || f.fixture);
    assert(count>=1); *total=1;
    const bool hid=Short(service->attr.uuid)==0x1812;
    chars[0]={{1,U(hid?0x2a4b:0x2a50),u16(hid?0x2e:0x24),4},0,0}; return 0;
}
Result btdevGattCharacteristicGetDescriptors(BtdevGattCharacteristic*,BtdevGattDescriptor*,u8,u8*) { return Invalid(); }
Result btmuGetGattCharacteristics(u32 connection,u16 service,BtmGattCharacteristic *chars,u8 count,u8 *total) {
    assert(!f.system || f.fixture);
    assert(connection==4 && service==0x25 && count>=4);
    if(f.security_requested) ++f.post_metadata; else ++f.pre_metadata;
    if(f.scenario==PreMetadataDelay && !f.security_requested) f.now+=90000;
    if(f.scenario==TruncatedMetadata && f.security_requested) { *total=count; return 0; }
    if(f.scenario==DriverDeviceShape) {
        assert(count>=9); *total=9;
        const u16 handles[]{0x28,0x2a,0x2c,0x2e,0x30,0x34,0x38,0x3b,0x3f};
        const u16 uuids[]{0x2a4a,0x2a4c,0x2a4e,0x2a4b,0x2a4d,0x2a4d,0x2a4d,0x2a4d,0x2a4d};
        for(unsigned i=0;i<9;++i) { chars[i].handle=handles[i]; chars[i].uuid=U(uuids[i]); chars[i].instance_id=i>=4 ? i-4 : 0; chars[i].properties=0; }
    return 0;
}
    *total=4; chars[0].uuid=U(0x2a4b); chars[0].handle=0x2e;
    for(unsigned i=0;i<3;++i) {
        chars[i+1].uuid=U(0x2a4d); chars[i+1].handle=0x30+i*4;
        chars[i+1].instance_id=f.security_requested && f.scenario!=DuplicateInstances ? i : 0;
        const bool driver_case=f.scenario>=DriverRecovered;
        chars[i+1].properties=f.security_requested && f.scenario!=ZeroProperties && !driver_case ? 0x12 : 0;
        if(f.security_requested && f.scenario==DriverConflict && i==1) chars[i+1].properties=0x12;
    }
#ifdef ARMODULE_HID_INFO_H1
    if (f.info_mode != 1) {
        chars[4]={};chars[4].uuid=U(0x2a4a);chars[4].handle=0x28;*total=5;
        if (f.info_mode==2) {chars[5]=chars[4];chars[5].handle=0x2a;*total=6;}
        if (f.info_mode==9) chars[4].instance_id=256;
    }
#endif
    return 0;
}
Result btmuGetGattDescriptors(u32 connection,u16 characteristic,BtmGattDescriptor *descs,u8 count,u8 *total) {
    assert(!f.system || f.fixture);
    assert(connection==4 && count>=2 && characteristic>=0x30 && characteristic<=(f.scenario==DriverDeviceShape ? 0x3f : 0x38)); ++f.descriptor_getters;
    if(f.scenario==DriverDeviceShape && characteristic==0x38) { *total=1; descs[0].handle=0x39; descs[0].uuid=U(0x2908); return 0; }
    if(f.scenario==BadDescriptor) { *total=1; descs[0].handle=characteristic; descs[0].uuid=U(0x2908); return 0; }
    *total=2; descs[0].handle=characteristic+1; descs[0].uuid=U(0x2902);
    descs[1].handle=characteristic+2; descs[1].uuid=U(0x2908); return 0;
}
Result btmInitialize() { assert(f.system);++f.system_opens;return f.system_open_result; }
void btmExit() { assert(f.system);++f.system_closes; }
Result btmConfigureBleMtu(u32 connection,u16 mtu) { assert(f.system && connection==4 && mtu==512);++f.system_mtu_calls;return f.system_mtu_result; }
Result btmGetBleMtu(u32 connection,u16 *mtu) { assert(f.system && connection==4);*mtu=247;return 0; }
Result btmGetGattServices(u32 connection,BtmGattService *out,u8 count,u8 *total) {
    assert(f.system);++f.system_getters;std::array<BtdevGattService,100> fixture{};
    if(f.context_active && f.census_variant==4)return 84;
    if(f.context_active && f.census_variant==5){*total=100;return 0;}
    f.fixture=true;const auto rc=btdevGetGattServices(connection,fixture.data(),count,total);f.fixture=false;
    for (u8 i=0;i<*total;++i) { out[i]={};out[i].uuid=fixture[i].attr.uuid;out[i].handle=fixture[i].attr.handle;
        out[i].instance_id=fixture[i].instance_id;out[i].end_group_handle=fixture[i].end_group_handle;out[i].primary_service=fixture[i].primary_service; }
    if(f.context_active && f.census_variant>=1 && f.census_variant<=3) {
        *total=4;out[3]={};out[3].uuid.size=f.census_variant==3?17:16;
        std::copy(BtmManagedProfileUuid.begin(),BtmManagedProfileUuid.end(),out[3].uuid.uuid);
        if(f.census_variant==2)out[3].uuid.uuid[0]^=1;
        out[3].handle=0xfca2;out[3].end_group_handle=0xfcac;out[3].primary_service=1;
    }
    return rc;
}
Result btmGetGattCharacteristics(u32 connection,u16 service,BtmGattCharacteristic *out,u8 count,u8 *total) {
    assert(f.system);++f.system_getters;
    if (service==0x12) { *total=1;out[0]={};out[0].uuid=U(0x2a50);out[0].handle=0x24;return 0; }
    f.fixture=true;const auto rc=btmuGetGattCharacteristics(connection,service,out,count,total);f.fixture=false;return rc;
}
Result btmGetGattDescriptors(u32 connection,u16 characteristic,BtmGattDescriptor *out,u8 count,u8 *total) {
    assert(f.system);++f.system_getters;f.fixture=true;
    const auto rc=btmuGetGattDescriptors(connection,characteristic,out,count,total);f.fixture=false;return rc;
}
Result btLeClientReadCharacteristic(u32 connection,bool primary,const BtdrvGattId *service,const BtdrvGattId *characteristic,u8 auth) {
    assert(!f.native_requests || f.fixture);
    assert(connection==4 && primary); ++f.characteristic_reads;
    const auto uuid=Short(characteristic->uuid);
#ifdef ARMODULE_HID_INFO_H1
    if (uuid==0x2a4a) {
        assert(Short(service->uuid)==0x1812 && !auth && f.security_requested && f.writes==2 && f.registers==2);
        ++f.info_reads;
        if(f.info_mode==4) return 0;
        if(f.info_mode==5) return Invalid();
        const u8 value[]{0x11,0x01,0,f.info_flags};
        Queue(0,0x1812,uuid,0,value,f.info_mode==3?3:4,f.info_mode==6?5:0);
        return 0;
    }
#endif
    if(uuid==0x2a50) { assert(Short(service->uuid)==0x180a && auth==0); const u8 pnp[]{2,0x71,1,0x27,4,0x60,0}; Queue(0,0x180a,uuid,0,pnp,sizeof pnp); }
    else {
        assert(uuid==0x2a4b && Short(service->uuid)==0x1812);
        if(auth) { assert(auth==1); ++f.security_reads; f.security_requested=true; }
        if(auth && f.scenario==SecurityTimeout) return 0;
        Queue(0,0x1812,uuid,0,K7q3m7Descriptor,sizeof K7q3m7Descriptor,auth && f.scenario==SecurityError ? 5 : 0);
    } return 0;
}
Result btLeClientReadDescriptor(u32 connection,bool primary,const BtdrvGattId *service,const BtdrvGattId *characteristic,const BtdrvGattId *descriptor,u8 auth) {
    assert(!f.native_requests || f.fixture);
    assert(connection==4 && primary && Short(service->uuid)==0x1812 && Short(characteristic->uuid)==0x2a4d && Short(descriptor->uuid)==0x2908 && auth==1);
    ++f.descriptor_reads; const u8 reference[]{u8(characteristic->instance_id==0 ? 1 : characteristic->instance_id==1 ? 2 : 0xef),1};
    if(f.scenario==DriverDeviceShape) {
        assert(characteristic->instance_id<5); const u8 ids[]{1,0xf0,0xf2,2,0xef};
        const u8 actual[]{ids[characteristic->instance_id],u8(characteristic->instance_id==2 ? 2 : 1)};
        Queue(2,0x1812,0x2a4d,0x2908,actual,sizeof actual); return 0;
    }
    Queue(2,0x1812,0x2a4d,0x2908,reference,sizeof reference); return 0;
}
Result btLeClientRegisterNotification(u32 connection,bool primary,const BtdrvGattId *service,const BtdrvGattId *characteristic) {
    assert(!f.native_requests || f.fixture);
    assert(connection==4 && primary && Short(service->uuid)==0x1812 && Short(characteristic->uuid)==0x2a4d);
    assert(f.scenario==DriverDeviceShape ? (characteristic->instance_id==0 || characteristic->instance_id==3) : characteristic->instance_id<2); ++f.registers; return 0;
}
Result btLeClientDeregisterNotification(u32 connection,bool primary,const BtdrvGattId*,const BtdrvGattId *characteristic) {
    assert(!f.native_requests || f.fixture);
    assert(connection==4 && primary && f.connected && !std::memcmp(&f.address,&f.target,sizeof f.target));
    assert(f.scenario==DriverDeviceShape ? (characteristic->instance_id==0 || characteristic->instance_id==3) : characteristic->instance_id<2); ++f.deregisters; return 0;
}
Result btLeClientWriteDescriptor(u32 connection,bool primary,const BtdrvGattId *service,const BtdrvGattId *characteristic,const BtdrvGattId *descriptor,const void *data,std::size_t size,u8 auth) {
    assert(!f.native_requests || f.fixture);
    assert(connection==4 && primary && Short(service->uuid)==0x1812 && Short(characteristic->uuid)==0x2a4d && Short(descriptor->uuid)==0x2902 && auth==1);
    assert(f.scenario==DriverDeviceShape ? (characteristic->instance_id==0 || characteristic->instance_id==3) : characteristic->instance_id<2);
    const auto *value=static_cast<const u8*>(data); assert(size==2 && value[0]==1 && value[1]==0); ++f.writes;
    Queue(3,0x1812,0x2a4d,0x2902,nullptr,0,f.scenario==BadCccCompletion ? 5 : 0); return 0;
}
Result btGetLeEventInfo(void *out,std::size_t size,BtdrvBleEventType *type) {
    assert(size==sizeof(BtdrvBleEventInfo) && !f.events.empty()); std::memcpy(out,&f.events.front(),size); f.events.pop_front(); *type=BtdrvBleEventType_ClientNotify; return 0;
}
Result FakeServiceDispatchInput(Service *s,u32 cmd,const void *input,std::size_t size,FakeDispatchParams p) {
#ifdef ARMODULE_CENTRAL_BOND_B1
    if(s->id==81){
#ifdef ARMODULE_RETRY_A5
        if(cmd==65004){
            assert(!input && !size && p.buffers[0].size==sizeof f.connection_evidence);
            std::memcpy(const_cast<void*>(p.buffers[0].data),&f.connection_evidence,sizeof f.connection_evidence);
            return f.evidence_result;
        }
#endif
#ifdef ARMODULE_WAKE_A3
        if(cmd==central_bond::WakeStatusCommand){
            assert(!input && !size && !p.in_send_pid && p.buffer_attrs[0]==5 && p.buffers[0].size==sizeof f.wake);
            std::memcpy(const_cast<void*>(p.buffers[0].data),&f.wake,sizeof f.wake);return 0;
        }
        if(cmd==central_bond::WakeCommand){
            assert(p.in_send_pid && input && size==32 && !f.connected && !f.context_active);
            central_bond::WakeRequest r{};std::memcpy(&r,static_cast<const u8*>(input)+8,sizeof r);
#ifdef ARMODULE_SETUP_SLEEP_R3
            if(r.reserved==central_bond::WakeKindPending)
                assert(r.serial==f.wake.serial && r.nonce==f.peer.nonce && !r.connection && !r.owner_epoch && f.wake.reserved==r.reserved);
            else
#endif
            assert(r.serial==f.wake.serial && r.nonce==f.bond.nonce && r.connection==f.bond.connection && r.owner_epoch==f.bond.owner_epoch && !r.reserved);
            ++f.wake_calls;
            if(!f.wake_result){const auto generation=f.bond.generation;f.bond={};f.bond.store=central_bond::Store::Saved;f.bond.generation=generation;
                if(f.deferred_reload){f.bond.store=central_bond::Store::Unknown;f.reload_polls=2;f.wake={};}}
            return f.wake_result;
        }
#endif
#ifdef ARMODULE_NATIVE_PEER_C1
        if(cmd==central_bond::PeerStatusCommand){
            assert(!input && !size && !p.in_send_pid && p.buffer_attrs[0]==5 && p.buffers[0].size==sizeof f.peer);
            ++f.peer_queries;std::memcpy(const_cast<void*>(p.buffers[0].data),&f.peer,sizeof f.peer);return f.peer_query_result;
        }
        if(cmd==central_bond::HciTraceCommand){
            assert(!input && !size && p.buffers[0].size==sizeof f.hci);
            ++f.hci_reads;f.now+=f.hci_query_delay_ms;
            std::memcpy(const_cast<void*>(p.buffers[0].data),&f.hci,sizeof f.hci);return f.hci_query_result;
        }
        if(cmd==central_bond::PreparePeerCommand){
            assert(p.in_send_pid && input && size==24 && !f.peer_prepares && !f.connected && f.bond.store==central_bond::Store::Saved);
            u64 pid{};std::memcpy(&pid,input,8);assert(!pid);
            central_bond::PreparePeer r{};std::memcpy(&r,static_cast<const u8*>(input)+8,sizeof r);
            assert(r.nonce && !r.reserved && r.capture_deadline_ms>f.now);
            ++f.peer_prepares;f.peer.phase=central_bond::PeerPhase::Waiting;f.peer.nonce=r.nonce;
            f.peer.started_ms=f.now;f.peer.capture_deadline_ms=r.capture_deadline_ms;f.peer.generation=f.bond.generation;
            return f.peer_prepare_result;
        }
#endif
        if(cmd==central_bond::StatusCommand){
            assert(!input && !size && !p.in_send_pid && p.buffer_attrs[0]==5 &&
                p.buffers[0].size==sizeof f.bond && p.buffers[0].data);
            ++f.bond_queries;
#ifdef ARMODULE_WAKE_A3
            if(f.reload_polls && !--f.reload_polls)f.bond.store=central_bond::Store::Saved;
#endif
            std::memcpy(const_cast<void*>(p.buffers[0].data),&f.bond,sizeof f.bond);
            return f.bond_query_result;
        }
        assert(p.in_send_pid && input && !p.buffer_attrs[0] && !p.buffers[0].size);
        u64 pid{};std::memcpy(&pid,input,sizeof pid);assert(!pid);
        if(cmd==central_bond::BeginCommand){
            assert(size==24 && !f.bond_begins && f.bond.store==central_bond::Store::Saved && f.connected);
            central_bond::Begin begin{};std::memcpy(&begin,static_cast<const u8*>(input)+8,sizeof begin);
            assert(begin.nonce && begin.connection==4 && begin.owner_epoch);
            ++f.bond_begins;f.bond.nonce=begin.nonce;f.bond.connection=begin.connection;
            f.bond.owner_epoch=begin.owner_epoch;f.bond.phase=central_bond::Phase::Encrypting;
        }else{
            assert(cmd==central_bond::CancelCommand && size==8);
            ++f.bond_cancels;f.bond.phase=central_bond::Phase::Cancelled;
        }
        return 0;
    }
#endif
    assert(s->id==80 && f.native_requests && !p.in_send_pid);
#ifdef ARMODULE_NATIVE_OWNER
    if(cmd==10){
        assert(size==12 && f.native_intents==1 && !f.native_bonds && !f.pair_calls);
        const auto *raw=static_cast<const u8*>(input);u32 transport{};std::memcpy(&transport,raw+8,4);
        assert(!std::memcmp(raw,f.target.address,6) && !raw[6] && !raw[7] && transport==2 && !p.buffer_attrs[0]);
        ++f.native_bonds;return f.native_bond_result;
    }
#endif
    assert(cmd==90 || cmd==91 || cmd==93 || cmd==94 || cmd==95);++f.native_calls;
    const auto *raw=static_cast<const u8*>(input);assert(raw[0]==1 && !raw[2] && !raw[3]);
    u32 connection{};std::memcpy(&connection,raw+4,4);assert(connection==4);
    BtdrvGattId service{},characteristic{},descriptor{};
    std::memcpy(&service,raw+8,sizeof service);std::memcpy(&characteristic,raw+32,sizeof characteristic);
    assert(size==((cmd==91 || cmd==93)?80U:56U));
    if (size==80) std::memcpy(&descriptor,raw+56,sizeof descriptor);
    if (cmd==93) assert(p.buffer_attrs[0]==3 && p.buffers[0].size==2 && p.buffers[0].data);
    else assert(!p.buffer_attrs[0] && !p.buffers[0].size && !p.buffers[0].data);
    if (f.native_result) return f.native_result;
    if (f.native_fail_command==cmd) return 0x5528f;
    f.fixture=true;Result rc=Invalid();
    switch (cmd) {
        case 90: rc=btLeClientReadCharacteristic(connection,true,&service,&characteristic,raw[1]);break;
        case 91: rc=btLeClientReadDescriptor(connection,true,&service,&characteristic,&descriptor,raw[1]);break;
        case 93: rc=btLeClientWriteDescriptor(connection,true,&service,&characteristic,&descriptor,p.buffers[0].data,p.buffers[0].size,raw[1]);break;
        case 94: assert(!raw[1]);rc=btLeClientRegisterNotification(connection,true,&service,&characteristic);break;
        case 95: assert(!raw[1]);rc=btLeClientDeregisterNotification(connection,true,&service,&characteristic);break;
    }
    f.fixture=false;
    if (f.drop_native_events) f.events.clear();
    return rc;
}
u64 appletGetAppletResourceUserId(){return f.cached_aruid;}
#ifdef ARMODULE_NATIVE_OWNER
bool PersistNative(const arnativebond::Record &r){
    assert(arnativebond::Valid(r) && r.state==unsigned(arnativebond::State::IntentSaved) && !r.request_issued && !f.native_bonds);
    ++f.native_intents;
    if(f.owner_change==1)f.address.address[0]^=1;
    if(f.owner_change==2)f.context_variant=2;
    if(f.owner_change==3)f.now+=2100;
    return f.persist_native;
}
void NativeOwnerFlow(unsigned variant){
    Sandbox sandbox;f=FakeState{};f.system=f.native_requests=true;
    LibnxSession remote(Log,true,true,true,true,variant!=12,true);Start(remote);
    if(variant!=11)assert(!Advance(remote) && remote.Ready());
    f.context_active=true;
    arnativebond::OwnerEnvironment env{};env.pad_attached=env.log_healthy=true;
    env.status.instance=42;env.status.now_ms=f.now;env.status.state=unsigned(armodule::State::Ready);
    env.status.output_attached=1;env.status.successful_writes=1;
    env.startup.instance=42;env.startup.now_ms=10000;env.startup.stage=unsigned(armodule::StartupStage::Status);
    auto &c=env.context;c.instance=42;c.expected_handle=4;c.snapshot.begin_ms=f.now-1;c.snapshot.end_ms=f.now;
    c.snapshot.user_open=c.snapshot.btm_open=0;
    c.snapshot.user_connections.list.result=c.snapshot.system_connections.list.result=0;
    c.snapshot.user_connections.list.count=c.snapshot.system_connections.list.count=1;
    c.snapshot.user_connections.list.matches=c.snapshot.system_connections.list.matches=1;
    c.snapshot.user_connections.target_handle=c.snapshot.system_connections.target_handle=4;
    switch(variant){
        case 1:env.pad_attached=false;break;case 2:env.log_healthy=false;break;
        case 3:env.startup.instance=43;break;case 4:env.status.now_ms-=2100;break;
        case 5:env.status.buttons=1;break;case 6:env.status.acknowledged_buttons=1;break;
        case 7:env.status.state=unsigned(armodule::State::Stopped);break;case 8:c.expected_handle=5;break;
        case 9:c.snapshot.begin_ms=1;c.snapshot.end_ms=2;f.now=70000;env.status.now_ms=f.now;break;
        case 10:{const u8 key[]{0x52,0,0};Queue(4,0x1812,0x2a4d,0,key,sizeof key);f.now+=20;assert(!remote.Poll(f.now));break;}
        case 13:remote.Close();break;case 14:f.address.address[0]^=1;break;
        case 15:f.context_variant=1;break;case 16:f.context_variant=2;break;
        case 17:f.context_variant=3;break;case 18:f.context_variant=4;break;
        case 19:f.bond_enabled=0;break;case 20:f.enabled_result=77;break;case 21:f.persist_native=false;break;
        case 22:f.owner_change=1;break;case 23:f.owner_change=2;break;case 24:f.owner_change=3;break;
        case 25:f.native_bond_result=78;break;case 26:f.firmware=MAKEHOSVERSION(22,4,0);break;
        case 29:c.before_binding=1;break;case 30:env.status.successful_writes=0;break;
        case 32:f.now+=1800000;env.status.now_ms=f.now;c.snapshot.begin_ms=f.now-1;c.snapshot.end_ms=f.now;break;
        case 35:f.bond_enabled=0xff;break;
    }
    env.status.reserved=armodule::StatusChecksum(env.status);env.startup.checksum=armodule::StartupChecksum(env.startup);
    c.snapshot.checksum=araudit::Checksum(c.snapshot);c.checksum=EnrollmentContextChecksum(c);
    if(variant==31)++c.checksum;
    const auto opens=f.system_opens,users=f.user_opens,drivers=f.driver_opens,closed=f.driver_closes,system_closed=f.system_closes;
    const auto requests=f.native_calls,connects=f.connects,writes=f.writes;
    const auto r=remote.NativeBondOnce(variant==27?0:6,env,variant==28?nullptr:PersistNative);
    assert(variant==27?!arnativebond::ValidOwnerResult(r):arnativebond::ValidOwnerResult(r));
    assert(r.trial.instance==42 && r.trial.request_issued==unsigned(variant==0 || variant==25));
    assert(f.native_bonds==unsigned(variant==0 || variant==25) && !f.pair_calls);
    assert(f.system_opens==opens && f.user_opens==users && f.driver_opens==drivers && f.driver_closes==closed && f.system_closes==system_closed);
    assert(f.native_calls==requests && f.connects==connects && f.writes==writes);
    if(variant==0){assert(r.count==2 && f.native_intents==1 && f.enabled_calls==1);
        assert(arnativebond::MatchesOwnerCommand(r,42,6) && !arnativebond::MatchesOwnerCommand(r,43,6) && !arnativebond::MatchesOwnerCommand(r,42,7));
        for(unsigned i=0;i<sizeof r;++i){auto bad=r;reinterpret_cast<u8*>(&bad)[i]^=1;assert(!arnativebond::ValidOwnerResult(bad));}}
    const auto bonds=f.native_bonds,intents=f.native_intents;
    const auto again=remote.NativeBondOnce(7,env,PersistNative);
    assert(arnativebond::ValidOwnerResult(again) && !again.trial.request_issued && !again.count && f.native_bonds==bonds && f.native_intents==intents);
    remote.Close();assert(!f.disconnects);
}
#endif
void OwnedDisconnectFlow(unsigned variant) {
    Sandbox sandbox;f=FakeState{};f.system=f.native_requests=variant!=22;
    LibnxSession remote(Log,true,true,f.system,f.native_requests,true);
    if (variant==5) { assert(!remote.InitializeIdentityTest(f.target));assert(!remote.StartLinkOnlyScan(f.now)); }
    else {
        Start(remote);
        if (variant!=4) assert(!Advance(remote) && remote.Ready());
    }
    f.cleanup_test=true;f.user_present=f.system_present=true;
    if (variant==1) f.disconnect_result=71;
    if (variant==2) f.address.address[0]^=1;
    if (variant==3) f.cleanup_handle=5;
    if (variant==6) f.user_present=f.system_present=false;
    if (variant==7) f.user_present=false;
    if (variant==8) f.cleanup_system_result=72;
    if (variant==9) f.cleanup_user_result=73;
    if (variant==10) f.cleanup_variant=1;
    if (variant==11) f.cleanup_variant=2;
    if (variant==12) f.cleanup_handle=0xffffffff;
    if (variant==21) f.native_fail_command=95;
    if (variant==23) remote.Close();
    if (variant==24) f.cleanup_unrelated=true;
    const auto reads=f.characteristic_reads,writes=f.writes,bonds=f.native_bonds,pairs=f.pair_calls;
    const auto now=f.now;
    const auto begin=remote.BeginOwnedDisconnect(now);
    assert(!remote.Buttons() && remote.Poll(f.now)); // No new input after stop begins.
    assert(f.characteristic_reads==reads && f.writes==writes && f.native_bonds==bonds && f.pair_calls==pairs);
    if (variant==5 || variant==6) {
        assert(!begin && !f.owned_disconnects && remote.GetDisconnectState()==DisconnectState::Confirmed);
    } else if ((variant>=1 && variant<=12) || (variant>=21 && variant<=23)) {
        assert(begin && remote.GetDisconnectState()==DisconnectState::Failed);
        assert(f.owned_disconnects==unsigned(variant==1));
    } else {
        assert(!begin && f.owned_disconnects==1 && remote.GetDisconnectState()==DisconnectState::Waiting);
        assert(!Logged("CONFIRMED_ABSENT")); // Accepted IPC is not disconnection.
        const auto queries=f.cleanup_queries;
        assert(!remote.BeginOwnedDisconnect(f.now) && f.owned_disconnects==1);
        f.now+=99;assert(!remote.PollOwnedDisconnect(f.now) && f.cleanup_queries==queries);
        if (variant==13) f.now=now+5000;
        else if (variant==14) f.now=now-1;
        else {
            ++f.now;
            if (variant==15) f.cleanup_system_result=74;
            if (variant==16) f.address.address[0]^=1;
            if (variant==18) f.cleanup_variant=3;
            if (variant==19) remote.Close();
        }
        if (variant==0 || variant==17 || variant==20 || variant==24) {
            // A user-only disappearance must not be reported as a closed link.
            f.user_present=false;
            assert(!remote.PollOwnedDisconnect(f.now));
            assert(remote.GetDisconnectState()==DisconnectState::Waiting);
            if (variant==17) {
                f.now=now+5000;assert(remote.PollOwnedDisconnect(f.now));
            } else {
                f.system_present=false;f.now+=100;
                if (variant==20) f.cleanup_user_result=75;
                const auto result=remote.PollOwnedDisconnect(f.now);
                assert(bool(result)==(variant==20));
                if (!result) assert(remote.GetDisconnectState()==DisconnectState::Confirmed);
            }
        } else assert(remote.PollOwnedDisconnect(f.now));
        if (variant && variant!=24) assert(remote.GetDisconnectState()==DisconnectState::Failed);
    }
    const auto requests=f.owned_disconnects,queries=f.cleanup_queries;
    const auto result=remote.DisconnectResult();
    assert(remote.BeginOwnedDisconnect(f.now)==result && remote.PollOwnedDisconnect(f.now)==result);
    assert(f.owned_disconnects==requests && f.cleanup_queries==queries); // No retries after terminal state.
    remote.Close();remote.Close();
    assert(f.owned_disconnects==requests && !f.disconnects);
    if (variant!=5) assert(std::filesystem::exists("sdmc:/config/amazon-remote/link-test-pending.txt"));
    assert(f.characteristic_reads==reads && f.writes==writes && f.native_bonds==bonds && f.pair_calls==pairs);
}
void DisconnectAttribution(unsigned variant) {
    Sandbox sandbox;f=FakeState{};f.system=f.native_requests=true;
    LibnxSession remote(Log,true,true,true,true,true);
    Start(remote);assert(!Advance(remote) && remote.Ready());
    f.cleanup_test=true;f.user_present=f.system_present=true;
    constexpr Result error=0x5528f;
    const char *expected=nullptr;
    switch (variant) {
    case 0:f.cleanup_system_result=error;expected="stage=system_presence Result=0005528f";break;
    case 1:f.cleanup_user_fail_at=1;expected="stage=user_presence Result=0005528f";break;
    case 2:f.cleanup_user_fail_at=2;expected="stage=subscription_binding index=0 Result=0005528f";break;
    case 3:f.cleanup_user_fail_at=3;expected="stage=subscription_binding index=1 Result=0005528f";break;
    case 4:f.native_fail_command=95;expected="stage=subscription_deregister index=0 Result=0005528f";break;
    case 5:f.cleanup_user_fail_at=4;expected="stage=final_binding Result=0005528f";break;
    case 6:f.disconnect_result=error;expected="stage=btm_disconnect Result=0005528f";break;
    case 7:expected="stage=system_presence Result=0005528f";break;
    case 8:expected="stage=user_presence Result=0005528f";break;
    }
    const auto reads=f.characteristic_reads,writes=f.writes,pairs=f.pair_calls,bonds=f.native_bonds;
    const auto begin=remote.BeginOwnedDisconnect(f.now);
    if (variant<7) {
        assert(begin && remote.GetDisconnectState()==DisconnectState::Failed);
        assert(f.owned_disconnects==unsigned(variant==6));
        assert(!Logged("OWNED_DISCONNECT requested handle="));
    } else {
        assert(!begin && remote.GetDisconnectState()==DisconnectState::Waiting && f.owned_disconnects==1);
        if (variant==7)f.cleanup_system_result=error;
        if (variant==8)f.cleanup_user_result=error;
        if (variant<9) {f.now+=100;assert(remote.PollOwnedDisconnect(f.now)==error);}
        else {
            for (unsigned i=0;i<40;++i) {f.now+=100;assert(!remote.PollOwnedDisconnect(f.now));}
            const auto count=std::count_if(f.logs.begin(),f.logs.end(),[](const auto &s) {
                return s.starts_with("DISCONNECT_STEP stage=system_presence") || s.starts_with("DISCONNECT_STEP stage=user_presence");
            });
            assert(count==2); // Unchanged polling must not flood the log.
            f.user_present=f.system_present=false;f.now+=100;assert(!remote.PollOwnedDisconnect(f.now));
            assert(remote.GetDisconnectState()==DisconnectState::Confirmed);
        }
    }
    if (expected)assert(Logged(expected) && remote.GetDisconnectState()==DisconnectState::Failed);
    for(const auto &s:f.logs)if(s.starts_with("DISCONNECT_STEP")) {
        assert(s.size()<192 && s.find("address=")==std::string::npos && s.find("key=")==std::string::npos);
    }
    const auto requests=f.owned_disconnects,queries=f.cleanup_queries;
    remote.BeginOwnedDisconnect(f.now);remote.PollOwnedDisconnect(f.now);
    assert(f.owned_disconnects==requests && f.cleanup_queries==queries);
    remote.Close();
    assert(f.owned_disconnects==requests && !f.disconnects);
    assert(f.characteristic_reads==reads && f.writes==writes && f.pair_calls==pairs && f.native_bonds==bonds);
    assert(std::filesystem::exists("sdmc:/config/amazon-remote/link-test-pending.txt"));
}
void SystemDisconnectContract(unsigned variant) {
    Sandbox sandbox;f=FakeState{};f.system=f.native_requests=true;f.cached_aruid=0;
    LibnxSession remote(Log,true,true,true,true,true);
    Start(remote);assert(!Advance(remote) && remote.Ready());
    f.cleanup_test=true;f.user_present=f.system_present=true;
    f.cleanup_final_variant=variant;
    const auto result=remote.BeginOwnedDisconnect(f.now);
    assert(bool(result)==(variant!=0));
    assert(f.owned_disconnects==unsigned(variant==0) && !f.user_disconnects);
    assert(f.system_opens==1 && !f.system_closes && !f.pair_calls && !f.native_bonds);
    if (!variant) {
        assert(remote.GetDisconnectState()==DisconnectState::Waiting);
        f.user_present=f.system_present=false;f.now+=100;
        assert(!remote.PollOwnedDisconnect(f.now));
        assert(remote.GetDisconnectState()==DisconnectState::Confirmed);
    } else assert(remote.GetDisconnectState()==DisconnectState::Failed);
    const auto requests=f.owned_disconnects;
    remote.BeginOwnedDisconnect(f.now);remote.PollOwnedDisconnect(f.now);remote.Close();
    assert(f.owned_disconnects==requests && !f.user_disconnects && !f.disconnects);
    assert(std::filesystem::exists("sdmc:/config/amazon-remote/link-test-pending.txt"));
}
void ConnectBoundaryReplay() {
    Sandbox sandbox;f=FakeState{};f.system=f.native_requests=true;
    LibnxSession remote(Log,true,true,true,true,true,true);
    assert(!remote.InitializeIdentityTest(f.target) && !remote.StartLinkOnlyScan(f.now));
    auto sighting=Sighting();f.now+=2977;f.preconnect_delay_ms=36;
    const auto rc=remote.ConfirmObservedLink(sighting,f.now,f.now+38000);
#ifdef ARMODULE_CONNECT_DISPATCH
    assert(!rc && f.connects==1 && Logged("P3_DISPATCH_REQUEST"));
    assert(remote.ConfirmObservedLink(sighting,f.now,f.now+38000) && f.connects==1);
#else
    assert(rc && !f.connects && Logged("Final presence gate expired/invalid"));
#endif
    assert(!f.pair_calls && !f.native_bonds && !f.registers && !f.writes);
    assert(std::filesystem::exists("sdmc:/config/amazon-remote/link-test-pending.txt"));
    remote.Close();
}
#ifdef ARMODULE_CONNECT_DISPATCH
void ConnectDispatchBoundaries(unsigned variant) {
    Sandbox sandbox;f=FakeState{};f.system=f.native_requests=true;
    LibnxSession remote(Log,true,true,true,true,true,true);
    assert(!remote.InitializeIdentityTest(f.target) && !remote.StartLinkOnlyScan(f.now));
    auto sighting=Sighting();f.now+=3000;
    auto deadline=f.now+38000, caller_now=f.now;
    bool admitted=false,marked=true;
    switch(variant) {
    case 0: f.preconnect_delay_ms=250;admitted=true;break;
    case 1: f.preconnect_delay_ms=251;break;
    case 2: ++f.now;marked=false;break;
    case 3: sighting.captured_boot_ms=f.now+1;marked=false;break;
    case 4: sighting.captured_boot_ms=9999;marked=false;break;
    case 5: f.preconnect_delay_ms=-1;break;
    case 6: deadline=f.now+36;f.preconnect_delay_ms=36;break;
    case 7: f.physical_log_delay_ms=100;f.marker_log_delay_ms=150;admitted=true;break;
    case 8: f.physical_log_delay_ms=100;f.marker_log_delay_ms=151;break;
    case 9: ++f.now;caller_now-=2000;marked=false;break;
    case 10: sighting.raw[7]^=1;marked=false;break;
    case 11: sighting.raw[323]=11;marked=false;break;
    case 12: sighting.raw[15]^=1;marked=false;break;
    case 13: deadline=f.now;marked=false;break;
    case 14: sighting.captured_boot_ms=f.now;f.preconnect_delay_ms=251;break;
    case 15: sighting.captured_boot_ms=f.now;admitted=true;break;
    case 16: sighting.version++;marked=false;break;
    }
    const auto rc=remote.ConfirmObservedLink(sighting,caller_now,deadline);
    assert(f.connects==unsigned(admitted) && (!admitted || !rc));
    assert(std::filesystem::exists("sdmc:/config/amazon-remote/link-test-pending.txt")==marked);
    assert(f.preconnect_system_queries==unsigned(marked));
    assert(!f.pair_calls && !f.native_bonds && !f.registers && !f.writes);
    if(marked) {
        assert(remote.ConfirmObservedLink(sighting,f.now,deadline));
        assert(f.connects==unsigned(admitted));
    }
    if(variant==2 || variant==9) {
        assert(!remote.LinkAttempted());
        // A genuinely fresh subsequent physical decision, not an auto-retry.
        assert(!remote.ConfirmObservedLink(Sighting(),f.now,f.now+38000) && f.connects==1);
    }
    remote.Close();assert(!f.disconnects && f.scan_stops==1);
}
#endif
int main() {
    ConnectBoundaryReplay();
#ifdef ARMODULE_CONNECT_DISPATCH
    for(unsigned v=0;v<17;++v)ConnectDispatchBoundaries(v);
    std::cout<<"PASS P3: live 2977+36ms regression and17 timing/identity/lease boundaries, real-clock admission,250ms budget including logs, no retry/bond/output. NOT hardware proof.\n";
#else
    std::cout<<"PASS legacy negative control: live2977+36ms sequence still rejects before connect.\n";
#endif
    for (unsigned variant=0;variant<9;++variant) SystemDisconnectContract(variant);
    std::cout<<"PASS D3 system disconnect:9 zero-applet-identity/final-binding cases; one existing system client, no user-call fallback, no bond or guard removal. NOT hardware proof.\n";
    for (unsigned variant=0;variant<10;++variant) DisconnectAttribution(variant);
    std::cout<<"PASS D2 cleanup attribution: same service error at nine distinct call sites, bounded unchanged-poll logging, no fallback or retry. NOT hardware proof.\n";
    for (unsigned variant=0;variant<25;++variant) OwnedDisconnectFlow(variant);
    std::cout<<"PASS owned-link teardown:25 actual adapter scenarios, accepted versus completed, dual-view absence, unrelated connection preservation, no address-only adoption, timeout/error/rebound guards, no bond mutation or automatic marker removal. NOT hardware proof.\n";
#ifdef ARMODULE_MANUFACTURER_SCAN_CHECK
    for(unsigned variant=0;variant<6;++variant) {
        Sandbox sandbox;f=FakeState{};f.system=f.native_requests=true;
        if(variant==1)f.general_start_result=0x888;
        if(variant==2)f.general_getter_result=0x888;
        if(variant==3)f.general_count=11;
        if(variant==4)f.smart_getter_result=0x888;
        if(variant==5)f.general_stop_result=0x888;
        LibnxSession remote(Log,true,true,true,true,true,true);
        assert(!remote.InitializeIdentityTest(f.target));
        const auto start=remote.StartLinkOnlyScan(f.now);
        assert(bool(start)==(variant==1) && f.general_starts==1 && !f.smart_starts);
        if(!start) {
            const auto polled=remote.Poll(f.now);
            assert(bool(polled)==(variant==2 || variant==3));
            if(!polled) {
                const auto reads=f.scan_getters;f.now+=499;
                assert(!remote.Poll(f.now) && f.scan_getters==reads);
            }
        }
        assert(!f.connects && !f.pair_calls && !f.native_bonds && !f.registers && !f.writes);
        remote.Close();
        assert(f.general_stops==unsigned(variant!=1) && !f.smart_stops);
        if(variant==5)assert(Logged("stop own filtered scan"));
    }
    std::cout<<"PASS manufacturer S2: exact8-byte public general filter, primary start/getter/count errors, secondary smart error isolation,500ms polling, corresponding own-stop only, no automatic link/bond/output. NOT hardware proof.\n";
#endif
#ifdef ARMODULE_SCAN_ASSISTED_CHECK
    for (unsigned variant=0;variant<6;++variant) {
        Sandbox sandbox;f=FakeState{};f.system=f.native_requests=true;
        LibnxSession remote(Log,true,true,true,true,true,true);
        assert(!remote.InitializeIdentityTest(f.target));
        assert(!remote.StartLinkOnlyScan(f.now));
        assert(f.scan_starts==1 && !f.connects && !f.pair_calls && !f.native_bonds);
        assert(!remote.Poll(f.now) && !f.connects); // Scanning never connects itself.
        auto sighting=Sighting();
        if(variant==1)sighting.captured_boot_ms-=3001;
        if(variant==2)sighting.raw[7]^=1;
        if(variant==3)sighting.raw[15]^=1;
        if(variant==4)sighting.raw[323]=11;
        if(variant==5){std::ofstream marker("sdmc:/config/amazon-remote/link-test-pending.txt");marker<<"preserved";}
        const auto rc=remote.ConfirmObservedLink(sighting,f.now,f.now+60000);
        assert(f.connects==unsigned(variant==0));
        if(variant==0) {
            assert(!rc && remote.ConfirmObservedLink(sighting,f.now,f.now+60000));
            assert(f.connects==1 && !Advance(remote) && remote.Ready());
            assert(remote.FingerprintObserved() && remote.SecurityReadObserved());
        }
        remote.Close();
        assert(f.scan_starts==1 && f.scan_stops==1 && !f.disconnects && !f.pair_calls && !f.native_bonds);
#ifdef ARMODULE_MANUFACTURER_SCAN_CHECK
        assert(f.general_starts==1 && f.general_stops==1 && !f.smart_starts && !f.smart_stops);
#else
        assert(f.smart_starts==1 && f.smart_stops==1 && !f.general_starts && !f.general_stops);
#endif
    }
    std::cout<<"PASS scan-assisted:6 actual SDK adapter cases; no auto-connect, one sighting-gated request, stale/wrong-address/wrong-ad/count/marker refusal, original fingerprint/input path and owned-scan cleanup. NOT hardware proof.\n";
#endif
#ifdef ARMODULE_KNOWN_RECONNECT_CHECK
    for (unsigned variant=0;variant<14;++variant) {
        Sandbox sandbox;f=FakeState{};f.system=f.native_requests=true;
        LibnxSession remote(Log,true,true,true,true,true,true);
        assert(!remote.InitializeIdentityTest(f.target));
        assert(!remote.PrepareKnownReconnect(f.now) && remote.KnownReconnectPrepared());
        assert(remote.PrepareKnownReconnect(f.now));
        assert(remote.StartLinkOnlyScan(f.now));
        if(variant<=8)f.preconnect_variant=variant;
        if(variant==7)f.connected=true;
        auto request_time=f.now,deadline=f.now+60000;
        if(variant==9)++request_time;
        if(variant==10)request_time-=3001;
        if(variant==11)++deadline;
        if(variant==12)deadline=f.now;
        if(variant==13){std::ofstream marker("sdmc:/config/amazon-remote/link-test-pending.txt");marker<<"preserved";}
        const auto rc=remote.ConfirmKnownReconnect(request_time,deadline);
        const bool admitted=variant==0 || variant==2;
        assert(bool(rc)==!admitted && f.connects==unsigned(admitted));
        assert(!f.scan_starts && !f.scan_stops && !f.scan_getters && !f.pair_calls && !f.native_bonds);
        if(admitted) {
            assert(remote.ConfirmKnownReconnect(f.now,deadline) && f.connects==1);
            assert(!Advance(remote) && remote.Ready() && remote.FingerprintObserved() && remote.SecurityReadObserved());
            const u8 press[]{0x52,0,0},release[]{0,0,0};
            Queue(4,0x1812,0x2a4d,0,press,sizeof press);f.now+=20;
            assert(!remote.Poll(f.now) && remote.Buttons()==Up);
            Queue(4,0x1812,0x2a4d,0,release,sizeof release);f.now+=20;
            assert(!remote.Poll(f.now) && !remote.Buttons());
        }
        remote.Close();assert(!f.scan_starts && !f.scan_stops && !f.scan_getters && !f.disconnects && !f.pair_calls && !f.native_bonds);
    }
    std::cout<<"PASS known reconnect:14 actual SDK adapter scenarios; no scan/cache operations, one explicit address request, existing system/user target/error/capacity/deadline/replay/marker refusal; original fingerprint/security/input press-release path. NOT device or bonding proof.\n";
#endif
    for (unsigned variant=0;variant<9;++variant) {
        Sandbox sandbox;f=FakeState{};f.system=f.native_requests=true;f.preconnect_variant=variant;
        LibnxSession remote(Log,true,true,true,true,true,true);
        assert(!remote.InitializeIdentityTest(f.target));
        assert(!remote.StartLinkOnlyScan(f.now));
        if (variant==7)f.connected=true; // The user view already owns the target.
        const auto sighting=Sighting();const auto before=f.now;
        const auto rc=remote.ConfirmObservedLink(sighting,before,before+60000);
        const bool admitted=variant==0 || variant==2;
        assert(bool(rc)==!admitted && f.connects==unsigned(admitted));
        assert(f.preconnect_system_queries==unsigned(variant!=7));
        assert(f.system_opens==1 && !f.system_closes && !f.pair_calls && !f.native_bonds && !f.native_calls);
        assert(std::filesystem::exists("sdmc:/config/amazon-remote/link-test-pending.txt"));
        if (variant==1 || variant==6)assert(Logged("PREEXISTING_SYSTEM_TARGET"));
        if (variant==8)assert(Logged("Final presence gate expired/invalid"));
        remote.Close();assert(!f.disconnects && f.system_closes==1);
    }
    std::cout<<"PASS preconnect system view:9 actual SDK adapter scenarios; system-only target, user target, error/capacity/overflow and expired presence refused; unrelated target permitted; one existing system client, no explicit bonding or foreign disconnect. NOT hardware proof.\n";
#ifdef ARMODULE_NATIVE_OWNER
    for(unsigned i=0;i<33;++i)NativeOwnerFlow(i);
    NativeOwnerFlow(35);
    std::cout<<"PASS native owner: 34 live SDK adapter scenarios; no extra clients/close/reconnect/pair route, exact command10 ABI, one-shot intent+revalidation, timeout/reassignment/error refusal. NOT device bond proof.\n";
#endif
#ifdef ARMODULE_NAV17
    assert(!InspectServiceUuid(16,nullptr).valid_length);
    assert(!InspectServiceUuid(0xffffffff,BtmManagedProfileUuid.data()).valid_length);
    assert(!InspectServiceUuid(17,BtmManagedProfileUuid.data()).valid_length);
    assert(InspectServiceUuid(16,BtmManagedProfileUuid.data()).managed_profile_match);
    assert(!InspectServiceUuid(2,BtmManagedProfileUuid.data()).managed_profile_match);
    for(unsigned variant=0;variant<7;++variant) {
        Sandbox sandbox;f=FakeState{};f.system=f.native_requests=true;
        LibnxSession remote(Log,true,true,true,true,true,true);Start(remote);
        assert(!Advance(remote) && remote.Ready());
        f.logs.clear();f.context_active=true;f.census_variant=variant;
        if(variant==6)f.cached_aruid=0;
        const auto opens=f.system_opens,reads=f.characteristic_reads,writes=f.writes,connects=f.connects;
        EnrollmentContextRecord r{};
        assert(!remote.ReadEnrollmentContext(42,r) && ValidEnrollmentContext(r));
        assert(!r.before_binding && !r.after_binding);
        assert(f.system_opens==opens && f.characteristic_reads==reads && f.writes==writes && f.connects==connects);
        assert(!f.pair_calls && !f.disconnects && Logged("SERVICE_CENSUS_BEGIN instance=42") && Logged("SERVICE_CENSUS_END instance=42"));
        assert(Logged(variant==6?"cached_client_aruid=0 aruid_zero=1":"cached_client_aruid=18446744073709551615 aruid_zero=0"));
        if(variant==1)assert(Logged("raw=6c1ce0b45d736a8a1a4ca54a263edcad managed_profile_match=1") && Logged("managed_profile_matches=1 cache_rows_complete=1"));
        if(variant==2)assert(Logged("raw=6d1ce0b45d736a8a1a4ca54a263edcad managed_profile_match=0"));
        if(variant==3)assert(Logged("size=17 valid=0 raw=INVALID") && Logged("invalid_uuid=1"));
        if(variant>=3 && variant<=5)assert(Logged("cache_rows_complete=0"));
        else assert(Logged("cache_rows_complete=1"));
        for(const auto &line:f.logs)if(line.starts_with("SERVICE_CENSUS_END"))assert(line.ends_with("physical_discovery_unverified=1"));
        const auto getters=f.system_getters;EnrollmentContextRecord duplicate{};
        assert(remote.ReadEnrollmentContext(42,duplicate) && f.system_getters==getters && !f.pair_calls);
        remote.Close();assert(!f.disconnects);
    }
    std::cout<<"PASS Navigation17 census:7 actual getter adapter scenarios, full128 UUID/ARUID, malformed/capacity/API errors, exact-target binding, bounded logging and no extra read/write/connect/pair commands. NOT hardware proof.\n";
#endif
    GoodFlow(); FingerprintUnchanged();
    for(unsigned variant=0;variant<3;++variant)SustainedFlow(variant);
#ifdef ARMODULE_RECONNECT_CHECK
    for(bool ready:{false,true})for(bool instance:{false,true})for(bool persist:{false,true}) {
        Sandbox sandbox;f=FakeState{};f.system=f.native_requests=true;
        LibnxSession remote(Log,true,true,true,true,true,true);
        if(ready){Start(remote);assert(!Advance(remote) && remote.Ready());}
        const auto connections=f.connects,reads=f.characteristic_reads,writes=f.writes;
        assert(remote.EnrollOnce(instance?42:0,persist?EnrollmentIntentPersist:nullptr));
        assert(!f.pair_calls && !f.native_bonds && !f.intent_calls && !f.paired_getters);
        assert(f.connects==connections && f.characteristic_reads==reads && f.writes==writes);
        remote.Close();assert(!f.disconnects);
    }
    std::cout<<"PASS reconnect-only transport:8 direct enrollment refusal cases, no intent/client/connect/read/write/pair side effects; normal fingerprint/input path preserved. NOT stored-bond proof.\n";
#else
    for(bool system:{false,true})for(unsigned variant=0;variant<23;++variant)EnrollmentFlow(variant,system);
    EnrollmentServiceGuards();
#endif
    for(unsigned variant=0;variant<15;++variant) {
        Sandbox sandbox;f=FakeState{};f.system=f.native_requests=true;
        LibnxSession remote(Log,true,true,true,true,true,true);Start(remote);
        if(variant!=10)assert(!Advance(remote) && remote.Ready());
        if(variant==11){const u8 keyboard[]{0x52,0,0};Queue(4,0x1812,0x2a4d,0,keyboard,sizeof keyboard);f.now+=20;assert(!remote.Poll(f.now));}
        if(variant==12)f.now+=1800000;
        if(variant==14)remote.Close();
        f.context_active=true;f.context_variant=variant;
        const auto opens=f.user_opens,closes=f.user_closes,system_closes=f.system_closes;
        const auto connects=f.connects,registers=f.registers,writes=f.writes,reads=f.characteristic_reads;
        EnrollmentContextRecord r{};const auto rc=remote.ReadEnrollmentContext(variant==13?0:42,r);
        assert(!f.pair_calls && !f.intent_calls && f.connects==connects && f.registers==registers && f.writes==writes && f.characteristic_reads==reads);
        assert(f.user_opens==opens && f.user_closes==closes && f.system_opens==1 && f.system_closes==system_closes);
        if(variant>=10)assert(rc && !f.sys_opens && !f.system_connection_queries);
        else {
            assert(!rc && ValidEnrollmentContext(r) && r.instance==42 && r.expected_handle==4 && !r.before_binding);
            assert(f.sys_opens==1 && f.sys_closes==unsigned(variant!=5) && f.flag_queries==unsigned(variant!=5));
            assert(f.paired_getters==2 && f.system_connection_queries==1);
            const auto relation=araudit::Compare(r.snapshot);
            assert(relation==(variant==1?araudit::ConnectionRelation::UserOnly:variant==2?araudit::ConnectionRelation::DifferentHandle:
                (variant==3 || variant==4 || variant==9)?araudit::ConnectionRelation::Indeterminate:araudit::ConnectionRelation::SameHandle));
            assert(bool(r.after_binding)==(variant==8));
            assert(r.snapshot.pairing_started==unsigned(variant==7));
            if(variant==5)assert(r.snapshot.sys_open==72 && r.snapshot.pairing_query==araudit::NotAttempted);
            if(variant==6)assert(r.snapshot.pairing_query==73 && !r.snapshot.pairing_started);
        }
        const auto getters=f.system_connection_queries;EnrollmentContextRecord duplicate{};
        assert(remote.ReadEnrollmentContext(42,duplicate) && f.system_connection_queries==getters && !f.pair_calls);
        remote.Close();assert(!f.disconnects);
    }
    std::cout<<"PASS same-session enrollment context:15 actual adapter scenarios; borrowed clients preserved, binding checks, independent query errors, clock bounds, neutral/READY guards, one-shot and zero pairing requests. NOT hardware proof.\n";
#ifndef ARMODULE_RECONNECT_CHECK
    std::cout<<"PASS enrollment transport:46 scenarios across fixed user/system routes; no automatic/duplicate/fallback request; exact target/neutral/fingerprint guards and bounded list observation. NOT hardware proof.\n";
#endif
    for (unsigned variant=0;variant<6;++variant) SystemFlow(variant);
    for(unsigned command:{90u,91u,93u,94u})NativeRequestFailure(command);
    NativeWhitelist();
    NativeOpenFailure();
    for(auto scenario : {ZeroProperties,DuplicateInstances,SecurityError,SecurityTimeout,TruncatedMetadata,BadDescriptor,BadCccCompletion}) FailureFlow(scenario);
    for(unsigned variant=0;variant<6;++variant) NotificationFailure(variant);
    for(auto scenario : {PreBindingDelay,PostBindingDelay,PreMetadataDelay}) MetadataDeadline(scenario);
    for(auto scenario : {DriverRecovered,DriverOpenError,DriverReadError,DriverWrongUuid,DriverRepeatedInstance,DriverUnknownInstance,DriverConflict,DriverBindingDelay,DriverReassigned,DriverZeroSelected,DriverDeviceShape}) DriverFlow(scenario);
    std::cout << "PASS actual LibnxSession adapter under fake SDK: ordered fingerprint/security/metadata/CCC, button decoding, zero/ambiguous metadata, security errors/timeouts, invalid notifications, fixed session expiry and target-safe cleanup. NOT hardware proof.\n";
    return 0;
}
