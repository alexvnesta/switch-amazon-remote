// SPDX-License-Identifier: GPL-2.0-only
#include "../control/driver_identity.hpp"
#include <cassert>
#include <cstdio>
struct Fake {
    unsigned opens{}, closes{}, queries{}, loader_opens{}, loader_closes{}, module_calls{};
    std::uint32_t firmware=ardriverid::Firmware, pm_error{}, before_error{}, loader_error{}, module_error{}, after_error{};
    std::uint64_t pid=44, after_pid=44, now=1000;
    int count=1;
    ardriverid::Inventory ids{};
    Fake() { ids[0]=ardriverid::Expected; }
    auto FirmwareVersion() { return firmware; }
    auto Now() { return now++; }
    auto PmOpen() { ++opens; return pm_error; }
    void PmClose() { ++closes; }
    auto ProcessId(std::uint64_t program,std::uint64_t &out) {
        assert(program==ardriverid::ProgramId);
        const auto first=queries++==0; out=first?pid:after_pid;
        return first?before_error:after_error;
    }
    auto LoaderOpen() { ++loader_opens; return loader_error; }
    void LoaderClose() { ++loader_closes; }
    auto Modules(std::uint64_t selected,ardriverid::Inventory &out,std::int32_t &n) {
        assert(selected==pid); ++module_calls; out=ids; n=count; return module_error;
    }
};
int main() {
    using namespace ardriverid;
    Fake good; auto r=Collect(good);
    assert(Valid(r) && r.verdict==unsigned(Verdict::KnownBuildMetadata) && r.matches==1);
    assert(good.opens==1 && good.closes==1 && good.queries==2 && good.loader_opens==1 && good.loader_closes==1 && good.module_calls==1);
    for (unsigned stage=0;stage<5;++stage) {
        Fake b;
        switch(stage) { case 0:b.pm_error=9;break;case 1:b.before_error=9;break;
            case 2:b.loader_error=9;break;case 3:b.module_error=9;break;case 4:b.after_error=9;break; }
        auto v=Collect(b); assert(Valid(v) && v.verdict==unsigned(Verdict::QueryFailed));
        assert(b.closes==(stage!=0) && b.loader_closes==(stage>=3));
    }
    Fake unsupported; unsupported.firmware=0; r=Collect(unsupported);
    assert(Valid(r) && r.verdict==unsigned(Verdict::UnsupportedFirmware) && !unsupported.opens);
    Fake missing; missing.pid=0; r=Collect(missing);
    assert(Valid(r) && r.verdict==unsigned(Verdict::QueryFailed) && !missing.loader_opens);
    Fake changed; changed.after_pid=45; r=Collect(changed);
    assert(Valid(r) && r.verdict==unsigned(Verdict::ProcessChanged));
    for (int count:{-1,0,16,17,2147483647}) {
        Fake b;b.count=count;r=Collect(b);
        assert(Valid(r) && r.verdict==unsigned(count==0?Verdict::NoModules:Verdict::InvalidInventory));
    }
    Fake empty; empty.ids={}; r=Collect(empty);
    assert(Valid(r) && r.verdict==unsigned(Verdict::InvalidInventory));
    Fake duplicate; duplicate.count=2; duplicate.ids[1]=Expected;r=Collect(duplicate);
    assert(Valid(r) && r.verdict==unsigned(Verdict::InvalidInventory));
    Fake other; other.ids[0][0]^=1;r=Collect(other);
    assert(Valid(r) && r.verdict==unsigned(Verdict::OtherBuildMetadata));
    Fake second; second.count=2; second.ids[1]=Expected; second.ids[0][0]^=1;r=Collect(second);
    assert(Valid(r) && r.verdict==unsigned(Verdict::KnownBuildMetadata));
    for (unsigned i=0;i<sizeof r;++i) {
        auto corrupt=r;reinterpret_cast<unsigned char*>(&corrupt)[i]^=1;assert(!Valid(corrupt));
    }
    auto bad=r;bad.program_id^=1;bad.checksum=Checksum(bad);assert(!Valid(bad));
    bad=r;bad.end_ms=bad.begin_ms-1;bad.checksum=Checksum(bad);assert(!Valid(bad));
    bad=r;bad.end_ms+=10001;bad.checksum=Checksum(bad);assert(!Valid(bad));
    std::puts("PASS I1: fixed Bluetooth PID lookup; loader metadata only; stable PID check; errors, missing/truncated/duplicate IDs and all600-byte corruption; service cleanup. Not live-code or bond proof.");
}
