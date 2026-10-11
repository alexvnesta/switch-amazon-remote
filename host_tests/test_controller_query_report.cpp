// SPDX-License-Identifier: GPL-2.0-only
#define ARMODULE_NAV15
#define ARMODULE_NAV17
#define ARMODULE_RECONNECT_CHECK
#define ARMODULE_NATIVE_HCI_C2
#define ARMODULE_AUTOMATIC_A1
#include "../tools/amazon-remote-module/control/controller_query_report.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main() {
    armodule::StartupRecord boot{}; boot.stage=13; boot.instance=123; boot.now_ms=100;
    boot.checksum=armodule::StartupChecksum(boot);
    armodule::Status s{}; s.instance=123; s.now_ms=1000; s.state=4; s.reserved=armodule::StatusChecksum(s);
    assert(arquery::SafeStopped(s,boot,1001));
    assert(!arquery::SafeStopped(s,boot,999) && !arquery::SafeStopped(s,boot,3001));
    for(unsigned state=0;state<=7;++state) {
        auto other=s; other.state=state; other.reserved=armodule::StatusChecksum(other);
        assert(arquery::SafeStopped(other,boot,1001)==(state==4 || state==7));
    }
    for(auto member:{&armodule::Status::result,&armodule::Status::pad_cleanup_result,
                     &armodule::Status::link_ready,&armodule::Status::buttons,&armodule::Status::output_attached}) {
        auto other=s; other.*member=1; other.reserved=armodule::StatusChecksum(other);
        assert(!arquery::SafeStopped(other,boot,1001));
    }
    auto other=s; ++other.instance; other.reserved=armodule::StatusChecksum(other);
    assert(!arquery::SafeStopped(other,boot,1001));
    arquery::Record r{}; r.started_ms=1000; r.finished_ms=1001;
    r.result=arquery::Result::PreconditionFailed; r.checksum=arquery::Checksum(r); assert(arquery::Valid(r));
    for(unsigned i=0;i<sizeof(r);++i) {
        auto broken=r; reinterpret_cast<unsigned char *>(&broken)[i]^=1; assert(!arquery::Valid(broken));
    }
    arquery::cq::Query q; assert(q.Begin(5,1000)); assert(q.Dispatch(1001)); q.Submission(9,1002);
    r.wire=q.Read(1003); r.result=arquery::Result::Completed; r.nonce=5; r.finished_ms=1003;
    r.checksum=arquery::Checksum(r); assert(arquery::Valid(r));
    assert(!arquery::FreshWire(r.wire,6,1000,1003));
    assert(!arquery::FreshWire(r.wire,5,1001,1003));
    assert(!arquery::FreshWire(r.wire,5,1000,999));
    r.wire.finished_ms=1004; r.checksum=arquery::Checksum(r); assert(!arquery::Valid(r));
    std::puts("PASS Q1 report: stopped/current boot gate, wire freshness, all-byte corruption and report binding");
}
