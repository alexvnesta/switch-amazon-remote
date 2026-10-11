// SPDX-License-Identifier: GPL-2.0-only
#include "../tools/amazon-remote-module/protocol.hpp"
#include <cassert>
#include <cstdio>
int main() {
    armodule::Command command{};
    command.instance=42;command.issued_ms=1000;command.sequence=1;
    for(unsigned kind=0;kind<10;++kind) {
        command.kind=kind;
        assert(armodule::ValidCommand(command,42,0,1001)==(kind>=1 && kind<=4));
    }
    command.kind=1;
    assert(!armodule::ValidCommand(command,43,0,1001));
    assert(!armodule::ValidCommand(command,42,1,1001));
    assert(!armodule::ValidCommand(command,42,0,999));
    assert(!armodule::ValidCommand(command,42,0,3001));
    for (unsigned phase=0;phase<4;++phase) {
        armodule::SessionPolicy policy;
        if (phase>0) assert(policy.Arm());
        if (phase>1) assert(policy.Link(true));
        if (phase>2) assert(policy.Ready());
        assert(policy.BeginStop());
        assert(policy.Get()==armodule::State::Stopping && !policy.Active());
        assert(!policy.Arm() && !policy.Link(true) && !policy.Ready() && !policy.CheckController());
        assert(!policy.BeginStop());policy.Stop();
        assert(!policy.BeginStop() && !policy.Arm());
        policy.StopDisconnected();
        assert(policy.Get()==armodule::State::Disconnected && !policy.Active());
        assert(!policy.BeginStop() && !policy.Arm() && !policy.Link(true) && !policy.Ready());
    }
    armodule::SessionPolicy suspended;
    assert(suspended.Arm() && suspended.BeginStop());
    suspended.Suspend();assert(!suspended.BeginStop());suspended.Resume();
    assert(suspended.Get()==armodule::State::Stopped && !suspended.Arm());
    armodule::Status status{};status.instance=42;status.now_ms=1000;
    status.state=unsigned(armodule::State::Stopping);status.reserved=armodule::StatusChecksum(status);
    assert(armodule::FreshStatus(status,1001));
    status.state=unsigned(armodule::State::Disconnected);status.reserved=armodule::StatusChecksum(status);
    assert(armodule::FreshStatus(status,1001));
    status.state=8;status.reserved=armodule::StatusChecksum(status);
    assert(!armodule::FreshStatus(status,1001));
    std::puts("PASS stop policy: non-active pending teardown, no early rearm or duplicate stop, power interruption and status validation.");
    std::puts("PASS reconnect command policy: only Arm/Link/Stop/neutral check admitted; enrollment/native bonding, replay/stale/future/foreign-instance refused.");
}
