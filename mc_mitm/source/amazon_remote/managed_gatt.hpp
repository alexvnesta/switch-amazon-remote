// SPDX-License-Identifier: GPL-2.0-only
// Public libnx BTM APIs for an explicitly requested background session.
// No ARUID borrowing, driver initialization, event interception or radio writes.
#pragma once
#include <switch.h>
#include <array>
namespace amazon_remote {
class ManagedGatt {
    bool system_{},open_{};
    static Result Invalid() { return MAKERESULT(Module_Libnx,LibnxError_BadInput); }
public:
    explicit ManagedGatt(bool system=false):system_(system) {}
    bool System() const { return system_; }
    Result Open() { if (!system_) return 0; const auto rc=btmInitialize();open_=R_SUCCEEDED(rc);return rc; }
    void Close() { if (open_) btmExit();open_=false; }
    Result PairDevice(u32 connection,BtdrvBleAdvertisePacketParameter param) {
        if(!system_ || !open_ || !connection || connection==0xffffffff)return Invalid();
        return btmBlePairDevice(connection,param);
    }
    Result PairedAddresses(BtdrvBleAdvertisePacketParameter param,BtdrvAddress *out,u8 count,u8 *total) {
        if(!system_ || !open_ || !out || !count || count>10 || !total)return Invalid();
        return btmBleGetPairedAddresses(param,out,count,total);
    }
    Result ConfigureMtu(u32 connection,u16 mtu) {
        if (!connection || mtu<24 || mtu>512 || (system_ && !open_)) return Invalid();
        return system_?btmConfigureBleMtu(connection,mtu):btdevConfigureBleMtu(connection,mtu);
    }
    Result GetMtu(u32 connection,u16 *out) {
        if (!connection || !out || (system_ && !open_)) return Invalid();
        return system_?btmGetBleMtu(connection,out):btdevGetBleMtu(connection,out);
    }
    Result RawCharacteristics(u32 connection,u16 handle,BtmGattCharacteristic *out,u8 count,u8 *total) {
        if (!connection || !handle || !out || !count || count>100 || !total || (system_ && !open_)) return Invalid();
        return system_?btmGetGattCharacteristics(connection,handle,out,count,total):btmuGetGattCharacteristics(connection,handle,out,count,total);
    }
    Result RawDescriptors(u32 connection,u16 handle,BtmGattDescriptor *out,u8 count,u8 *total) {
        if (!connection || !handle || !out || !count || count>100 || !total || (system_ && !open_)) return Invalid();
        return system_?btmGetGattDescriptors(connection,handle,out,count,total):btmuGetGattDescriptors(connection,handle,out,count,total);
    }
    Result Services(u32 connection,BtdevGattService *out,u8 count,u8 *total) {
        if (!system_) return btdevGetGattServices(connection,out,count,total);
        if (!open_ || !connection || !out || !count || count>100 || !total) return Invalid();
        std::array<BtmGattService,100> raw{};u8 n{};
        const auto rc=btmGetGattServices(connection,raw.data(),raw.size(),&n);
        if (R_FAILED(rc)) return rc;
        if (n>=raw.size() || n>count) return Invalid(); // never truncate an ambiguous inventory
        for (u8 i=0;i<n;++i) {
            if (raw[i].primary_service>1) return Invalid();
            out[i]={};out[i].attr.type=3;out[i].attr.uuid=raw[i].uuid;
            out[i].attr.handle=raw[i].handle;out[i].attr.connection_handle=connection;
            out[i].instance_id=raw[i].instance_id;out[i].end_group_handle=raw[i].end_group_handle;
            out[i].primary_service=raw[i].primary_service;
        }
        *total=n;return 0;
    }
    Result Characteristics(BtdevGattService *s,BtdevGattCharacteristic *out,u8 count,u8 *total) {
        if (!system_) return btdevGattServiceGetCharacteristics(s,out,count,total);
        if (!s || !out || !count || count>100 || !total) return Invalid();
        std::array<BtmGattCharacteristic,100> raw{};u8 n{};
        const auto rc=RawCharacteristics(s->attr.connection_handle,s->attr.handle,raw.data(),raw.size(),&n);
        if (R_FAILED(rc)) return rc;
        if (n>=raw.size() || n>count) return Invalid();
        for (u8 i=0;i<n;++i) {
            out[i]={};out[i].attr.type=1;out[i].attr.uuid=raw[i].uuid;
            out[i].attr.handle=raw[i].handle;out[i].attr.connection_handle=s->attr.connection_handle;
            out[i].instance_id=raw[i].instance_id;out[i].properties=raw[i].properties;
        }
        *total=n;return 0;
    }
    Result Descriptors(BtdevGattCharacteristic *c,BtdevGattDescriptor *out,u8 count,u8 *total) {
        if (!system_) return btdevGattCharacteristicGetDescriptors(c,out,count,total);
        if (!c || !out || !count || count>100 || !total) return Invalid();
        std::array<BtmGattDescriptor,100> raw{};u8 n{};
        const auto rc=RawDescriptors(c->attr.connection_handle,c->attr.handle,raw.data(),raw.size(),&n);
        if (R_FAILED(rc)) return rc;
        if (n>=raw.size() || n>count) return Invalid();
        for (u8 i=0;i<n;++i) {
            out[i]={};out[i].attr.type=2;out[i].attr.uuid=raw[i].uuid;
            out[i].attr.handle=raw[i].handle;out[i].attr.connection_handle=c->attr.connection_handle;
        }
        *total=n;return 0;
    }
};
}
