// Host-only link seam for the actual LibnxSession adapter. NOT a runtime SDK.
// Only contracts used by this adapter are modeled. Production still builds
// against pinned real libnx; fake events cannot establish Horizon ABI behavior.
#pragma once
#include <cstddef>
#include <cstdint>
using u8=std::uint8_t; using u16=std::uint16_t; using u32=std::uint32_t; using u64=std::uint64_t;
using Result=u32;
u64 appletGetAppletResourceUserId();
using s32=std::int32_t;
inline constexpr unsigned Module_Libnx=345, Module_Kernel=1;
inline constexpr unsigned LibnxError_BadInput=11, LibnxError_NotInitialized=12, LibnxError_IncompatSysVer=13, KernelError_TimedOut=117;
constexpr Result MAKERESULT(unsigned module,unsigned description) { return module|(description<<9); }
inline bool R_FAILED(Result result) { return result!=0; }
inline bool R_SUCCEEDED(Result result) { return result==0; }
struct Event { u32 id{}; };
struct Service { u32 id{}; };
bool serviceIsActive(Service*); void serviceClose(Service*);
bool hosversionBefore(unsigned,unsigned,unsigned);
#define MAKEHOSVERSION(a,b,c) (((a)<<16)|((b)<<8)|(c))
u32 hosversionGet();
Result smGetService(Service*,const char*);
Result FakeServiceDispatch(Service*,u32,const void*,std::size_t,void*,std::size_t);
inline constexpr unsigned SfBufferAttr_HipcPointer=1,SfBufferAttr_In=2,SfBufferAttr_Out=4;
#define serviceDispatch(srv,cmd,...) FakeServiceDispatchInput(srv,cmd,nullptr,0,FakeDispatchParams{__VA_ARGS__})
struct FakeDispatchParams { unsigned buffer_attrs[1]{};struct { const void *data{};std::size_t size{}; } buffers[1]{};bool in_send_pid{}; };
Result FakeServiceDispatchInput(Service*,u32,const void*,std::size_t,FakeDispatchParams);
#define serviceDispatchIn(srv,cmd,input,...) FakeServiceDispatchInput(srv,cmd,&(input),sizeof(input),FakeDispatchParams{__VA_ARGS__})
#define serviceDispatchInOut(srv,cmd,input,output) FakeServiceDispatch(srv,cmd,&(input),sizeof(input),&(output),sizeof(output))
#define serviceDispatchOut(srv,cmd,output) FakeServiceDispatch(srv,cmd,nullptr,0,&(output),sizeof(output))
struct BtdrvAddress { u8 address[6]{}; };
struct BtdrvGattAttributeUuid { u32 size{}; u8 uuid[16]{}; };
struct BtdrvGattId { u8 instance_id{}; u8 pad[3]{}; BtdrvGattAttributeUuid uuid{}; };
struct BtdrvBleScanResult { u8 unknown{}; BtdrvAddress addr{}; u8 padding[0x139]{}; u32 count{}, tail{}; };
struct BtdrvBleConnectionInfo { u32 connection_handle{}; BtdrvAddress addr{}; u8 pad[2]{}; };
enum BtmProfile {BtmProfile_Hid=1};
struct BtmDeviceCondition {BtdrvAddress address{};u32 profile{};};
Result btmGetDeviceCondition(BtmProfile,BtmDeviceCondition*,std::size_t,s32*);
Result btmBleGetConnectionState(BtdrvBleConnectionInfo*,u8,u8*);
Result btmsysInitialize();void btmsysExit();Result btmsysIsGamepadPairingStarted(bool*);
enum BtdrvGattAuthReqType { BtdrvGattAuthReqType_None=0, BtdrvGattAuthReqType_NoMitm=1 };
enum BtdrvBleEventType { BtdrvBleEventType_ClientConnection=4, BtdrvBleEventType_ClientNotify=8 };
struct FakeNotify {
    u32 result{},conn_id{}; u8 type{},pad[3]{};
    BtdrvGattAttributeUuid serv_uuid{},char_uuid{},desc_uuid{};
    u16 size{}; u8 data[0x200]{},tail[2]{};
};
struct FakeClientConnection { u32 result{};u8 status{},client_if{},pad[2]{};u32 conn_id{};BtdrvAddress address{};u16 reason{}; };
struct BtdrvBleEventInfo { union { u8 data[0x400]; FakeNotify client_notify; FakeClientConnection client_connection; }; };
struct BtdrvLeEventInfo {
    u32 unk_x0{},unk_x4{}; u8 unk_x8{},pad[3]{};
    BtdrvGattAttributeUuid uuid0{},uuid1{},uuid2{};
    u16 size{}; u8 data[0x3b6]{};
};
struct BtdevGattAttribute { u8 type{}; BtdrvGattAttributeUuid uuid{}; u16 handle{}; u32 connection_handle{}; };
struct BtdevGattService { BtdevGattAttribute attr{}; u16 instance_id{},end_group_handle{}; bool primary_service{}; };
struct BtdevGattCharacteristic { BtdevGattAttribute attr{}; u16 instance_id{}; u8 properties{}; };
struct BtdevGattDescriptor { BtdevGattAttribute attr{}; };
struct BtmGattCharacteristic { u8 unknown[4]{}; BtdrvGattAttributeUuid uuid{}; u16 handle{}; u8 pad[2]{}; u16 instance_id{}; u8 properties{},tail[5]{}; };
struct BtmGattDescriptor { u8 unknown[4]{}; BtdrvGattAttributeUuid uuid{}; u16 handle{}; u8 tail[6]{}; };
struct BtmGattService { BtdrvGattAttributeUuid uuid{}; u16 handle{},instance_id{},end_group_handle{}; u8 primary_service{}; };
Result btmInitialize(); void btmExit();
Result btmConfigureBleMtu(u32,u16); Result btmGetBleMtu(u32,u16*);
Result btmGetGattServices(u32,BtmGattService*,u8,u8*);
Result btmGetGattCharacteristics(u32,u16,BtmGattCharacteristic*,u8,u8*);
Result btmGetGattDescriptors(u32,u16,BtmGattDescriptor*,u8,u8*);
u64 armGetSystemTick(); u64 armTicksToNs(u64 ticks);
Result eventWait(Event*,u64); void eventClose(Event*);
Result btmuInitialize(); void btmuExit(); Result btdevInitialize(); void btdevExit();
Result btdevAcquireBleGattOperationEvent(Event*); Result btdevAcquireBleServiceDiscoveryEvent(Event*);
Result btdevAcquireBleConnectionStateChangedEvent(Event*); Result btdevAcquireBlePairingEvent(Event*);
Result btmuAcquireBleConnectionEvent(Event*); Result btmuAcquireBleScanEvent(Event*); Result btdevAcquireBleScanEvent(Event*);
Result btdevRegisterGattOperationNotification(const BtdrvGattAttributeUuid*);
Result btdevUnregisterGattOperationNotification(const BtdrvGattAttributeUuid*);
Result btmuStartBleScanForSmartDevice(const BtdrvGattAttributeUuid*); Result btdevStartBleScanSmartDevice(const BtdrvGattAttributeUuid*); Result btmuStopBleScanForSmartDevice(); Result btdevStopBleScanSmartDevice();
Result btdevGetBleConnectionInfoList(BtdrvBleConnectionInfo*,u8,u8*);
Result btmuBleGetConnectionState(BtdrvBleConnectionInfo*,u8,u8*);
Result btmuBleDisconnect(u32);
Result btmBleDisconnect(u32);
struct BtdrvBleAdvertisePacketParameter {u16 company_id{};u8 pattern_data[6]{};};
Result btmuStartBleScanForGeneral(BtdrvBleAdvertisePacketParameter);
Result btmuStopBleScanForGeneral();
Result btmuBleGetPairedDevices(BtdrvBleAdvertisePacketParameter,BtdrvAddress*,u8,u8*);
Result btmuBlePairDevice(u32,BtdrvBleAdvertisePacketParameter);
Result btmBlePairDevice(u32,BtdrvBleAdvertisePacketParameter);
Result btmBleGetPairedAddresses(BtdrvBleAdvertisePacketParameter,BtdrvAddress*,u8,u8*);
Result btmuGetBleScanResultsForGeneral(BtdrvBleScanResult*,u8,u8*);
Result btmuGetBleScanResultsForSmartDevice(BtdrvBleScanResult*,u8,u8*);
Result btmuBleConnect(BtdrvAddress); Result btdevConnectToGattServer(BtdrvAddress); Result btdevDisconnectFromGattServer(u32);
Result btdevConfigureBleMtu(u32,u16); Result btdevGetBleMtu(u32,u16*);
Result btdevGetGattServices(u32,BtdevGattService*,u8,u8*);
Result btdevGattServiceGetCharacteristics(BtdevGattService*,BtdevGattCharacteristic*,u8,u8*);
Result btdevGattCharacteristicGetDescriptors(BtdevGattCharacteristic*,BtdevGattDescriptor*,u8,u8*);
Result btmuGetGattCharacteristics(u32,u16,BtmGattCharacteristic*,u8,u8*);
Result btmuGetGattDescriptors(u32,u16,BtmGattDescriptor*,u8,u8*);
Result btLeClientReadCharacteristic(u32,bool,const BtdrvGattId*,const BtdrvGattId*,u8);
Result btLeClientReadDescriptor(u32,bool,const BtdrvGattId*,const BtdrvGattId*,const BtdrvGattId*,u8);
Result btLeClientRegisterNotification(u32,bool,const BtdrvGattId*,const BtdrvGattId*);
Result btLeClientDeregisterNotification(u32,bool,const BtdrvGattId*,const BtdrvGattId*);
Result btLeClientWriteDescriptor(u32,bool,const BtdrvGattId*,const BtdrvGattId*,const BtdrvGattId*,const void*,std::size_t,u8);
Result btGetLeEventInfo(void*,std::size_t,BtdrvBleEventType*);
