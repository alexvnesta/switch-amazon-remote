// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../tools/amazon-remote-pairing-audit/native_postcheck.hpp"
struct PostFixture {
    arnativebond::OwnerResult owner{};arnativebond::Record intent{};armodule::Status status{};
    PostFixture(){
        auto &t=owner.trial;t.state=unsigned(arnativebond::State::AcceptedOutcomeUnknown);
        t.instance=42;t.begin_ms=900;t.end_ms=902;t.handle=4;
        t.open_result=t.readiness_result=t.getter_result=t.request_result=0;t.enabled=t.request_issued=1;
        owner.sequence=4;owner.count=2;
        for(unsigned i=0;i<2;++i){auto &c=owner.checks[i];c.call=i+1;c.stage=unsigned(arnativebond::OwnerStage::Passed);
            c.result=c.user_result=c.system_result=c.invalid=0;c.instance=42;c.begin_ms=900+i;c.end_ms=901+i;
            c.count=c.matches=1;c.handle=4;}
        intent=t;intent.state=unsigned(arnativebond::State::IntentSaved);intent.request_issued=0;
        intent.request_result=arnativebond::Unknown;intent.end_ms=900;
        status.state=unsigned(armodule::State::Stopped);status.instance=42;status.now_ms=1000;status.last_sequence=5;
        Seal();
    }
    void Seal(){
        owner.trial.checksum=arnativebond::Checksum(owner.trial);intent.checksum=arnativebond::Checksum(intent);
        for(auto &c:owner.checks)c.checksum=arnativebond::OwnerChecksum(c);
        owner.checksum=arnativebond::OwnerResultChecksum(owner);
        status.reserved=armodule::Version==3?armodule::StatusChecksum(status):0;
    }
};
