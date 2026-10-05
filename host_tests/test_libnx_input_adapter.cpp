#define _DARWIN_C_SOURCE 1
#include "../mc_mitm/source/amazon_remote/libnx_session.hpp"
#include "../mc_mitm/source/amazon_remote/k7q3m7_descriptor.hpp"
#include "../mc_mitm/source/amazon_remote/cached_characteristic_getter.hpp"
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
#include <unistd.h>
using namespace amazon_remote;
namespace {
enum Scenario { Good, ZeroProperties, DuplicateInstances, SecurityError, SecurityTimeout, TruncatedMetadata, BadDescriptor, BadCccCompletion, PreBindingDelay, PostBindingDelay, PreMetadataDelay,
    DriverRecovered, DriverOpenError, DriverReadError, DriverWrongUuid, DriverRepeatedInstance, DriverUnknownInstance, DriverConflict, DriverBindingDelay, DriverReassigned, DriverZeroSelected, DriverDeviceShape };
struct FakeState {
    Scenario scenario=Good; u64 now=10000;
    BtdrvAddress target{{0x02,0xab,0xcd,0x12,0x34,0x56}}, address=target;
    bool connected=false, discovery=false, scan=false, security_requested=false, delayed=false;
    unsigned connects=0, characteristic_reads=0, security_reads=0, descriptor_reads=0, descriptor_getters=0;
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
    std::deque<BtdrvBleEventInfo> events; std::vector<std::string> logs;
} f;
BtdrvGattAttributeUuid U(u16 value) { BtdrvGattAttributeUuid u{}; u.size=2; u.uuid[0]=value&255; u.uuid[1]=value>>8; return u; }
u16 Short(const BtdrvGattAttributeUuid &u) { return u.size==2 ? u.uuid[0]|u16(u.uuid[1])<<8 : 0; }
Result Invalid() { return MAKERESULT(Module_Libnx,LibnxError_BadInput); }
Result Timeout() { return MAKERESULT(Module_Kernel,KernelError_TimedOut); }
void Log(const char *line) { f.logs.emplace_back(line); }
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
    const u8 keyboard[]{0x52,0x58,0}, released[]{0,0,0}, consumer[]{0x23,2,0,0};
    Queue(4,0x1812,0x2a4d,0,keyboard,sizeof keyboard); assert(!Advance(remote,1));
    // Advance stops at Ready; explicit runtime polling is needed for reports.
    f.now+=20; assert(!remote.Poll(f.now)); assert((remote.Buttons()&(Up|Confirm))==(Up|Confirm));
    Queue(4,0x1812,0x2a4d,0,consumer,sizeof consumer); f.now+=20; assert(!remote.Poll(f.now)); assert(remote.Buttons()&Home);
    Queue(4,0x1812,0x2a4d,0,released,sizeof released); f.now+=20; assert(!remote.Poll(f.now)); assert(!(remote.Buttons()&(Up|Confirm)) && (remote.Buttons()&Home));
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
void serviceClose(Service *s) { assert(s->id==80); s->id=0; ++f.driver_closes; }
bool hosversionBefore(unsigned,unsigned,unsigned) { return false; }
Result smGetService(Service *s,const char *name) {
    assert(std::string(name)=="btdrv" && !s->id); ++f.driver_opens;
    if(f.scenario==DriverOpenError) return Invalid();
    s->id=80;
    return 0;
}
Result FakeServiceDispatch(Service *s,u32 cmd,const void *input,std::size_t input_size,void *output,std::size_t output_size) {
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
Result btmuStartBleScanForSmartDevice(const BtdrvGattAttributeUuid *uuid) { assert(uuid->size==16); f.scan=true; return 0; }
Result btdevStartBleScanSmartDevice(const BtdrvGattAttributeUuid *uuid) { return btmuStartBleScanForSmartDevice(uuid); }
Result btmuStopBleScanForSmartDevice() { f.scan=false; return 0; } Result btdevStopBleScanSmartDevice() { return btmuStopBleScanForSmartDevice(); }
Result btdevGetBleConnectionInfoList(BtdrvBleConnectionInfo *info,u8 count,u8 *total) { return btmuBleGetConnectionState(info,count,total); }
Result btmuBleGetConnectionState(BtdrvBleConnectionInfo *info,u8 count,u8 *total) {
    if(!f.delayed && ((f.scenario==PreBindingDelay && Logged("FINGERPRINT_VERIFIED")) ||
                     (f.scenario==PostBindingDelay && Logged("SECURITY_READ_COMPLETED")))) {
        f.delayed=true; f.now+=90000;
    }
    assert(count>=1); *total=f.connected ? 1 : 0;
    if(f.connected) info[0]={4,f.address,{}};
    return 0;
}
Result btmuGetBleScanResultsForGeneral(BtdrvBleScanResult*,u8,u8 *total) { *total=0; return 0; }
Result btmuGetBleScanResultsForSmartDevice(BtdrvBleScanResult*,u8,u8 *total) { *total=0; return 0; }
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
    assert(f.context_active && f.system_opens==1 && !f.system_closes && count==16);++f.system_connection_queries;
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
    } return 0;
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
    f.fixture=true;const auto rc=btdevGetGattServices(connection,fixture.data(),count,total);f.fixture=false;
    for (u8 i=0;i<*total;++i) { out[i]={};out[i].uuid=fixture[i].attr.uuid;out[i].handle=fixture[i].attr.handle;
        out[i].instance_id=fixture[i].instance_id;out[i].end_group_handle=fixture[i].end_group_handle;out[i].primary_service=fixture[i].primary_service; }
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
    assert(s->id==80 && f.native_requests && !p.in_send_pid);
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
int main() {
    GoodFlow(); FingerprintUnchanged();
    for(unsigned variant=0;variant<3;++variant)SustainedFlow(variant);
    for(bool system:{false,true})for(unsigned variant=0;variant<23;++variant)EnrollmentFlow(variant,system);
    EnrollmentServiceGuards();
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
    std::cout<<"PASS enrollment transport:46 scenarios across fixed user/system routes; no automatic/duplicate/fallback request; exact target/neutral/fingerprint guards and bounded list observation. NOT hardware proof.\n";
    for (unsigned variant=0;variant<6;++variant) SystemFlow(variant);
    for(unsigned command:{90u,91u,93u,94u})NativeRequestFailure(command);
    NativeWhitelist();
    NativeOpenFailure();
    for(auto scenario : {ZeroProperties,DuplicateInstances,SecurityError,SecurityTimeout,TruncatedMetadata,BadDescriptor,BadCccCompletion}) FailureFlow(scenario);
    for(unsigned variant=0;variant<6;++variant) NotificationFailure(variant);
    for(auto scenario : {PreBindingDelay,PostBindingDelay,PreMetadataDelay}) MetadataDeadline(scenario);
    for(auto scenario : {DriverRecovered,DriverOpenError,DriverReadError,DriverWrongUuid,DriverRepeatedInstance,DriverUnknownInstance,DriverConflict,DriverBindingDelay,DriverReassigned,DriverZeroSelected,DriverDeviceShape}) DriverFlow(scenario);
    std::cout << "PASS actual LibnxSession adapter under fake SDK: ordered fingerprint/security/metadata/CCC, button decoding, zero/ambiguous metadata, security errors/timeouts, invalid notifications, fixed session expiry and target-safe cleanup. NOT hardware proof.\n";
}
