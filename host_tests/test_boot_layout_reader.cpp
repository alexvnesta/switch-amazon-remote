// SPDX-License-Identifier: GPL-2.0-only
#include "../tools/amazon-remote-module/control/boot_layout_reader.hpp"
#include <cassert>
#include <cstdio>
namespace f=arbootlayout;
struct Fake {
    f::Expected expected{0x160500,0x2f4000,{},{} };f::Modules modules{};
    unsigned call{},fail{},pm_closed{},ldr_closed{},pid_calls{};
    std::uint32_t version=0x160500;std::int32_t count=2;bool changed_pid=false;
    Fake(){expected.main.fill(1);expected.addon.fill(2);modules[0]={0x8000000,0x2f4000,expected.main};modules[1]={0x82f4000,0x3000,expected.addon};}
    std::uint32_t Firmware(){return version;}
    std::uint32_t Step(){return ++call==fail?0x1234:0;}
    std::uint32_t PmOpen(){return Step();}void PmClose(){++pm_closed;}
    std::uint32_t LoaderOpen(){return Step();}void LoaderClose(){++ldr_closed;}
    std::uint32_t Pid(std::uint64_t &out){out=700+(changed_pid?pid_calls:0);++pid_calls;return Step();}
    std::uint32_t ReadModules(std::uint64_t,f::Modules &out,std::int32_t &n){out=modules;n=count;return Step();}
};
int main(){
    Fake ok;auto r=f::Collect(ok,ok.expected,12);assert(f::Valid(r) && r.verdict==f::Verdict::Matched);
    assert(ok.pm_closed==1 && ok.ldr_closed==1 && ok.call==5 && r.addon_delta==0x2f4000);
    for(unsigned fail=1;fail<=5;++fail){Fake b;b.fail=fail;auto x=f::Collect(b,b.expected,1);
        assert(f::Valid(x) && x.verdict==f::Verdict::ReadError && x.result==0x1234);
        assert(x.count==0 && x.main==f::Id{} && x.addon==f::Id{});
        assert((fail!=3 || x.before==0) && x.after==0);
        assert(b.call==fail && b.pm_closed==(fail>1) && b.ldr_closed==(fail>2));}
    Fake firmware;firmware.version=0;auto wrong=f::Collect(firmware,firmware.expected,1);
    assert(f::Valid(wrong) && wrong.verdict==f::Verdict::WrongFirmware && !firmware.call);
    for(int n: {-1,17,100}){Fake b;b.count=n;auto x=f::Collect(b,b.expected,1);
        assert(f::Valid(x) && x.verdict==f::Verdict::ReadError && x.count==0 && x.main==f::Id{} && b.call==4);}
    for(unsigned mutation=0;mutation<12;++mutation){Fake b;
        switch(mutation){
            case 0:b.modules[1].base+=0x9000;break;case 1:b.modules[0].base=0;break;
            case 2:b.modules[0].base+=1;break;case 3:b.modules[0].size+=4096;break;
            case 4:b.modules[1].size+=4096;break;case 5:b.modules[0].id[0]^=1;break;
            case 6:b.modules[1].id[0]^=1;break;case 7:b.changed_pid=true;break;
            case 8:b.count=0;break;case 9:b.count=1;break;case 10:b.count=16;break;
            case 11:b.modules[1].base=1;break;
        }
        auto x=f::Collect(b,b.expected,1);assert(f::Valid(x) && x.verdict==f::Verdict::WrongModules);
    }
    for(std::size_t i=0;i<sizeof r;++i){auto altered=r;reinterpret_cast<std::uint8_t *>(&altered)[i]^=1;assert(!f::Valid(altered));}
    f::Record started{};started.run=1;assert(f::Valid(f::Seal(started)));
    std::puts("PASS boot layout reader: getter failures, gap/build/count/PID refusal, checksums and cleanup");
}
