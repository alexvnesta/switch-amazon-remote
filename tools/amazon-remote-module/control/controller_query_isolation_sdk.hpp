// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>
#include "controller_query_isolation.hpp"
#include "../io.hpp"
namespace arqueryq2 {
class Backend {
public:
    explicit Backend(Service &service):service_(service){}
    std::uint64_t Now(){return armTicksToNs(armGetSystemTick())/1000000;}
    bool ReadIsolation(Intent &intent,armodule::StartupRecord &boot) {
        static_assert(DisabledResult==MAKERESULT(Module_Libnx,LibnxError_BadInput));
        struct stat st{};
        if(hosversionGet()!=MAKEHOSVERSION(22,5,0) ||
           ::stat(armodule::EnablePath,&st)==0 || errno!=ENOENT)return false;
        char paused[sizeof(ExpectedEnable)-1]{};
        return armodule::ReadExact(PausePath,paused) &&
            !std::memcmp(paused,ExpectedEnable,sizeof paused) &&
            armodule::ReadExact(IntentPath,intent) && armodule::ReadExact(armodule::StartupPath,boot);
    }
    ::Result ProcessId(std::uint64_t &pid) {
        pid=0;auto rc=pmdmntInitialize();if(rc)return rc;
        u64 native{};rc=pmdmntGetProcessId(&native,armodule::ModuleProgramId);pmdmntExit();
        pid=native;return rc;
    }
    ::Result ReadLease(amazon_remote::CaptureDiagnosticsWire &out) {
        out={};return serviceDispatch(&service_,65006,
            .buffer_attrs={SfBufferAttr_HipcPointer|SfBufferAttr_Out},.buffers={{&out,sizeof out}});
    }
    ::Result EndLease(){return serviceDispatch(&service_,65003);}
private:
    Service &service_;
};
}
