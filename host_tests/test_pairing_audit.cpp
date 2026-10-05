// SPDX-License-Identifier: GPL-2.0-only
#include "../tools/amazon-remote-pairing-audit/backend.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
namespace {
const araudit::Address target{0x02,0xab,0xcd,0x12,0x34,0x56};
unsigned variant;std::vector<unsigned> calls;
void SetAddress(BtdrvAddress &out){std::memcpy(out.address,target.data(),target.size());}
}
Result btmuInitialize(){calls.push_back(1);return variant==1?51:0;}
void btmuExit(){calls.push_back(3);}
Result btmuBleGetPairedDevices(BtdrvBleAdvertisePacketParameter p,BtdrvAddress *a,u8 size,u8 *count){
    calls.push_back(2);assert(p.company_id==0x171 && size==10);
    const u8 pattern[]={4,0x27,2,0x7d,0,0};assert(!std::memcmp(p.pattern_data,pattern,sizeof pattern));
    SetAddress(a[0]);*count=variant==3?11:variant==4?2:variant==8?0:variant==9?10:1;
    if(variant==4)SetAddress(a[1]);
    return variant==2?52:0;
}
Result btmInitialize(){calls.push_back(4);return variant==5?53:0;}
void btmExit(){calls.push_back(6);}
Result btmGetDeviceCondition(BtmProfile profile,BtmDeviceCondition *d,std::size_t size,s32 *count){
    calls.push_back(5);assert(profile==BtmProfile_Hid && size==10);
    *count=variant==7?-1:variant==10?11:variant==11?2:variant==8?0:variant==9?10:1;
    for(unsigned i=0;i<size;++i)d[i].profile=BtmProfile_Hid;
    SetAddress(d[0].address);if(variant==11)SetAddress(d[1].address);
    if(variant==12)d[0].profile=2;
    return variant==6?54:0;
}
Result btmsysInitialize(){calls.push_back(7);return variant==13?55:0;}
void btmsysExit(){calls.push_back(9);}
Result btmsysIsGamepadPairingStarted(bool *started){calls.push_back(8);*started=variant!=15;return variant==14?56:0;}
int main(){
    for(variant=0;variant<16;++variant){
        calls.clear();araudit::Backend backend;const auto s=araudit::Collect(backend,target,1234);
        assert(s.magic==0x41525032 && s.version==2 && s.boot_ms==1234 && !s.reserved && !s.reserved0 && s.checksum==araudit::Checksum(s));
        std::vector<unsigned> expected=variant==1?std::vector<unsigned>{1}:std::vector<unsigned>{1,2,3};
        for(auto call:variant==5?std::vector<unsigned>{4}:std::vector<unsigned>{4,5,6})expected.push_back(call);
        for(auto call:variant==13?std::vector<unsigned>{7}:std::vector<unsigned>{7,8,9})expected.push_back(call);
        assert(calls==expected); // Exact actual-SDK adapter call footprint and cleanup.
        if(variant==1)assert(s.ble_open==51 && s.ble.result==araudit::NotAttempted);
        else if(variant==2)assert(s.ble.result==52 && !s.ble.count && !s.ble.matches);
        else if(variant==3 || variant==4)assert(s.ble.result==araudit::InvalidSnapshot && s.ble.invalid);
        else assert(!s.ble.result && s.ble.matches==(variant!=8));
        if(variant==5)assert(s.btm_open==53 && s.hid.result==araudit::NotAttempted);
        else if(variant==6)assert(s.hid.result==54 && !s.hid.count && !s.hid.matches);
        else if(variant==7 || variant==10 || variant==11)assert(s.hid.result==araudit::InvalidSnapshot && s.hid.invalid);
        else if(variant==12)assert(s.hid.result==araudit::InvalidSnapshot && s.hid.invalid && !s.hid.matches);
        else assert(!s.hid.result && s.hid.matches==(variant!=8));
        if(variant==13)assert(s.system_open==55 && s.pairing_query==araudit::NotAttempted && !s.pairing_started);
        else if(variant==14)assert(s.pairing_query==56 && !s.pairing_started);
        else assert(!s.pairing_query && s.pairing_started==(variant!=15));
    }
    std::puts("PASS actual getter-only pairing audit backend: exact AD-derived parameter, independent client failures/cleanup, empty/full/overflow/duplicate/foreign-profile lists and native pairing flag. MODEL ONLY.");
}
