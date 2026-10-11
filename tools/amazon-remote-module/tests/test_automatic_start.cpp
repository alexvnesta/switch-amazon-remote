// SPDX-License-Identifier: GPL-2.0-only
#include "../automatic_start.hpp"
#include "../../../mc_mitm/source/amazon_remote/session_budget.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace armodule;
using A = AutomaticStart::Action;
int main() {
    AutomaticStart s;
    assert(s.Next(1000,false,true,State::Idle,false)==A::None);
    s.PauseUnused(); assert(!s.Consumed());
    assert(s.Next(2000,true,false,State::Idle,false)==A::None);
    assert(s.Next(17000,true,true,State::Idle,false)==A::Arm);
    assert(s.Consumed());
    for (unsigned i=0;i<100;++i) assert(s.Next(17001+i,true,true,State::Idle,false)==A::None);
    assert(s.Next(17200,true,false,State::Scanning,false)==A::None);
    assert(s.Next(17300,true,false,State::Scanning,true)==A::Link);
    for (unsigned i=0;i<100;++i) assert(s.Next(17400+i,true,false,State::Scanning,true)==A::None);
    assert(s.Next(18000,true,false,State::Ready,true)==A::None);
    assert(s.Next(19000,false,false,State::Suspended,false)==A::None);
    assert(s.Next(20000,true,true,State::Idle,true)==A::None); // Never rearm after sleep.
    for (State terminal : {State::Stopped,State::Disconnected,State::Stopping}) {
        AutomaticStart p;
        assert(p.Next(1,true,false,terminal,false)==A::None);
        assert(p.Next(2,true,true,State::Idle,true)==A::None);
    }
    AutomaticStart timeout;
    assert(timeout.Next(1,true,false,State::Idle,false)==A::None);
    assert(timeout.Next(120001,true,false,State::Idle,false)==A::Stop);
    assert(timeout.Next(120002,true,true,State::Idle,true)==A::None);
    AutomaticStart backwards;
    assert(backwards.Next(10,true,false,State::Idle,false)==A::None);
    assert(backwards.Next(9,true,true,State::Idle,true)==A::Stop);
    Command c{}; c.instance=1;c.issued_ms=1;c.sequence=1;
    for(unsigned k=0;k<10;++k) { c.kind=k; assert(ValidCommand(c,1,0,2)==(k==3)); }
    amazon_remote::SessionBudget ordinary(true), background(true,true), disabled(false,true);
    assert(!background.AdmitReady(1000,91000)); // No extension to setup deadline.
    assert(background.AdmitReady(1000,1100));
    assert(ordinary.AdmitReady(1000,1100));
    assert(!background.Expired(1000,86400000));
    assert(ordinary.Expired(1000,86400000));
    assert(background.Expired(1000,1099));
    assert(disabled.Expired(1000,91000));
    puts("PASS automatic startup: fully-awake admission, one arm/link, stop-only UI, startup timeout, power/clock fail-closed, sustained background budget");
}
