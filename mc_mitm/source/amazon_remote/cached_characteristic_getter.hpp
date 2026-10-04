// Getter-only public btdrv IPC, matching pinned libnx btdrv.c commands80/81.
// GPL-2.0-only. No btdrvInitialize: that wrapper sends global command0.
// Wire contract reference: lib/libnx/nx/source/services/btdrv.c (libnx Authors,
// ISC license in lib/libnx/LICENSE.md). No private MissionControl BLE code used.
#pragma once
#include <switch.h>
#include <cstddef>
namespace amazon_remote {
struct CachedFirstIn {
    u8 primary; u8 pad[3]; u32 connection; BtdrvGattId service;
    BtdrvGattAttributeUuid filter;
};
struct CachedNextIn {
    u8 primary; u8 pad[3]; u32 connection; BtdrvGattId service, previous;
    BtdrvGattAttributeUuid filter;
};
struct CachedCharacteristicOut { u8 properties; u8 pad[3]; BtdrvGattId id; };
static_assert(sizeof(CachedFirstIn)==0x34 && sizeof(CachedNextIn)==0x4c);
static_assert(sizeof(CachedCharacteristicOut)==0x1c && offsetof(CachedCharacteristicOut,id)==4);
class CachedCharacteristicGetter {
public:
    CachedCharacteristicGetter() = default;
    CachedCharacteristicGetter(const CachedCharacteristicGetter &) = delete;
    CachedCharacteristicGetter &operator=(const CachedCharacteristicGetter &) = delete;
    ~CachedCharacteristicGetter() { if(serviceIsActive(&service_)) serviceClose(&service_); }
    Result Open() {
        if(hosversionBefore(5,1,0)) return MAKERESULT(Module_Libnx,LibnxError_IncompatSysVer);
        // Obtain only a service handle; no initialization, event or radio calls.
        return smGetService(&service_,"btdrv");
    }
    Result First(u32 connection,const BtdrvGattId &service,const BtdrvGattAttributeUuid &filter,CachedCharacteristicOut &out) {
        if(!serviceIsActive(&service_)) return MAKERESULT(Module_Libnx,LibnxError_NotInitialized);
        const CachedFirstIn in{1,{0},connection,service,filter}; out={};
        return serviceDispatchInOut(&service_,80,in,out);
    }
    Result Next(u32 connection,const BtdrvGattId &service,const BtdrvGattId &previous,const BtdrvGattAttributeUuid &filter,CachedCharacteristicOut &out) {
        if(!serviceIsActive(&service_)) return MAKERESULT(Module_Libnx,LibnxError_NotInitialized);
        const CachedNextIn in{1,{0},connection,service,previous,filter}; out={};
        return serviceDispatchInOut(&service_,81,in,out);
    }
private:
    Service service_{};
};
}
