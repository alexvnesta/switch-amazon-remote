// SPDX-License-Identifier: GPL-2.0-only
#include "../mc_mitm/source/amazon_remote/hci_status_trace.hpp"
#include <cassert>
using namespace amazon_remote::central_bond;
int main(){
    const std::array<std::uint8_t,6> peer{6,5,4,3,2,1};
    std::array<std::uint8_t,1024> wire{};
    const std::uint32_t opcode=0x200d;
    std::memcpy(wire.data(),&Magic,4);std::copy(peer.begin(),peer.end(),wire.begin()+4);
    wire[10]=6;std::memcpy(wire.data()+20,&opcode,4);
    for(unsigned status=0;status<256;++status)for(unsigned own=0;own<4;++own){
        HciTrace out{};out.supported=1;wire[11]=status;wire[16]=own;
        assert(CaptureHciStatus(out,wire,peer,23,1000,2000,true));
        assert(out.count==1 && out.window_nonce==23 && out.entries[0].status==status && out.entries[0].own_address_type==own);
        assert(out.entries[0].boot_ms==2000 && !out.dropped);
    }
    wire[11]=0;wire[16]=0;
    for(std::size_t i=0;i<64;++i){
        if(i==11 || i==16)continue;
        auto altered=wire;altered[i]^=0x80;HciTrace out{};out.supported=1;
        assert(!CaptureHciStatus(out,altered,peer,23,1000,2000,true) && !out.count);
    }
    for(std::size_t size=0;size<64;++size){HciTrace out{};out.supported=1;
        assert(!CaptureHciStatus(out,std::span(wire.data(),size),peer,23,1000,2000,true));}
    HciTrace out{};assert(!CaptureHciStatus(out,wire,peer,23,1000,2000,true));out.supported=1;
    assert(!CaptureHciStatus(out,wire,peer,23,1000,2000,false));
    assert(!CaptureHciStatus(out,wire,peer,0,1000,2000,true));
    assert(!CaptureHciStatus(out,wire,peer,23,0,2000,true));
    assert(!CaptureHciStatus(out,wire,peer,23,1000,999,true));
    auto bad=wire;bad[16]=4;assert(!CaptureHciStatus(out,bad,peer,23,1000,2000,true));
    for(unsigned i=0;i<4;++i)assert(CaptureHciStatus(out,wire,peer,23,1000,2000+i,true));
    const auto preserved=out;
    assert(!CaptureHciStatus(out,wire,peer,24,1000,2004,true));
    assert(!CaptureHciStatus(out,wire,peer,23,1001,2004,true));
    assert(!CaptureHciStatus(out,wire,peer,23,1000,2002,true));
    assert(CaptureHciStatus(out,wire,peer,23,1000,2004,true) && out.count==4 && out.dropped==1);
    assert(!std::memcmp(out.entries,preserved.entries,sizeof out.entries));
    out.dropped=0xffffffff;assert(CaptureHciStatus(out,wire,peer,23,1000,2004,true) && out.dropped==0xffffffff);
}
