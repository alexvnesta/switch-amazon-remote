// SPDX-License-Identifier: GPL-2.0-only
#include "filter_sdk/stratosphere.hpp"
#include "../mc_mitm/source/amazon_remote/lease_scan_trace_runtime.hpp"
#include "../mc_mitm/source/amazon_remote/capture_lease.hpp"
#include "../mc_mitm/source/amazon_remote/manufacturer_filter_correction.hpp"
#include "../mc_mitm/source/amazon_remote/intent_trace.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
namespace runtime=ams::mc::lease_scan_trace;
using namespace amazon_remote;
constexpr char Mode[]="sdmc:/config/amazon-remote/scan-diagnostic-mode.txt";
constexpr char Output[]="sdmc:/config/amazon-remote/scan-diagnostic-v2.txt";
CaptureLeaseWire lease{CaptureLeaseMagic,1,1,0,300000,360000};
bool getter_success=true;
namespace ams::mc::amazon_observer {
bool ReadCaptureStatus(void *out,std::size_t n){assert(n==sizeof lease);if(!getter_success)return false;std::memcpy(out,&lease,n);return true;}
}
void Request(unsigned cmd=55,std::uint64_t program=BtmProgram,std::uint64_t process=119){
    const auto original=OriginalAmazonGeneralFilter();const auto before=original;
    runtime::ObserveRequest(cmd,program,process,(cmd==57 || cmd==58)?original.data():nullptr,
        (cmd==57 || cmd==58)?original.size():0);
    assert(original==before);
}
void Event(unsigned type=7){
    std::array<std::uint8_t,1024> data{}; data[4]=2;data[7]=0xa7;data[8]=0xff;
    const auto before=data;runtime::ObserveManaged(type,data.data(),data.size());assert(data==before);
}
void End(){lease.state=3;lease.deadline_boot_ms=0;runtime::Poll();}
std::string Text(){const auto &v=fake_filter::files.at(Output);return {v.begin(),v.end()};}
int main(int argc,char **argv){
    assert(argc==2);const unsigned scenario=std::strtoul(argv[1],nullptr,10);
    fake_filter::now=300000;
    if(scenario!=0){const std::string mode=scenario==1?"lease-scan-v2":"lease-scan-v2\n";fake_filter::files[Mode]={mode.begin(),mode.end()};}
    runtime::Initialize();runtime::Arm(300000);
    if(scenario<2){Request();Event();End();assert(!fake_filter::creates);}
    else if(scenario==2){
        Request(57);Event();runtime::Poll();assert(!fake_filter::creates);
        Request(61);Event(6);Request(55);runtime::Poll();
        fake_filter::now=345000;lease.now_boot_ms=345000;Request(58);Event();Request(56);End();
        const auto text=Text();assert(text.find("command=57")!=std::string::npos && text.find("command=58")!=std::string::npos);
        assert(text.find("event=7")!=std::string::npos && text.find("sdk_filter_action=2801795074")!=std::string::npos);
        assert(text.find("event=6")!=std::string::npos && text.find("raw_prefix=00000000020000")!=std::string::npos);
        assert(text.find("begin_boot_ms=300000")!=std::string::npos && text.find("intent_drops=0")!=std::string::npos);
        const auto c=fake_filter::creates,w=fake_filter::writes;runtime::Poll();assert(c==1&&w==1&&fake_filter::creates==c&&fake_filter::writes==w);
        runtime::Arm(345000);Request();runtime::Poll();assert(Text()==text);
    }else if(scenario==3){
        const auto good=lease;
        for(unsigned i=0;i<12;++i){
            lease=good;getter_success=true;
            switch(i){case 0:lease.magic^=1;break;case 1:lease.version=2;break;case 2:lease.reserved=1;break;
            case 3:lease.state=0;break;case 4:lease.state=2;break;case 5:lease.state=3;break;
            case 6:lease.now_boot_ms=0;break;case 7:lease.now_boot_ms=300001;break;
            case 8:lease.now_boot_ms=296999;break;case 9:lease.deadline_boot_ms=300000;break;
            case 10:lease.deadline_boot_ms=360001;break;case 11:getter_success=false;break;}
            Request();Event();
        }
        lease=good;getter_success=true;Request(55,0);Request(55,BtmProgram,0);Request(65);Event(4);
        End();assert(Text().find("intent_queued=0")!=std::string::npos && Text().find("header_queued=0")!=std::string::npos);
    }else if(scenario==4){Request();Event();runtime::Suspend();End();assert(!fake_filter::creates);}
    else if(scenario==5){
        Request();Event();fake_filter::now=360000;lease.now_boot_ms=360000;lease.state=2;
        Request(56);Event();runtime::Poll();assert(!fake_filter::creates);End();
        assert(Text().find("intent_queued=1")!=std::string::npos && Text().find("header_queued=1")!=std::string::npos);
    }else if(scenario==6 || scenario==7){
        Request();Event();fake_filter::fail_create=scenario==6;fake_filter::fail_write=scenario==7;
        End();const auto c=fake_filter::creates,w=fake_filter::writes;runtime::Poll();assert(c==1&&fake_filter::creates==c&&fake_filter::writes==w);
    }else if(scenario==8){fake_filter::files[Output]={1,2,3};Request();End();assert((fake_filter::files.at(Output)==std::vector<std::uint8_t>{1,2,3}));}
    else if(scenario==9){
        for(unsigned i=0;i<100;++i){Request();Event();}End();
        assert(Text().find("intent_drops=84")!=std::string::npos && Text().find("header_drops=68")!=std::string::npos);
    }else if(scenario==10){
        for(unsigned i=0;i<400;++i){Request(57);Event();runtime::Poll();}
        End();assert(Text().size()<=32768 && Text().find("text_drops=0")==std::string::npos && Text().find("end_boot_ms=")!=std::string::npos);
    }else if(scenario==11){
        // No Poll during the short active window: queues still saved at End.
        Request(57);Request(58);Event();Event(6);End();
        assert(Text().find("command=58")!=std::string::npos && Text().find("header_queued=2")!=std::string::npos);
    }else assert(false);
    std::cout<<"PASS lease trace runtime scenario"<<scenario<<"; passive fake SDK only, no device discovery/cleanup proof.\n";
}
