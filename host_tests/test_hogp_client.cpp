#include "../mc_mitm/source/amazon_remote/hogp_client.hpp"
#include "../mc_mitm/source/amazon_remote/remote_decoder.hpp"
#include "../mc_mitm/source/amazon_remote/k7q3m7_descriptor.hpp"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

using namespace amazon_remote;
std::vector<std::uint8_t> Descriptor() {
    std::ifstream f("mc_mitm/source/amazon_remote/k7q3m7-descriptor.bin", std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
Attribute Attr(Kind kind, std::uint16_t handle, std::uint16_t uuid, std::uint16_t end = 0, std::uint8_t prop = 0) {
    return {kind, handle, end, uuid, prop, 0, kind == Kind::Service};
}
std::vector<Attribute> Layout() {
    return {
        Attr(Kind::Service, 1, 0x180a, 4), Attr(Kind::Characteristic, 3, 0x2a50),
        Attr(Kind::Service, 5, 0x1812, 40), Attr(Kind::Characteristic, 8, 0x2a4b),
        Attr(Kind::Characteristic, 10, 0x2a4d, 0, 0x12), Attr(Kind::Descriptor, 11, 0x2902), Attr(Kind::Descriptor, 12, 0x2908),
        Attr(Kind::Characteristic, 14, 0x2a4d, 0, 0x12), Attr(Kind::Descriptor, 15, 0x2902), Attr(Kind::Descriptor, 16, 0x2908),
        Attr(Kind::Characteristic, 18, 0x2a4d, 0, 0x12), Attr(Kind::Descriptor, 19, 0x2902), Attr(Kind::Descriptor, 20, 0x2908),
        Attr(Kind::Characteristic, 22, 0x2a4d, 0, 0x08), Attr(Kind::Descriptor, 23, 0x2908),
        Attr(Kind::Service, 41, 0xfe03, 300)
    };
}
void AppendAll(HogpClient &c, const std::vector<Attribute> &attrs) {
    for (std::size_t i = 0; i < attrs.size(); i += 10)
        assert(c.Append(4, attrs.data() + i, std::min<std::size_t>(10, attrs.size() - i)));
}
void Capacity(const std::vector<std::uint8_t> &map) {
    for (unsigned n : {60, 61, 62, 256}) {
        HogpClient c(map.data(), map.size()); c.Connect(4);
        std::vector<Attribute> a;
        for (unsigned i = 1; i <= n; ++i) a.push_back(Attr(Kind::Descriptor, i, 0x2902));
        AppendAll(c, a); assert(c.Count() == n);
        const auto count = c.Count();
        auto duplicate = a.back(); assert(!c.Append(4, &duplicate, 1));
        assert(c.Count() == count); // Rejected batches cannot partially mutate state.
    }
    HogpClient c(map.data(), map.size()); c.Connect(4);
    std::vector<Attribute> full;
    for (unsigned i = 1; i <= 256; ++i) full.push_back(Attr(Kind::Descriptor, i, 0x2902));
    AppendAll(c, full); auto extra = Attr(Kind::Descriptor, 257, 0x2902);
    assert(!c.Append(4, &extra, 1)); assert(c.GetError() == Error::Capacity); assert(c.Count() == 256);
    c.Connect(4); assert(c.Count() == 0);
    assert(!c.Append(4, full.data(), 11)); assert(c.GetError() == Error::BadBatch);
    c.Connect(4); assert(!c.Append(5, full.data(), 10)); assert(c.Count() == 0); assert(c.GetPhase() == Phase::Cache);
    c.Connect(4); std::array<Attribute, 2> dup{extra, extra};
    assert(!c.Append(4, dup.data(), 2)); assert(c.Count() == 0);
}
void Progress(HogpClient &c, const std::vector<std::uint8_t> &map, bool duplicate_keyboard = false, std::uint8_t source = 1) {
    Request r;
    assert(c.Next(r, 1)); assert(r.characteristic.uuid == 0x2a50); assert(!c.Next(r, 2));
    const std::uint8_t pnp[]{source, 0x71, 1, 0x27, 4, 0x60, 0};
    assert(c.Complete(r, true, pnp, sizeof pnp));
    assert(c.Next(r, 2)); assert(r.characteristic.uuid == 0x2a4b);
    assert(c.Complete(r, true, map.data(), map.size()));
    for (auto ref : {std::array<std::uint8_t, 2>{1, 1}, {2, 1}, {0xef, 1}, {0xf2, 2}}) {
        assert(c.Next(r, 3)); assert(r.operation == Operation::ReadDescriptor); assert(r.descriptor.uuid == 0x2908);
        if (duplicate_keyboard && ref[0] == 2) ref[0] = 1;
        const auto ok = c.Complete(r, true, ref.data(), ref.size());
        if (duplicate_keyboard && ref[0] == 0xf2) { assert(!ok); assert(c.GetError() == Error::AmbiguousInput); return; }
        assert(ok);
    }
    unsigned operations = 0;
    while (c.Next(r, 5)) {
        // Only keyboard and consumer reports subscribed; vendor and output remain untouched.
        assert(r.characteristic.handle == 10 || r.characteristic.handle == 14);
        if (r.operation == Operation::WriteDescriptor) assert(r.descriptor.uuid == 0x2902 && r.size == 2 && r.value[0] == 1 && r.value[1] == 0);
        else assert(r.operation == Operation::RegisterNotification);
        assert(c.Complete(r, true, nullptr, 0)); ++operations;
    }
    assert(operations == 4); assert(c.GetPhase() == Phase::Ready);
}
void FingerprintWhitelist() {
    Request r;
    for (const auto operation : {Operation::ReadCharacteristic, Operation::ReadDescriptor,
                                Operation::RegisterNotification, Operation::WriteDescriptor}) {
        r.operation = operation;
        for (const auto service : {0x180a, 0x1812, 0xfe03, 0}) {
            r.service.uuid = service;
            for (const auto characteristic : {0x2a50, 0x2a4b, 0x2a4d, 0}) {
                r.characteristic.uuid = characteristic;
                const bool expected = operation == Operation::ReadCharacteristic &&
                    ((service == 0x180a && characteristic == 0x2a50) ||
                     (service == 0x1812 && characteristic == 0x2a4b));
                assert(IsFingerprintRead(r) == expected);
            }
        }
    }
}
std::vector<Attribute> MinimalFingerprintLayout() {
    return {Attr(Kind::Service, 0x12, 0x180a, 0x24),
            Attr(Kind::Characteristic, 0x24, 0x2a50),
            Attr(Kind::Service, 0x25, 0x1812, 0x41),
            Attr(Kind::Characteristic, 0x2e, 0x2a4b)};
}
void FingerprintServiceSelection(const std::vector<std::uint8_t> &map) {
    const auto minimal = MinimalFingerprintLayout();
    std::vector<Attribute> discovery{minimal[2], Attr(Kind::Service, 0x26, 0x180f), minimal[0]};
    discovery[1].primary = false; // Observed battery record: end=0, non-primary; type not inferred.
    const std::array<Attribute, 2> sentinel{Attr(Kind::Service, 900, 1, 901), Attr(Kind::Service, 902, 2, 903)};
    auto out = sentinel;
    assert(SelectFingerprintServices(discovery.data(), discovery.size(), out));
    assert(out[0].uuid == 0x180a && out[0].handle == 0x12 && out[0].end == 0x24);
    assert(out[1].uuid == 0x1812 && out[1].handle == 0x25 && out[1].end == 0x41);
    // Neither unrelated Included records nor malformed unrelated service ranges
    // are guessed into primary services. Required Included records are rejected.
    discovery.push_back(Attr(Kind::Included, 0, 0x180f));
    assert(SelectFingerprintServices(discovery.data(), discovery.size(), out));
    const auto unchanged = [&] {
        assert(out[0].handle == sentinel[0].handle && out[0].uuid == sentinel[0].uuid && out[0].end == sentinel[0].end);
        assert(out[1].handle == sentinel[1].handle && out[1].uuid == sentinel[1].uuid && out[1].end == sentinel[1].end);
    };
    out = sentinel;
    assert(!SelectFingerprintServices(nullptr, 2, out)); unchanged();
    for (const std::size_t count : {0, 100, 101}) {
        assert(!SelectFingerprintServices(discovery.data(), count, out)); unchanged();
    }
    for (unsigned variant = 0; variant < 12; ++variant) {
        std::vector<Attribute> bad{minimal[0], minimal[2]};
        if (variant == 0) bad.clear();
        if (variant == 1) bad.pop_back();
        if (variant == 2) bad.erase(bad.begin());
        if (variant == 3) bad.push_back(bad[0]);
        if (variant == 4) bad.push_back(bad[1]);
        if (variant == 5) bad[0].primary = false;
        if (variant == 6) bad[1].primary = false;
        if (variant == 7) bad[0].handle = 0;
        if (variant == 8) bad[1].end = bad[1].handle - 1;
        if (variant == 9) bad[0].end = bad[1].handle; // Touching inclusive ranges overlap.
        if (variant == 10) bad[0].kind = Kind::Included;
        if (variant == 11) bad[1].kind = Kind::Characteristic;
        out = sentinel; assert(!SelectFingerprintServices(bad.data(), bad.size(), out)); unchanged();
    }
    // The largest admitted discovery count is bounded; unrelated entries cannot
    // mask a duplicate or force the helper to inspect beyond the caller's extent.
    discovery.resize(99, Attr(Kind::Included, 0, 0xfe03));
    assert(SelectFingerprintServices(discovery.data(), discovery.size(), out));
    // Original cache append still rejects the invalid battery service record.
    HogpClient battery(map.data(), map.size(), true); battery.Connect(4);
    auto malformed_battery = Attr(Kind::Service, 0x26, 0x180f); malformed_battery.primary = false;
    assert(!battery.Append(4, &malformed_battery, 1));
    assert(battery.GetError() == Error::BadAttribute && battery.Count() == 0);
    // Four attributes suffice for identity-only mode, without any report or CCC.
    HogpClient c(map.data(), map.size(), true); c.Connect(4); AppendAll(c, minimal);
    assert(c.Count() == 4 && c.FinishCache(4));
    const std::uint8_t pnp[]{1, 0x71, 1, 0x27, 4, 0x60, 0};
    Request r; unsigned reads = 0;
    assert(c.Next(r, 1)); ++reads; assert(IsFingerprintRead(r) && r.characteristic.handle == 0x24);
    assert(c.Complete(r, true, pnp, sizeof pnp));
    assert(c.Next(r, 2)); ++reads; assert(IsFingerprintRead(r) && r.characteristic.handle == 0x2e);
    assert(c.Complete(r, true, map.data(), map.size()));
    assert(reads == 2 && !c.Next(r, 3) && c.GetPhase() == Phase::FingerprintVerified);
    InputReport report; assert(!c.Notification(4, 0x1812, 0x2a4d, pnp, 3, report));
    // Legacy mode still requires reports and their references.
    HogpClient legacy(map.data(), map.size()); legacy.Connect(4); AppendAll(legacy, minimal);
    assert(!legacy.FinishCache(4) && legacy.GetError() == Error::BadLayout);
    for (unsigned variant = 0; variant < 14; ++variant) {
        auto bad = minimal;
        if (variant == 0) bad.erase(bad.begin() + 1); // Missing PnP.
        if (variant == 1) bad.pop_back(); // Missing map.
        if (variant == 2) bad[1].handle = 0x11; // PnP before DI.
        if (variant == 3) bad[1].handle = 0x42; // PnP after DI.
        if (variant == 4) bad[3].handle = 0x23; // Map before HID.
        if (variant == 5) bad[3].handle = 0x42; // Map after HID.
        if (variant == 6) bad.push_back(Attr(Kind::Characteristic, 0x20, 0x2a50));
        if (variant == 7) bad.push_back(Attr(Kind::Characteristic, 0x30, 0x2a4b));
        if (variant == 8) bad[0].primary = false;
        if (variant == 9) bad[2].primary = false;
        if (variant == 10) bad[0].end = 0x25; // Overlapping ranges.
        if (variant == 11) bad.push_back(Attr(Kind::Service, 0x50, 0x180a, 0x60));
        if (variant == 12) bad.push_back(Attr(Kind::Service, 0x50, 0x1812, 0x60));
        if (variant == 13) bad[0].uuid = 0x180f; // Missing required service.
        HogpClient invalid(map.data(), map.size(), true); invalid.Connect(4); AppendAll(invalid, bad);
        assert(!invalid.FinishCache(4)); assert(invalid.GetError() == Error::BadLayout);
        assert(!invalid.Next(r, 10));
    }
}
void IdentityOnly(const std::vector<std::uint8_t> &map) {
    static_assert(static_cast<int>(Phase::Ready) == 6);
    static_assert(static_cast<int>(Phase::Failed) == 7);
    static_assert(static_cast<int>(Phase::FingerprintVerified) == 8);
    const std::uint8_t pnp[]{1, 0x71, 1, 0x27, 4, 0x60, 0};
    const std::uint8_t keyboard[]{0x52, 0x58, 0};
    auto prepare = [&](HogpClient &c) {
        c.Connect(4); AppendAll(c, Layout()); assert(c.FinishCache(4));
        assert(c.GetPhase() == Phase::Identity);
    };
    auto no_input = [&](const HogpClient &c) {
        InputReport output{0xaa, pnp, sizeof pnp};
        assert(!c.Notification(4, 0x1812, 0x2a4d, keyboard, sizeof keyboard, output));
        assert(output.id == 0xaa && output.payload == pnp && output.size == sizeof pnp);
        assert(c.GetPhase() != Phase::Ready);
    };
    auto failed = [&](HogpClient &c, Error expected) {
        assert(c.GetPhase() == Phase::Failed && c.GetError() == expected);
        Request forbidden; assert(!c.Next(forbidden, 50000)); no_input(c);
    };
    // Both defined PnP source values retain the exact VID/PID + map gate.
    for (const std::uint8_t source : {1, 2}) {
        HogpClient c(map.data(), map.size(), true); prepare(c); no_input(c);
        Request pnp_request, map_request;
        assert(c.Next(pnp_request, 1)); assert(IsFingerprintRead(pnp_request));
        assert(pnp_request.characteristic.uuid == 0x2a50 && pnp_request.size == 0);
        Request duplicate; assert(!c.Next(duplicate, 2));
        auto identity = std::array<std::uint8_t, 7>{source, 0x71, 1, 0x27, 4, 0x60, 0};
        assert(c.Complete(pnp_request, true, identity.data(), identity.size())); no_input(c);
        assert(c.Next(map_request, 3)); assert(IsFingerprintRead(map_request));
        assert(map_request.characteristic.uuid == 0x2a4b && map_request.size == 0);
        assert(c.Complete(map_request, true, map.data(), map.size()));
        assert(c.GetPhase() == Phase::FingerprintVerified && c.GetError() == Error::None);
        for (unsigned i = 0; i < 16; ++i) assert(!c.Next(duplicate, 50000 + i));
        assert(c.Tick(100000)); no_input(c);
        assert(!c.Complete(map_request, true, map.data(), map.size()));
        assert(c.GetPhase() == Phase::FingerprintVerified);
        // A new generation restarts the same restricted mode, never full HOGP.
        prepare(c); assert(c.Next(pnp_request, 1));
        assert(c.Complete(pnp_request, true, pnp, sizeof pnp));
        assert(c.Next(map_request, 2)); assert(c.Complete(map_request, true, map.data(), map.size()));
        assert(c.GetPhase() == Phase::FingerprintVerified); no_input(c);
    }
    // Wrong identity in any relevant field and truncated/empty PnP are terminal.
    for (unsigned variant = 0; variant < 6; ++variant) {
        HogpClient c(map.data(), map.size(), true); prepare(c); Request r; assert(c.Next(r, 0));
        std::array<std::uint8_t, 7> wrong{1, 0x71, 1, 0x27, 4, 0x60, 0};
        std::size_t size = wrong.size();
        if (variant == 0) wrong[0] = 0;
        if (variant == 1) wrong[0] = 3;
        if (variant == 2) wrong[1] ^= 1;
        if (variant == 3) wrong[3] ^= 1;
        if (variant == 4) --size;
        if (variant == 5) size = 0;
        assert(!c.Complete(r, true, wrong.data(), size)); failed(c, Error::WrongIdentity);
    }
    // Operation failures, null nonempty responses, timeouts, and stale requests
    // cannot advance either of the two identity stages or revive failed state.
    for (unsigned stage = 0; stage < 2; ++stage) {
        for (unsigned failure = 0; failure < 3; ++failure) {
            HogpClient c(map.data(), map.size(), true); prepare(c); Request r; assert(c.Next(r, 100));
            if (stage) { assert(c.Complete(r, true, pnp, sizeof pnp)); assert(c.Next(r, 200)); }
            if (failure == 0) { assert(!c.Complete(r, false, nullptr, 0)); failed(c, Error::OperationFailed); }
            if (failure == 1) { assert(!c.Complete(r, true, nullptr, 1)); failed(c, Error::UnexpectedResponse); }
            if (failure == 2) {
                const auto start = stage ? 200u : 100u;
                assert(c.Tick(start + HogpClient::TimeoutMilliseconds - 1));
                assert(!c.Tick(start + HogpClient::TimeoutMilliseconds)); failed(c, Error::Timeout);
            }
            assert(!c.Complete(r, true, stage ? map.data() : pnp, stage ? map.size() : sizeof pnp));
            no_input(c);
        }
        HogpClient c(map.data(), map.size(), true); prepare(c); Request stale; assert(c.Next(stale, 0));
        if (stage) { assert(c.Complete(stale, true, pnp, sizeof pnp)); assert(c.Next(stale, 1)); }
        prepare(c); Request fresh; assert(c.Next(fresh, 2));
        if (stage) { assert(c.Complete(fresh, true, pnp, sizeof pnp)); assert(c.Next(fresh, 3)); }
        assert(!c.Complete(stale, true, stage ? map.data() : pnp, stage ? map.size() : sizeof pnp));
        assert(c.GetPhase() == (stage ? Phase::Map : Phase::Identity));
        for (unsigned field = 0; field < 4; ++field) {
            auto forged = fresh;
            if (field == 0) ++forged.connection;
            if (field == 1) ++forged.characteristic.handle;
            if (field == 2) forged.operation = Operation::WriteDescriptor;
            if (field == 3) ++forged.generation;
            assert(!c.Complete(forged, true, nullptr, 0));
            assert(c.GetPhase() == (stage ? Phase::Map : Phase::Identity)); no_input(c);
        }
    }
    for (unsigned variant = 0; variant < 3; ++variant) {
        HogpClient c(map.data(), map.size(), true); prepare(c); Request r; assert(c.Next(r, 0));
        assert(c.Complete(r, true, pnp, sizeof pnp)); assert(c.Next(r, 1));
        auto wrong = map; wrong[0] ^= 1;
        assert(!c.Complete(r, true, variant == 0 ? wrong.data() : map.data(),
                          variant == 2 ? 0 : map.size() - (variant == 1 ? 1 : 0)));
        failed(c, Error::WrongReportMap);
    }
}
int main() {
    const auto map = Descriptor(); assert(map.size() == 149);
    static_assert(sizeof K7q3m7Descriptor == 149);
    assert(std::equal(map.begin(), map.end(), K7q3m7Descriptor));
    Capacity(map);
    FingerprintWhitelist(); FingerprintServiceSelection(map); IdentityOnly(map);
    auto attrs = Layout();
    // Exercise the actual failing aggregate shape: six batches of ten plus two.
    for (std::uint16_t h = 42; attrs.size() < 62; ++h) attrs.push_back(Attr(Kind::Descriptor, h, 0x2902));
    HogpClient c(map.data(), map.size()); c.Connect(4); AppendAll(c, attrs); assert(c.Count() == 62);
    assert(c.FinishCache(4)); Progress(c, map);
    InputReport report;
    const std::uint8_t keyboard[]{0x52, 0x58, 0}; const std::uint8_t consumer[]{0x23, 2, 0, 0};
    assert(c.Notification(4, 0x1812, 0x2a4d, keyboard, sizeof keyboard, report)); assert(report.id == 1);
    RemoteDecoder decoder; assert(decoder.Configure(map.data(), map.size()));
    assert(decoder.Ingest(report.id, report.payload, report.size) == Update::Changed);
    assert((decoder.Buttons() & (Up | Confirm)) == (Up | Confirm));
    assert(c.Notification(4, 0x1812, 0x2a4d, consumer, sizeof consumer, report)); assert(report.id == 2);
    assert(decoder.Ingest(report.id, report.payload, report.size) == Update::Changed);
    assert((decoder.Buttons() & (Up | Confirm | Home)) == (Up | Confirm | Home));
    assert(!c.Notification(5, 0x1812, 0x2a4d, keyboard, sizeof keyboard, report));
    assert(!c.Notification(4, 0xfe03, 0x2a4d, keyboard, sizeof keyboard, report));
    assert(!c.Notification(4, 0x1812, 0x2a4d, keyboard, 2, report));
    c.Disconnect(); decoder.Disconnect(); assert(decoder.Buttons() == 0);
    assert(!c.Notification(4, 0x1812, 0x2a4d, keyboard, sizeof keyboard, report));
    c.Connect(4); AppendAll(c, Layout()); assert(c.FinishCache(4)); Request stale; assert(c.Next(stale, 0));
    c.Connect(4); AppendAll(c, Layout()); assert(c.FinishCache(4)); Request fresh; assert(c.Next(fresh, 0));
    assert(!c.Complete(stale, true, nullptr, 0)); assert(c.GetPhase() == Phase::Identity);
    auto forged = fresh; forged.service.handle++;
    assert(!c.Complete(forged, true, nullptr, 0)); assert(c.GetPhase() == Phase::Identity);
    forged = fresh; forged.characteristic.instance++;
    assert(!c.Complete(forged, true, nullptr, 0)); assert(c.GetPhase() == Phase::Identity);
    forged = fresh; forged.service.uuid++;
    assert(!c.Complete(forged, true, nullptr, 0)); assert(c.GetPhase() == Phase::Identity);
    assert(!c.Tick(10000)); assert(c.GetError() == Error::Timeout);
    c.Connect(4); AppendAll(c, Layout()); assert(c.FinishCache(4)); Progress(c, map, true);
    c.Connect(4); AppendAll(c, Layout()); assert(c.FinishCache(4)); assert(c.Next(fresh, 0));
    const std::uint8_t speaker[]{1, 0x71, 1, 0x99, 9, 0, 0};
    assert(!c.Complete(fresh, true, speaker, sizeof speaker)); assert(c.GetError() == Error::WrongIdentity);
    c.Connect(4); AppendAll(c, Layout()); assert(c.FinishCache(4)); assert(c.Next(fresh, 0));
    const std::uint8_t pnp[]{1, 0x71, 1, 0x27, 4, 0x60, 0};
    assert(c.Complete(fresh, true, pnp, sizeof pnp)); assert(c.Next(fresh, 1));
    auto incompatible_map = map; incompatible_map[0] ^= 1;
    assert(!c.Complete(fresh, true, incompatible_map.data(), incompatible_map.size())); assert(c.GetError() == Error::WrongReportMap);
    c.Connect(4); AppendAll(c, Layout()); assert(c.FinishCache(4)); Progress(c, map, false, 2);
    for (const std::uint8_t source : {0, 3, 255}) {
        c.Connect(4); AppendAll(c, Layout()); assert(c.FinishCache(4)); assert(c.Next(fresh, 0));
        const std::uint8_t invalid_source[]{source, 0x71, 1, 0x27, 4, 0x60, 0};
        assert(!c.Complete(fresh, true, invalid_source, sizeof invalid_source)); assert(c.GetError() == Error::WrongIdentity);
    }
    std::cout << "PASS bounded HOGP discovery, 62 records, subscriptions, decoder integration, reconnect, timeout, strict service selector, minimal identity-only cutoff and whitelist\n";
}
