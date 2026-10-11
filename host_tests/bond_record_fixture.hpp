// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../mc_mitm/source/amazon_remote/bond_record.hpp"
#include "bond_host_sha256.hpp"
#include <cassert>
namespace bond_fixture {
namespace rec=amazon_remote::bond::record;
namespace legacy=amazon_remote::bond::legacy;
inline rec::Binding Binding() {
    rec::Binding b{};
    b.controller_context.fill(0x42); // Synthetic, not a native root digest.
    b.local={1,2,3,4,5,6}; b.peer={6,5,4,3,2,1}; b.profile=1;
    return b;
}
struct Keys {
    std::array<legacy::Material,5> material;
    rec::Materials pointers{};
    explicit Keys(const rec::Binding &b,std::uint32_t mask=rec::KnownMask,
                  std::uint8_t bytes=16,bool mitm=false) {
        for(std::size_t i=0;i<rec::Kinds.size();++i) if(mask&rec::Kinds[i]) {
            std::array<std::uint8_t,36> event{};
            std::copy(b.peer.begin(),b.peer.end(),event.begin()); event[6]=rec::Kinds[i];
            for(std::size_t j=8;j<event.size();++j) event[j]=std::uint8_t(j+i);
            const std::uint8_t sec=mitm ? 4 : 1;
            switch(rec::Kinds[i]) {
                case 1: event[8+26]=sec; event[8+27]=bytes; break;
                case 2: event[8+16]=b.peer_type;
                    std::copy(b.peer.begin(),b.peer.end(),event.begin()+8+17); break;
                case 4: event[8+20]=sec; break;
                case 8: event[8+2]=bytes; event[8+3]=sec; break;
                // Synthetic local DIV equals the LENC fixture's two bytes.
                case 32: event[8+4]=11; event[8+5]=12; event[8+6]=sec; break;
            }
            assert(legacy::Decode(legacy::Layout::AuditedC91Legacy,b.peer,event,material[i]));
            pointers[i]=&material[i]; legacy::Erase(event);
        }
    }
};
}
