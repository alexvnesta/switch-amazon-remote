// SPDX-License-Identifier: GPL-2.0-only
// Compile with A3 and without A3 to exercise the actual variant-selected gate.
#include "../idle_health.hpp"
#include <cassert>
#include <cstdio>

namespace {
using namespace armodule;
#ifdef ARMODULE_WAKE_A3
static_assert(IdleHealthMinimumMs == 2000 && IdleHealthMinimumSamples == 4);
#else
static_assert(IdleHealthMinimumMs == 15000 && IdleHealthMinimumSamples == 20);
#endif

Status Frame(std::uint64_t time, std::uint64_t instance = 7) {
    Status s{};
    s.instance = instance;
    s.now_ms = time;
    if constexpr (Version == 3) s.reserved = StatusChecksum(s);
    return s;
}

void Seal(Status &s) {
    s.reserved = Version == 3 ? StatusChecksum(s) : 0;
}

std::uint64_t Qualify(IdleHealth &gate, std::uint64_t first = 1000,
                      std::uint64_t instance = 7) {
    const auto last = first + IdleHealthMinimumMs;
    for (auto time = first; time <= last; time += 100) {
        gate.Observe(Frame(time, instance), time);
    }
    assert(gate.Ready(last));
    return last;
}

template <class Mutation>
void RejectAndRestart(Mutation mutate, bool reseal = true) {
    IdleHealth gate;
    const auto last = Qualify(gate);
    auto bad = Frame(last + 100);
    mutate(bad);
    if (reseal) Seal(bad);
    gate.Observe(bad, last + 100);
    assert(!gate.Ready(last + 100) && gate.Seconds() == 0);
    gate.Observe(Frame(last + 200), last + 200);
    assert(!gate.Ready(last + 200));
    Qualify(gate, last + 300);
}
}

int main() {
    using namespace armodule;
    IdleHealth gate;
    assert(!gate.Ready(0) && !gate.Ready(1000) && gate.Seconds() == 0);

    // Enough distinct records cannot bypass the exact elapsed-time boundary.
    const std::uint64_t first = 1000;
    const auto boundary = first + IdleHealthMinimumMs;
    for (auto time = first; time < boundary; time += 100) {
        gate.Observe(Frame(time), time);
        assert(!gate.Ready(time));
    }
    gate.Observe(Frame(boundary - 1), boundary - 1);
    assert(!gate.Ready(boundary - 1));
    gate.Observe(Frame(boundary), boundary);
    assert(gate.Ready(boundary));
    assert(gate.Seconds() == IdleHealthMinimumMs / 1000);
    assert(!gate.Ready(boundary - 1));
    assert(gate.Ready(boundary + 2000));
    assert(!gate.Ready(boundary + 2001));

    // Exact duration is insufficient with one too few distinct publications.
    // Sparse spacing remains below the unchanged two-second inter-record gap.
    gate.Reset();
    for (unsigned i = 0; i < IdleHealthMinimumSamples - 1; ++i) {
        const auto time = first + IdleHealthMinimumMs * i / (IdleHealthMinimumSamples - 2);
        gate.Observe(Frame(time), time);
    }
    assert(!gate.Ready(boundary));
    for (unsigned i = 0; i < 1000; ++i) gate.Observe(Frame(boundary), boundary);
    assert(!gate.Ready(boundary)); // Cached re-reads are not fresh publications.
    gate.Observe(Frame(boundary + 1), boundary + 1);
    assert(gate.Ready(boundary + 1));

    // Legacy variants cannot accidentally inherit the short A3 qualification.
    gate.Reset();
    for (std::uint64_t time = 1000; time <= 3000; time += 500) gate.Observe(Frame(time), time);
#ifdef ARMODULE_WAKE_A3
    assert(gate.Ready(3000));
#else
    assert(!gate.Ready(3000));
    for (std::uint64_t time = 3500; time <= 16000; time += 500) gate.Observe(Frame(time), time);
    assert(gate.Ready(16000));
#endif

    // A gap of exactly two seconds is allowed; exceeding it restarts admission.
    gate.Reset();
    auto last = Qualify(gate);
    gate.Observe(Frame(last + 2000), last + 2000);
    assert(gate.Ready(last + 2000));
    gate.Observe(Frame(last + 4001), last + 4001);
    assert(!gate.Ready(last + 4001) && gate.Seconds() == 0);

    // Stale/future observations, a backwards publication and a new process all
    // invalidate prior qualification. They cannot reuse the prior duration.
    gate.Reset();
    last = Qualify(gate);
    gate.Observe(Frame(last), last + 2001);
    assert(!gate.Ready(last + 2001) && gate.Seconds() == 0);
    last = Qualify(gate, last + 2100);
    gate.Observe(Frame(last + 100), last + 99);
    assert(!gate.Ready(last + 100) && gate.Seconds() == 0);
    last = Qualify(gate, last + 200);
    gate.Observe(Frame(last - 1), last);
    assert(!gate.Ready(last) && gate.Seconds() == 0);
    last = Qualify(gate, last + 100);
    gate.Observe(Frame(last + 100, 8), last + 100);
    assert(!gate.Ready(last + 100) && gate.Seconds() == 0);
    Qualify(gate, last + 200, 8);

    RejectAndRestart([](Status &s) { s.result = 1; });
    RejectAndRestart([](Status &s) { s.output_attached = 1; });
    RejectAndRestart([](Status &s) { s.link_ready = 1; });
    RejectAndRestart([](Status &s) { s.buttons = 1; });
    RejectAndRestart([](Status &s) { s.state = unsigned(State::Scanning); });
    RejectAndRestart([](Status &s) { s.instance = 0; });
    RejectAndRestart([](Status &s) { s.now_ms = 0; });
    RejectAndRestart([](Status &s) { ++s.magic; });
    RejectAndRestart([](Status &s) { ++s.version; });
    RejectAndRestart([](Status &s) { s.reserved ^= 1; }, false);

    gate.Reset();
    assert(!gate.Ready(100000) && gate.Seconds() == 0);
    std::printf("PASS idle qualification: %llums/%u distinct records; boundaries, duplicates, freshness, gaps, instance/clock resets, errors and output guards\n",
                static_cast<unsigned long long>(IdleHealthMinimumMs), IdleHealthMinimumSamples);
}
