// SPDX-License-Identifier: GPL-2.0-only
#include "../startup_power.hpp"
#include "../automatic_start.hpp"
#include "../idle_health.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace armodule;
using A = AutomaticStart::Action;
static void event(StartupPower &p, unsigned state, unsigned ack=0) {
    p.EventObserved(); assert(!p.CanStart() && !p.CanResume());
    p.Request(state,0); assert(!p.CanStart() && !p.CanResume());
    p.Acknowledge(ack);
}
static void no_frontend_startup() {
    // Compose the production gates without a frontend command or any screen
    // state. This proves no UI predicate in these classes, NOT the timing of
    // real PSC events relative to the console's entrance screen.
    for(unsigned transition:{99u,1u,4u}) {
        StartupPower power(true);IdleHealth health;AutomaticStart automatic;
        if(transition!=99)event(power,transition);
        for(std::uint64_t now=1000;now<=3000;now+=500) {
            Status s{};s.instance=7;s.now_ms=now;s.reserved=StatusChecksum(s);
            health.Observe(s,now);
            const auto action=automatic.Next(now,power.CanStart(),health.Ready(now),State::Idle,false);
            assert(action==((now==3000 && transition==99)?A::Arm:A::None));
        }
        assert(health.Ready(3000));
        if(transition!=99) {
            event(power,0);
            assert(automatic.Next(3020,power.CanStart(),health.Ready(3020),State::Idle,false)==A::Arm);
        }
        assert(automatic.Next(3040,power.CanStart(),false,State::Scanning,false)==A::None);
        assert(automatic.Next(3060,power.CanStart(),false,State::Scanning,true)==A::Link);
        assert(automatic.Next(3080,power.CanStart(),false,State::Ready,true)==A::None);
    }
}
int main() {
    no_frontend_startup();
    StartupPower bad_registration(false); assert(bad_registration.Failed());
    assert(!bad_registration.CanStart() && !bad_registration.CanResume());
    StartupPower cold(true); AutomaticStart auto_start;
    assert(cold.CanStart() && cold.IsBootstrap() && !cold.CanResume());
    assert(auto_start.Next(8000,cold.CanStart(),false,State::Idle,false)==A::None);
    assert(auto_start.Next(10109,cold.CanStart(),true,State::Idle,false)==A::Arm);
    assert(auto_start.Next(10500,cold.CanStart(),false,State::Scanning,true)==A::Link);
    assert(auto_start.Next(10501,cold.CanStart(),true,State::Idle,true)==A::None);
    // A power transition cannot revive a stopped/failed automatic attempt.
    assert(auto_start.Next(12000,cold.CanStart(),true,State::Stopped,true)==A::None);
    event(cold,2); event(cold,0);
    assert(auto_start.Next(13000,cold.CanStart(),true,State::Idle,true)==A::None);
    for (unsigned state : {1u,2u,3u,4u,5u,6u,99u}) {
        StartupPower p(true); AutomaticStart a;
        p.EventObserved();
        assert(!p.CanStart() && !p.IsBootstrap());
        assert(a.Next(10109,p.CanStart(),true,State::Idle,false)==A::None);
        p.Request(state,0); p.Acknowledge(0);
        assert(!p.CanStart() && !p.CanResume());
        assert(p.Failed()==(state>=5));
    }
    StartupPower wake(true);
    event(wake,2); event(wake,4); event(wake,1);
    assert(!wake.CanStart() && !wake.CanResume() && !wake.IsBootstrap());
    event(wake,0); assert(wake.CanStart() && wake.CanResume() && !wake.IsBootstrap());
    event(wake,1); assert(!wake.CanStart() && !wake.CanResume());
    event(wake,0); assert(wake.CanResume());
    for (unsigned failure : {0u,1u,2u,3u,4u}) {
        StartupPower p(true);
        if (failure==0) { p.EventObserved(); p.Request(0,1); }
        if (failure==1) event(p,0,1);
        if (failure==2) { p.EventObserved(); p.EventObserved(); }
        if (failure==3) p.Acknowledge(0);
        if (failure==4) p.Request(0,0);
        assert(p.Failed() && !p.CanStart() && !p.CanResume());
        event(p,0); assert(p.Failed()); // No implicit reset or recovery.
    }
    puts("PASS A4 startup operational admission, first-event revocation, ACK-only wake, terminal failures and stop preservation");
}
