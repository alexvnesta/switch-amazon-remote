// SPDX-License-Identifier: GPL-2.0-only
#include "postcheck_fixture.hpp"
#include <cassert>
#include <cstdio>
int main(){
    using arpostcheck::Guard;
    PostFixture good;assert(arpostcheck::Check(good.owner,good.intent,good.status,1000)==Guard::Passed);
    unsigned cases=0;
    const auto reject=[&](auto change,Guard expected){PostFixture f;change(f);f.Seal();
        assert(arpostcheck::Check(f.owner,f.intent,f.status,1000)==expected);++cases;};
    reject([](auto &f){f.owner.trial.state=4;f.owner.trial.request_result=7;},Guard::Owner);
    reject([](auto &f){f.owner.count=1;},Guard::Owner);
    reject([](auto &f){f.owner.checks[1].instance=43;},Guard::Owner);
    reject([](auto &f){f.owner.checks[1].handle=5;},Guard::Owner);
    reject([](auto &f){f.owner.checks[0].user_result=7;},Guard::Owner);
    reject([](auto &f){f.owner.checks[0].system_result=7;},Guard::Owner);
    reject([](auto &f){f.owner.checks[0].matches=0;},Guard::Owner);
    reject([](auto &f){f.owner.checks[0].count=16;},Guard::Owner);
    reject([](auto &f){f.owner.checks[0].invalid=1;},Guard::Owner);
    reject([](auto &f){f.owner.checks[0].begin_ms=899;},Guard::Owner);
    reject([](auto &f){f.owner.checks[1].end_ms=903;},Guard::Owner);
    reject([](auto &f){f.intent.state=1;},Guard::Intent);
    reject([](auto &f){f.intent.instance=43;},Guard::Intent);
    reject([](auto &f){f.intent.handle=5;},Guard::Intent);
    reject([](auto &f){f.intent.begin_ms=899;},Guard::Intent);
    reject([](auto &f){f.intent.end_ms=903;},Guard::Intent);
    reject([](auto &f){f.intent.enabled=0;},Guard::Intent);
    reject([](auto &f){f.intent.readiness_result=7;},Guard::Intent);
    reject([](auto &f){f.status.instance=43;},Guard::Status);
    reject([](auto &f){f.status.state=unsigned(armodule::State::Ready);},Guard::Status);
    reject([](auto &f){f.status.result=7;},Guard::Status);
    reject([](auto &f){f.status.buttons=1;},Guard::Status);
    reject([](auto &f){f.status.output_attached=1;},Guard::Status);
    reject([](auto &f){f.status.link_ready=1;},Guard::Status);
    reject([](auto &f){f.status.pad_cleanup_result=7;},Guard::Status);
    reject([](auto &f){f.status.capture_deadline=1;},Guard::Status);
    reject([](auto &f){f.status.last_sequence=4;},Guard::Status);
    reject([](auto &f){f.status.now_ms=901;},Guard::Status);
    assert(arpostcheck::Check(good.owner,good.intent,good.status,999)==Guard::Status);
    assert(arpostcheck::Check(good.owner,good.intent,good.status,3001)==Guard::Status);
    for(unsigned i=0;i<sizeof(good.owner);++i){auto f=good;reinterpret_cast<unsigned char*>(&f.owner)[i]^=1;
        assert(arpostcheck::Check(f.owner,f.intent,f.status,1000)!=Guard::Passed);}
    for(unsigned i=0;i<sizeof(good.intent);++i){auto f=good;reinterpret_cast<unsigned char*>(&f.intent)[i]^=1;
        assert(arpostcheck::Check(f.owner,f.intent,f.status,1000)!=Guard::Passed);}
    std::printf("PASS postcheck guard: %u resealed invalid owner/intent/stopped-session cases, future/stale clocks, every-byte owner/intent corruption; no bond inference.\n",cases);
}
