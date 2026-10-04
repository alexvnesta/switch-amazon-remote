// SPDX-License-Identifier: GPL-2.0-only
#include "../controller.hpp"
#include "../protocol.hpp"
#include "../io.hpp"
#include "../config.hpp"
#include <cassert>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <unistd.h>

using namespace armodule;
int HorizonRename(const char *source, const char *destination) {
    struct stat present{};
    if (::stat(destination,&present) == 0) { errno = EEXIST; return -1; }
    return std::rename(source,destination);
}
struct FakePad {
    unsigned fail_step{}, calls{}; std::vector<unsigned> order; std::vector<std::uint64_t> states;
    std::uint32_t Call(unsigned operation) { order.push_back(operation); return ++calls == fail_step ? 42 : 0; }
    std::uint32_t Open() { return Call(1); }
    std::uint32_t AttachSession() { return Call(2); }
    std::uint32_t AttachDevice() { return Call(3); }
    std::uint32_t Set(std::uint64_t buttons) { states.push_back(buttons); return Call(4); }
    std::uint32_t DetachDevice() { return Call(5); }
    std::uint32_t ReleaseSession() { return Call(6); }
    void Close() { Call(7); }
};
void Mapping() {
    using namespace amazon_remote;
    assert(MapButtons(Confirm) == 1 && MapButtons(Back) == 2 && MapButtons(Menu) == 1024);
    assert(MapButtons(Home) == 262144 && MapButtons(Up) == 8192 && MapButtons(Down) == 32768);
    assert(MapButtons(Left) == 4096 && MapButtons(Right) == 16384);
    constexpr auto unmapped = PlayPause | Rewind | FastForward | Voice | App1 | App2 | App3 | App4;
    assert(MapButtons(unmapped | 0xffff0000u) == 0);
    for (unsigned mask = 0; mask < 65536; ++mask) {
        std::uint64_t separate = 0;
        for (unsigned bit = 0; bit < 16; ++bit) if (mask & (1u<<bit)) separate |= MapButtons(1u<<bit);
        assert(MapButtons(mask) == separate);
    }
}
void Ownership() {
    FakePad backend;
    {
        OwnedController pad(backend);
        assert(!pad.Attached() && pad.Update(0) != 0);
        assert(pad.Start() == 0 && pad.Attached());
        assert(pad.SuccessfulWrites() == 1 && pad.LastAcknowledgedButtons() == 0);
        assert(pad.Start() != 0);
        assert(pad.Update(amazon_remote::Confirm) == 0);
        assert(pad.SuccessfulWrites() == 2 && pad.LastAcknowledgedButtons() == 1);
        const auto count = backend.states.size();
        assert(pad.Update(amazon_remote::Confirm) == 0 && backend.states.size() == count);
        assert(pad.Update(0) == 0 && backend.states.back() == 0);
        assert(pad.Update(amazon_remote::Home) == 0);
        assert(pad.Stop() == 0 && !pad.Attached() && backend.states.back() == 0);
        assert(pad.SuccessfulWrites() == 5 && pad.LastAcknowledgedButtons() == 0 && pad.CleanupError() == 0);
        const auto calls = backend.calls;
        assert(pad.Stop() == 0 && backend.calls == calls);
    }
    assert((backend.order == std::vector<unsigned>{1,2,3,4,4,4,4,4,5,6,7}));
    const std::vector<std::vector<unsigned>> orders{{}, {1}, {1,2,7}, {1,2,3,6,7}, {1,2,3,4,4,5,6,7}};
    for (unsigned failure = 1; failure <= 4; ++failure) {
        FakePad failed; failed.fail_step = failure; OwnedController pad(failed);
        assert(pad.Start() == 42 && !pad.Attached());
        assert(failed.order == orders[failure]);
        const auto count = failed.calls; assert(pad.Start() != 0 && failed.calls == count);
    }
    for (unsigned failure = 5; failure <= 7; ++failure) {
        FakePad failed; failed.fail_step = failure; OwnedController pad(failed);
        assert(pad.Start() == 0);
        if (failure == 5) assert(pad.Update(amazon_remote::Up) == 42);
        else assert(pad.Stop() == 42);
        assert(!pad.Attached() && failed.states.back() == 0);
        assert(failed.order[failed.order.size()-3] == 5 && failed.order[failed.order.size()-2] == 6 && failed.order.back() == 7);
        const auto count = failed.calls; pad.Stop(); assert(count == failed.calls);
    }
    // A failed neutral write must not overwrite the last successfully ACKed key.
    FakePad stale; OwnedController held(stale);
    assert(held.Start() == 0 && held.Update(amazon_remote::Confirm) == 0);
    stale.fail_step = stale.calls + 1;
    assert(held.Stop() == 42 && held.CleanupError() == 42);
    assert(held.LastAcknowledgedButtons() == 1 && held.SuccessfulWrites() == 2);
    assert(held.Stop() == 42 && held.Start() != 0);
}
void NeutralCheck() {
    for (unsigned failure = 0; failure <= 7; ++failure) {
        FakePad backend; backend.fail_step = failure;
        OwnedController pad(backend);
        assert(NeutralControllerCheck(pad) == (failure ? 42u : 0u));
        assert(!pad.Attached());
        for (auto buttons : backend.states) assert(buttons == 0);
        if (!failure) {
            assert(pad.SuccessfulWrites() == 2 && pad.LastAcknowledgedButtons() == 0);
            assert(pad.Start() == 0); // Successful neutral check does not poison the later real session.
        } else assert(pad.Start() != 0);
    }
}
void Protocol() {
    Command c{}; c.instance = 100; c.issued_ms = 200; c.sequence = 1; c.kind = unsigned(CommandKind::Arm);
    assert(ValidCommand(c,100,0,200) && ValidCommand(c,100,0,2200));
    assert(!ValidCommand(c,100,0,2201) && !ValidCommand(c,100,0,199));
    assert(!ValidCommand(c,101,0,200) && !ValidCommand(c,100,1,200));
    assert(!ValidCommand(c,0,0,200));
    for (unsigned kind = 0; kind < 10000; ++kind) {
        c.kind = kind; assert(ValidCommand(c,100,0,200) == (kind >= 1 && kind <= 4));
    }
    c.kind = 1; c.reserved = 1; assert(!ValidCommand(c,100,0,200)); c.reserved = 0;
    c.magic ^= 1; assert(!ValidCommand(c,100,0,200)); c.magic ^= 1;
    c.version = 1; assert(!ValidCommand(c,100,0,200)); c.version = Version;
    c.sequence = 0; assert(!ValidCommand(c,100,0,200)); c.sequence = 1;
    c.issued_ms = 0; assert(!ValidCommand(c,100,0,200));
    c.issued_ms = std::numeric_limits<std::uint64_t>::max();
    assert(ValidCommand(c,100,0,c.issued_ms) && !ValidCommand(c,100,0,0));
    Status s{}; s.instance = 100; s.now_ms = 200;
    assert(FreshStatus(s,200) && !FreshStatus(s,199) && !FreshStatus(s,2201));
    s.state = 6; assert(!FreshStatus(s,200)); s.state = 0;
    s.link_ready = 2; assert(!FreshStatus(s,200)); s.link_ready = 0;
    s.output_attached = 2; assert(!FreshStatus(s,200)); s.output_attached = 0;
    s.controller_check = 3; assert(!FreshStatus(s,200)); s.controller_check = 0;
    s.version = 1; assert(!FreshStatus(s,200)); s.version = Version;
    s.reserved = 1; assert(!FreshStatus(s,200));
}
void Policy() {
    SessionPolicy p;
    assert(p.Get() == State::Idle && !p.Active());
    assert(p.CheckController() && !p.CheckController() && p.Get() == State::Idle && !p.Active());
    assert(!p.Link(true) && !p.Ready());
    assert(p.Arm() && !p.Arm() && p.Active());
    assert(!p.CheckController());
    assert(!p.Link(false) && p.Get() == State::Scanning);
    assert(p.Link(true) && !p.Link(true));
    assert(p.Ready() && !p.Ready() && p.Get() == State::Ready);
    p.Suspend(); assert(!p.Active() && !p.Arm());
    p.Resume(); assert(p.Get() == State::Stopped && !p.Arm() && !p.Link(true));
    for (unsigned boundary = 0; boundary < 4; ++boundary) {
        SessionPolicy stage;
        if (boundary > 0) stage.Arm();
        if (boundary > 1) stage.Link(true);
        if (boundary > 2) stage.Ready();
        stage.Stop(); assert(!stage.Active() && !stage.Arm() && !stage.Ready());
        assert(!stage.CheckController());
        stage.Suspend(); stage.Resume(); assert(stage.Get() == State::Stopped && !stage.Arm());
    }
    InputHoldGuard guard;
    assert(!guard.Accept(0,0));
    assert(guard.Accept(amazon_remote::Home,100));
    assert(guard.Accept(amazon_remote::Confirm,10099));
    assert(!guard.Accept(amazon_remote::Confirm,10100));
    assert(guard.Accept(0,10101) && guard.Accept(amazon_remote::Up,10102));
    assert(!guard.Accept(0,10101));
    InputHoldGuard unmapped; assert(unmapped.Accept(amazon_remote::Voice,1));
    assert(unmapped.Accept(amazon_remote::Voice,10000000));
    for (unsigned power = 0; power < 10000; ++power) {
        const auto decision = ClassifyPower(power);
        assert(decision.valid == (power < 6));
        assert(decision.resume == (power == 0 || power == 1));
        assert(decision.suspend == (power == 2 || power == 3 || power == 5));
        assert(decision.shutdown == (power == 5));
    }
}
void FileProtocol() {
    char directory[] = "/tmp/amazon-module-test-XXXXXX"; assert(::mkdtemp(directory));
    const std::string path = std::string(directory)+"/command", temp = std::string(directory)+"/temp";
    Command c{}; c.kind = 1; c.instance = 5; c.sequence = 1; c.issued_ms = 2;
    Command out{}; out.instance = 999;
    assert(!ReadExact(path.c_str(),out) && out.instance == 999);
    assert(Publish(temp.c_str(),path.c_str(),c));
    assert(ReadExact(path.c_str(),out) && out.instance == 5);
    c.sequence = 2;
    assert(Publish(temp.c_str(),path.c_str(),c,HorizonRename));
    assert(ReadExact(path.c_str(),out) && out.sequence == 2);
    FILE *f = std::fopen(path.c_str(),"ab"); assert(f); std::fputc(1,f); std::fclose(f);
    out.instance = 999; assert(!ReadExact(path.c_str(),out) && out.instance == 999);
    f = std::fopen(path.c_str(),"wb"); assert(f); std::fputc(1,f); std::fclose(f);
    assert(!ReadExact(path.c_str(),out));
    f = std::fopen(temp.c_str(),"wb"); assert(f); std::fputs("preserve",f); std::fclose(f);
    assert(!Publish(temp.c_str(),path.c_str(),c));
    char retained[8]{}; assert(ReadExact(temp.c_str(),retained) && retained[0] == 'p');
    assert(std::remove(path.c_str()) == 0 && std::remove(temp.c_str()) == 0 && ::rmdir(directory) == 0);
}
void Configuration() {
    std::array<std::uint8_t,6> address{};
    constexpr char good[] = "02:AB:CD:12:34:56\n";
    assert(ParseTarget(good,18,address) && address[0] == 0x02 && address[2] == 0xcd);
    const auto saved = address;
    assert(!ParseTarget(nullptr,18,address));
    assert(!ParseTarget(good,17,address) && !ParseTarget(good,19,address));
    assert(!ParseTarget("00:00:00:00:00:00\n",18,address));
    assert(!ParseTarget("ff:ff:ff:ff:ff:ff\n",18,address));
    assert(address == saved);
    for (unsigned index = 0; index < 18; ++index) {
        std::string bad(good,18); bad[index] = 'x';
        assert(!ParseTarget(bad.data(),bad.size(),address) && address == saved);
    }
}
int main() {
    Mapping(); Ownership(); NeutralCheck(); Protocol(); Policy(); FileProtocol(); Configuration();
    std::puts("PASS: all 65536 mappings, neutral-only preflight, sticky cleanup/ACK telemetry, owned-pad failures, versioned freshness/replay, policy/power/hold guards and Horizon-style publication");
}
