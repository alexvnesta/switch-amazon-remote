// SPDX-License-Identifier: GPL-2.0-only
#include "../mc_mitm/source/amazon_remote/intent_trace.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
using namespace amazon_remote;
int main() {
    for (const char *s : {"intents", "intents\n", "intents\r\n"}) assert(IsIntentMode(s, std::strlen(s)));
    for (const char *s : {"", "passive", "intents ", " intents", "intents\r", "intents\n\n"}) assert(!IsIntentMode(s, std::strlen(s)));
    assert(!IsIntentMode(nullptr, 7));
    assert(IntentDeadline(1234) == 121234);
    assert(!IntentWindowOpen(1233, 121234));
    assert(IntentWindowOpen(1234, 121234));
    assert(IntentWindowOpen(121233, 121234));
    assert(!IntentWindowOpen(121234, 121234));
    assert(!IntentWindowOpen(121235, 121234));
    assert(!IntentWindowOpen(0, 0));
    assert(IntentDeadline(UINT64_MAX - 10) == UINT64_MAX);
    assert(IntentWindowOpen(UINT64_MAX - 10, UINT64_MAX));
    assert(!IntentWindowOpen(UINT64_MAX, UINT64_MAX));
    IntentTrace trace; IntentRecord record{}; record.command = 999;
    assert(!trace.Pop(record) && record.command == 999);
    assert(!trace.Observe(55, BtmProgram, 42, 1, nullptr, 0));
    trace.SetEnabled(true);
    std::array<std::uint8_t, 255> payload{};
    for (unsigned i = 0; i < payload.size(); ++i) payload[i] = i;
    const auto original = payload;
    for (unsigned cmd = 0; cmd < 100; ++cmd) {
        const auto size = ExpectedIntentSize(cmd);
        assert(trace.Observe(cmd, BtmProgram, 42, 3, payload.data(), size) == IsIntentCommand(cmd));
        if (IsIntentCommand(cmd)) {
            assert(trace.Pop(record) && record.command == cmd && record.program_id == BtmProgram && record.process_id == 42);
            assert(record.copied_size == size && record.reported_size == size);
            assert(std::equal(payload.begin(), payload.begin() + size, record.payload.begin()));
        }
        assert(payload == original);
    }
    assert(!trace.Observe(55, 0, 42, 1, nullptr, 0));
    assert(trace.Observe(55, HidProgram, 42, 1, nullptr, 0)); assert(trace.Pop(record) && record.flags == 0);
    for (unsigned size = 0; size <= 255; ++size) {
        assert(trace.Observe(57, BtmProgram, 42, 4, payload.data(), size));
        assert(trace.Pop(record));
        assert(record.copied_size == std::min<unsigned>(size, 62));
        assert(bool(record.flags & IntentUnexpectedSize) == (size != 62));
        assert(bool(record.flags & IntentTruncated) == (size > 62));
        assert(std::equal(payload.begin(), payload.begin() + record.copied_size, record.payload.begin()));
        assert(payload == original);
    }
    assert(trace.Observe(58, BtmProgram, 42, 5, nullptr, 62));
    assert(trace.Pop(record) && record.flags == IntentNullPayload && record.copied_size == 0);
    std::array<std::uint8_t, 20> uuid{};
    for (unsigned size : {0u, 1u, 2u, 4u, 16u, 17u, 255u}) {
        uuid[0] = size;
        assert(trace.Observe(62, BtmProgram, 42, 6, uuid.data(), uuid.size()));
        assert(trace.Pop(record));
        assert(bool(record.flags & IntentInvalidUuidSize) == (size != 2 && size != 4 && size != 16));
    }
    assert(trace.Observe(57, BtmProgram, 42, UINT64_MAX, payload.data(), payload.size())); assert(trace.Pop(record));
    record.process_id = UINT64_MAX; record.reported_size = UINT64_MAX; record.sequence = UINT32_MAX;
    char text[1024]; std::memset(text, 'X', sizeof(text));
    assert(FormatIntent(text, sizeof(text), record));
    assert(std::strlen(text) < 512 && text[512] == 'X');
    assert(std::strstr(text, "driver_result=unobserved") && std::strstr(text, "ownership=unproven"));
    assert(!FormatIntent(nullptr, 1, record)); assert(!FormatIntent(text, 0, record));
    for (unsigned cap = 1; cap < 32; ++cap) {
        std::memset(text, 'X', sizeof(text)); assert(!FormatIntent(text, cap, record));
        assert(text[cap-1] == 0 && text[cap] == 'X');
    }
    record.copied_size = 63; assert(!FormatIntent(text, sizeof(text), record) && text[0] == 0);
    IntentTrace queue; queue.SetEnabled(true);
    for (unsigned i = 0; i < 17; ++i) assert(queue.Observe(55, BtmProgram, 42, i, nullptr, 0) == (i < 16));
    queue.SetEnabled(false);
    for (unsigned i = 0; i < 16; ++i) assert(queue.Pop(record) && record.sequence == i && record.boot_ms == i);
    assert(!queue.Pop(record)); IntentCounters counters{}; queue.SnapshotInto(counters);
    assert(counters.seen == 17 && counters.queued == 16 && counters.drops == 1);
    IntentTrace concurrent; concurrent.SetEnabled(true);
    std::atomic<bool> done{false}; unsigned consumed = 0;
    std::thread consumer([&] {
        std::uint64_t previous = 0; bool have_previous = false;
        auto consume = [&] { if (!concurrent.Pop(record)) return false;
            assert(!have_previous || record.boot_ms > previous); previous = record.boot_ms; have_previous = true; ++consumed; return true; };
        while (!done.load(std::memory_order_acquire)) if (!consume()) std::this_thread::yield();
        while (consume()) {}
    });
    std::thread producer([&] {
        for (unsigned i = 0; i < 100000; ++i) concurrent.Observe(55, BtmProgram, 42, i, nullptr, 0);
        done.store(true, std::memory_order_release);
    });
    producer.join(); consumer.join(); concurrent.SnapshotInto(counters);
    assert(counters.seen == 100000 && counters.queued == consumed && counters.queued + counters.drops == counters.seen);
    std::puts("PASS opt-in, absolute120s deadline, 13 commands, actor scope, immutable bounded payloads, malformed metadata, formatter limits, FIFO, 100000 concurrent intent observations");
}
