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
    if(f.scenario==DriverOpenError) return Invalid(); s->id=80; return 0;
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
Result btmuInitialize() { return 0; } void btmuExit() {}
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
    if(f.connected) info[0]={4,f.address,{}}; return 0;
}
Result btmuGetBleScanResultsForGeneral(BtdrvBleScanResult*,u8,u8 *total) { *total=0; return 0; }
Result btmuGetBleScanResultsForSmartDevice(BtdrvBleScanResult*,u8,u8 *total) { *total=0; return 0; }
Result btmuBleConnect(BtdrvAddress target) { assert(!std::memcmp(&target,&f.target,sizeof target)); ++f.connects; f.connected=f.discovery=true; return 0; }
Result btdevConnectToGattServer(BtdrvAddress target) { return btmuBleConnect(target); }
Result btdevDisconnectFromGattServer(u32) { ++f.disconnects; return Invalid(); }
Result btdevConfigureBleMtu(u32 connection,u16 mtu) { assert(connection==4 && mtu==512); return 0; }
Result btdevGetBleMtu(u32 connection,u16 *mtu) { assert(connection==4); *mtu=247; return 0; }
Result btdevGetGattServices(u32 connection,BtdevGattService *services,u8 count,u8 *total) {
    assert(connection==4 && count>=3); *total=3;
    services[0]={{3,U(0x180a),0x12,4},0,0x24,true};
    services[1]={{3,U(0x1812),0x25,4},0,0x41,true};
    services[2]={{3,U(0x180f),0x26,4},0,0,false}; return 0;
}
Result btdevGattServiceGetCharacteristics(BtdevGattService *service,BtdevGattCharacteristic *chars,u8 count,u8 *total) {
    assert(count>=1); *total=1;
    const bool hid=Short(service->attr.uuid)==0x1812;
    chars[0]={{1,U(hid?0x2a4b:0x2a50),u16(hid?0x2e:0x24),4},0,0}; return 0;
}
Result btdevGattCharacteristicGetDescriptors(BtdevGattCharacteristic*,BtdevGattDescriptor*,u8,u8*) { return Invalid(); }
Result btmuGetGattCharacteristics(u32 connection,u16 service,BtmGattCharacteristic *chars,u8 count,u8 *total) {
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
    assert(connection==4 && count>=2 && characteristic>=0x30 && characteristic<=(f.scenario==DriverDeviceShape ? 0x3f : 0x38)); ++f.descriptor_getters;
    if(f.scenario==DriverDeviceShape && characteristic==0x38) { *total=1; descs[0].handle=0x39; descs[0].uuid=U(0x2908); return 0; }
    if(f.scenario==BadDescriptor) { *total=1; descs[0].handle=characteristic; descs[0].uuid=U(0x2908); return 0; }
    *total=2; descs[0].handle=characteristic+1; descs[0].uuid=U(0x2902);
    descs[1].handle=characteristic+2; descs[1].uuid=U(0x2908); return 0;
}
Result btLeClientReadCharacteristic(u32 connection,bool primary,const BtdrvGattId *service,const BtdrvGattId *characteristic,u8 auth) {
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
    assert(connection==4 && primary && Short(service->uuid)==0x1812 && Short(characteristic->uuid)==0x2a4d);
    assert(f.scenario==DriverDeviceShape ? (characteristic->instance_id==0 || characteristic->instance_id==3) : characteristic->instance_id<2); ++f.registers; return 0;
}
Result btLeClientDeregisterNotification(u32 connection,bool primary,const BtdrvGattId*,const BtdrvGattId *characteristic) {
    assert(connection==4 && primary && f.connected && !std::memcmp(&f.address,&f.target,sizeof f.target));
    assert(f.scenario==DriverDeviceShape ? (characteristic->instance_id==0 || characteristic->instance_id==3) : characteristic->instance_id<2); ++f.deregisters; return 0;
}
Result btLeClientWriteDescriptor(u32 connection,bool primary,const BtdrvGattId *service,const BtdrvGattId *characteristic,const BtdrvGattId *descriptor,const void *data,std::size_t size,u8 auth) {
    assert(connection==4 && primary && Short(service->uuid)==0x1812 && Short(characteristic->uuid)==0x2a4d && Short(descriptor->uuid)==0x2902 && auth==1);
    assert(f.scenario==DriverDeviceShape ? (characteristic->instance_id==0 || characteristic->instance_id==3) : characteristic->instance_id<2);
    const auto *value=static_cast<const u8*>(data); assert(size==2 && value[0]==1 && value[1]==0); ++f.writes;
    Queue(3,0x1812,0x2a4d,0x2902,nullptr,0,f.scenario==BadCccCompletion ? 5 : 0); return 0;
}
Result btGetLeEventInfo(void *out,std::size_t size,BtdrvBleEventType *type) {
    assert(size==sizeof(BtdrvBleEventInfo) && !f.events.empty()); std::memcpy(out,&f.events.front(),size); f.events.pop_front(); *type=BtdrvBleEventType_ClientNotify; return 0;
}
int main() {
    GoodFlow(); FingerprintUnchanged();
    for(auto scenario : {ZeroProperties,DuplicateInstances,SecurityError,SecurityTimeout,TruncatedMetadata,BadDescriptor,BadCccCompletion}) FailureFlow(scenario);
    for(unsigned variant=0;variant<6;++variant) NotificationFailure(variant);
    for(auto scenario : {PreBindingDelay,PostBindingDelay,PreMetadataDelay}) MetadataDeadline(scenario);
    for(auto scenario : {DriverRecovered,DriverOpenError,DriverReadError,DriverWrongUuid,DriverRepeatedInstance,DriverUnknownInstance,DriverConflict,DriverBindingDelay,DriverReassigned,DriverZeroSelected,DriverDeviceShape}) DriverFlow(scenario);
    std::cout << "PASS actual LibnxSession adapter under fake SDK: ordered fingerprint/security/metadata/CCC, button decoding, zero/ambiguous metadata, security errors/timeouts, invalid notifications, fixed session expiry and target-safe cleanup. NOT hardware proof.\n";
}
