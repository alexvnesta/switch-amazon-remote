// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>
#include <cstring>
namespace amazon_remote {
// Public libnx btdrv.c commands90/91/93/94/95 (HOS5.1+). The normal
// btdrvInitialize() sends InitializeBluetoothDriver; this adapter deliberately
// opens ONLY its own service session. No global event reads or initialization.
class NativeGattRequests {
    static_assert(sizeof(BtdrvGattId)==0x18 && offsetof(BtdrvGattId,uuid)==4);
    Service service_{};
    static Result Invalid() { return MAKERESULT(Module_Libnx,LibnxError_BadInput); }
    static bool Uuid(const BtdrvGattId *id,u16 value) {
        return id && id->uuid.size==2 && id->uuid.uuid[0]==(value&255) && id->uuid.uuid[1]==(value>>8);
    }
    bool Input(u32 connection,bool primary,const BtdrvGattId *s,const BtdrvGattId *c) {
        return serviceIsActive(&service_) && connection && primary && Uuid(s,0x1812) && Uuid(c,0x2a4d);
    }
public:
    Result Open() {
        if (hosversionBefore(5,1,0)) return MAKERESULT(Module_Libnx,LibnxError_IncompatSysVer);
        return smGetService(&service_,"btdrv");
    }
    void Close() { serviceClose(&service_); }
    Result ReadCharacteristic(u32 connection,bool primary,const BtdrvGattId *s,const BtdrvGattId *c,u8 auth) {
        if (!serviceIsActive(&service_) || !connection || !primary || auth>1 ||
            !((Uuid(s,0x180a)&&Uuid(c,0x2a50)&&auth==0) || (Uuid(s,0x1812)&&Uuid(c,0x2a4b)))) return Invalid();
        const struct { u8 primary,auth,pad[2];u32 connection;BtdrvGattId s,c; } in{u8(primary),auth,{0},connection,*s,*c};
        static_assert(sizeof in==0x38);
        return serviceDispatchIn(&service_,90,in);
    }
    Result ReadDescriptor(u32 connection,bool primary,const BtdrvGattId *s,const BtdrvGattId *c,const BtdrvGattId *d,u8 auth) {
        if (!Input(connection,primary,s,c) || !Uuid(d,0x2908) || auth>1) return Invalid();
        const struct { u8 primary,auth,pad[2];u32 connection;BtdrvGattId s,c,d; } in{u8(primary),auth,{0},connection,*s,*c,*d};
        static_assert(sizeof in==0x50);
        return serviceDispatchIn(&service_,91,in);
    }
    Result Notify(u32 connection,bool primary,const BtdrvGattId *s,const BtdrvGattId *c,bool enable) {
        if (!Input(connection,primary,s,c)) return Invalid();
        const struct { u8 primary,pad[3];u32 connection;BtdrvGattId s,c; } in{u8(primary),{0},connection,*s,*c};
        static_assert(sizeof in==0x38);
        return serviceDispatchIn(&service_,enable?94:95,in);
    }
    Result WriteCcc(u32 connection,bool primary,const BtdrvGattId *s,const BtdrvGattId *c,const BtdrvGattId *d,const void *data,std::size_t size,u8 auth) {
        constexpr u8 enable[]{1,0};
        if (!Input(connection,primary,s,c) || !Uuid(d,0x2902) || auth>1 || size!=sizeof enable || !data || std::memcmp(data,enable,size)) return Invalid();
        const struct { u8 primary,auth,pad[2];u32 connection;BtdrvGattId s,c,d; } in{u8(primary),auth,{0},connection,*s,*c,*d};
        static_assert(sizeof in==0x50);
        return serviceDispatchIn(&service_,93,in,
            .buffer_attrs={SfBufferAttr_HipcPointer|SfBufferAttr_In},.buffers={{data,size}});
    }
};
}
