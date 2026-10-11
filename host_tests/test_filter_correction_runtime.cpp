// SPDX-License-Identifier: GPL-2.0-only
#include "filter_sdk/stratosphere.hpp"
#include "../mc_mitm/source/amazon_remote/filter_correction_runtime.hpp"
#include "../mc_mitm/source/amazon_remote/manufacturer_filter_correction.hpp"
#include "../mc_mitm/source/amazon_remote/capture_lease.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
namespace runtime=ams::mc::amazon_filter_correction;
using namespace amazon_remote;
CaptureLeaseWire lease{CaptureLeaseMagic,1,1,0,100000,160000};
bool getter_success=true;
namespace ams::mc::amazon_observer {
bool ReadCaptureStatus(void *out,std::size_t size){assert(size==sizeof lease);if(!getter_success)return false;std::memcpy(out,&lease,size);return true;}
}
constexpr char Mode[]="sdmc:/config/amazon-remote/filter-correction-mode.txt";
constexpr char Output[]="sdmc:/config/amazon-remote/filter-correction-result.bin";
unsigned forwards{};std::uint32_t forward_result{};
bool poll_during_forward{};
ManufacturerFilterWire forwarded{};
std::uint32_t Forward(void *context,std::uint32_t cmd,const void *p,std::size_t n){
    assert(context==&forwards && (cmd==57 || cmd==58) && n==62);
    ++forwards;std::memcpy(forwarded.data(),p,n);
    if(poll_during_forward){lease.state=3;lease.deadline_boot_ms=0;runtime::Poll();assert(!fake_filter::creates);}
    return forward_result;
}
bool Call(unsigned cmd,std::uint64_t program=FilterBtmProgram,std::uint64_t pid=42,
          const ManufacturerFilterWire *wire=nullptr){
    const auto original=OriginalAmazonGeneralFilter();if(!wire)wire=&original;
    const auto before=*wire;
    std::uint32_t result=0x55;
    const auto handled=runtime::Correct(cmd,program,pid,wire->data(),wire->size(),&forwards,Forward,result);
    if(handled)assert(result==forward_result);else assert(result==0x55);
    assert(*wire==before);return handled;
}
void End(){lease.state=3;lease.deadline_boot_ms=0;runtime::Poll();}
std::uint32_t Word(const std::vector<std::uint8_t> &v,unsigned offset){std::uint32_t x{};assert(offset+4<=v.size());std::memcpy(&x,v.data()+offset,4);return x;}
std::uint32_t Fnv(const std::uint8_t *p,std::size_t size){std::uint32_t h=2166136261u;for(std::size_t i=0;i<size;++i)h=(h^p[i])*16777619u;return h;}
int main(int argc,char **argv){
    assert(argc==2);const unsigned scenario=std::strtoul(argv[1],nullptr,10);
    if(scenario!=0){const std::string mode=scenario==1?"amazon-general-v1":"amazon-general-v1\n";fake_filter::files[Mode]={mode.begin(),mode.end()};}
    runtime::Initialize();
    if(scenario<2){assert(!Call(57));assert(!forwards);End();assert(!fake_filter::creates);}
    else if(scenario==2){
        const auto good=lease;
        for(unsigned i=0;i<12;++i){
            lease=good;getter_success=true;
            switch(i){case 0:lease.magic^=1;break;case 1:lease.version=2;break;case 2:lease.reserved=1;break;
            case 3:lease.state=0;break;case 4:lease.state=2;break;case 5:lease.state=3;break;
            case 6:lease.now_boot_ms=0;break;case 7:lease.now_boot_ms=100001;break;
            case 8:lease.now_boot_ms=96999;break;case 9:lease.deadline_boot_ms=100000;break;
            case 10:lease.deadline_boot_ms=160001;break;case 11:getter_success=false;break;}
            assert(!Call(57) && !forwards);
        }
        lease=good;getter_success=true;
        assert(!Call(57,0));assert(!Call(57,FilterBtmProgram,0));assert(!Call(59));
        auto other=OriginalAmazonGeneralFilter();other[0]=1;assert(!Call(57,FilterBtmProgram,42,&other));
        assert(Call(57) && forwards==1 && forwarded==CorrectedAmazonGeneralFilter());
        assert(!Call(57) && forwards==1);
        lease.state=2;fake_filter::now=160000;runtime::Poll();assert(!fake_filter::creates);
        assert(!Call(58,FilterBtmProgram,43));assert(Call(58) && forwards==2);
        End();const auto &v=fake_filter::files.at(Output);
        assert(v.size()==72 && Word(v,0)==0x41465231 && Word(v,8)==4 && !Word(v,40) && !Word(v,44));
        assert(Word(v,68)==Fnv(v.data(),68));runtime::Poll();assert(fake_filter::creates==1 && fake_filter::writes==1);
    }else if(scenario==3){runtime::Suspend();assert(!Call(57));End();assert(!fake_filter::creates && !forwards);}
    else if(scenario==4){assert(Call(57));runtime::Suspend();assert(Call(58));End();assert(forwards==2 && !fake_filter::creates);}
    else if(scenario==5){forward_result=0x1759;assert(Call(57));assert(!Call(57));assert(!Call(58));End();assert(Word(fake_filter::files.at(Output),8)==5 && Word(fake_filter::files.at(Output),40)==0x1759);}
    else if(scenario==6){assert(Call(57));forward_result=0x1759;assert(Call(58));End();assert(Word(fake_filter::files.at(Output),8)==6 && Word(fake_filter::files.at(Output),44)==0x1759);}
    else if(scenario==7 || scenario==8){
        assert(Call(57));assert(Call(58));fake_filter::fail_create=scenario==7;fake_filter::fail_write=scenario==8;End();
        const unsigned creates=fake_filter::creates,writes=fake_filter::writes;runtime::Poll();assert(creates==1 && fake_filter::creates==creates && fake_filter::writes==writes);
    }else if(scenario==9){fake_filter::files[Output]={1,2,3};End();assert((fake_filter::files.at(Output)==std::vector<std::uint8_t>{1,2,3}) && fake_filter::creates==1 && !fake_filter::writes);}
    else if(scenario==10){poll_during_forward=true;assert(Call(57));assert(Call(58));End();assert(Word(fake_filter::files.at(Output),8)==4 && fake_filter::creates==1);}
    else assert(false);
    std::cout<<"PASS actual correction runtime scenario"<<scenario<<"; fake forwarding/storage/clock only; no console proof.\n";
}
