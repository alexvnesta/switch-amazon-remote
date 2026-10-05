// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>
#include "audit.hpp"
#include <cstring>
namespace araudit {
struct Backend {
    Result OpenBle(){return btmuInitialize();}
    Result Paired(Parameter p,std::array<Address,10> &out,std::uint32_t &count) {
        BtdrvBleAdvertisePacketParameter parameter{};parameter.company_id=p.company;
        std::memcpy(parameter.pattern_data,p.pattern.data(),p.pattern.size());
        std::array<BtdrvAddress,10> addresses{};u8 total{};
        const auto rc=btmuBleGetPairedDevices(parameter,addresses.data(),addresses.size(),&total);
        if(rc)return rc;
        count=total;
        if(total<=addresses.size())for(unsigned i=0;i<total;++i)std::memcpy(out[i].data(),addresses[i].address,out[i].size());
        return rc;
    }
    void CloseBle(){btmuExit();}
    Result OpenHid(){return btmInitialize();}
    Result Hid(std::array<Address,10> &out,std::uint32_t &count) {
        // DeviceCondition contains no link keys. Do NOT substitute DeviceInfo.
        std::array<BtmDeviceCondition,10> devices{};s32 total{};
        const auto rc=btmGetDeviceCondition(BtmProfile_Hid,devices.data(),devices.size(),&total);
        if(rc)return rc;
        count=static_cast<std::uint32_t>(total);
        if(total<0 || static_cast<unsigned>(total)>devices.size())return 0; // collector marks invalid bounds
        for(int i=0;i<total;++i) {
            if(devices[i].profile!=BtmProfile_Hid)return InvalidSnapshot;
            std::memcpy(out[i].data(),devices[i].address.address,out[i].size());
        }
        return 0;
    }
    void CloseHid(){btmExit();}
    Result OpenSystem(){return btmsysInitialize();}
    Result PairingStarted(bool &out){return btmsysIsGamepadPairingStarted(&out);}
    void CloseSystem(){btmsysExit();}
};
}
