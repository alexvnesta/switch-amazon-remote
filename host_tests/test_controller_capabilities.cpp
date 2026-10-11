// SPDX-License-Identifier: GPL-2.0-only
#include "../mc_mitm/source/amazon_remote/controller_capabilities.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
using namespace amazon_remote::controller_capabilities;

int main() {
    std::array<std::uint8_t, 252> input{0, 8, 0x34, 0x12, 7, 0x0f, 0, 0x78, 0x56};
    Reply r{};
    assert(Decode(LocalVersion, input.data(), 9, r));
    assert(r.outcome == Outcome::Reported && r.hci_version == 8 && r.hci_revision == 0x1234);
    assert(r.lmp_version == 7 && r.manufacturer == 15 && r.lmp_subversion == 0x5678);
    assert(!r.filtering && !r.max_filters && !r.scan_storage_bytes);
    const auto version = r;
    for (unsigned opcode = 0; opcode <= 65535; ++opcode) {
        if (Selected(opcode)) continue;
        assert(!Decode(opcode, nullptr, SIZE_MAX, r) && r == version);
    }
    assert(Decode(VendorCapabilities, input.data(), 9, r));
    assert(r.outcome == Outcome::Reported && !r.filtering && r.max_filters == 0x78);
    input[6] = 1;
    for (std::size_t length = 0; length <= 253; ++length) {
        assert(Decode(VendorCapabilities, input.data(), length, r));
        const bool valid = length == 9 || (length >= 11 && length <= 252);
        assert((r.outcome == Outcome::Reported) == valid);
        if (valid) {
            assert(r.filtering == 1 && r.max_filters == 0x78 && r.scan_storage_bytes == 0x712);
            assert(r.uninterpreted_extension_bytes == length - 9);
        }
        assert(Decode(LocalVersion, input.data(), length, r));
        assert((r.outcome == Outcome::Reported) == (length == 9));
    }
    for (unsigned filtering = 2; filtering <= 255; ++filtering) {
        input[6] = filtering;
        assert(Decode(VendorCapabilities, input.data(), 9, r) && r.outcome == Outcome::Malformed);
        assert(!r.filtering && !r.max_filters && !r.scan_storage_bytes);
    }
    input[6] = 1;
    assert(Decode(VendorCapabilities, nullptr, 9, r) && r.outcome == Outcome::Malformed);

    // PROT_NONE after the selected prefix catches over-reads. In particular,
    // unrelated opcodes, error tails and unknown capability extensions cannot
    // be copied to output or accidentally inspected by this implementation.
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    auto *memory = static_cast<std::uint8_t *>(mmap(nullptr, page * 2, PROT_READ | PROT_WRITE,
                                                  MAP_PRIVATE | MAP_ANON, -1, 0));
    assert(memory != MAP_FAILED && mprotect(memory + page, page, PROT_NONE) == 0);
    const auto *inaccessible = memory + page;
    for (unsigned opcode = 0; opcode <= 65535; ++opcode) {
        if (!Selected(opcode)) assert(!Decode(opcode, inaccessible, 252, r));
    }
    for (unsigned status = 1; status <= 255; ++status) {
        memory[page - 1] = status;
        for (const auto opcode : {LocalVersion, VendorCapabilities}) {
            assert(Decode(opcode, memory + page - 1, 252, r));
            assert(r.outcome == Outcome::ControllerError && r.status == status);
            assert(!r.filtering && !r.manufacturer && !r.uninterpreted_extension_bytes);
        }
    }
    for (std::size_t i = 0; i < 9; ++i) memory[page - 9 + i] = input[i];
    assert(Decode(VendorCapabilities, memory + page - 9, 252, r) && r.outcome == Outcome::Reported);

    Observer observer;
    assert(!observer.ObserveComplete(VendorCapabilities, inaccessible, 9));
    assert(!observer.ObserveResetBoundary() && !observer.Start(0));
    assert(observer.Start(1234) && !observer.Start(1234) && !observer.Start(5678));
    assert(observer.ReadSerialized().vendor_capabilities.EffectiveOutcome() == Outcome::NotObserved);
    assert(observer.ObserveComplete(VendorCapabilities, input.data(), 9));
    assert(observer.ObserveComplete(VendorCapabilities, input.data(), 9));
    auto s = observer.ReadSerialized();
    assert(s.vendor_capabilities.count == 2 && s.vendor_capabilities.EffectiveOutcome() == Outcome::Reported);
    input[7] ^= 1;
    assert(observer.ObserveComplete(VendorCapabilities, input.data(), 9));
    s = observer.ReadSerialized();
    assert(s.vendor_capabilities.EffectiveOutcome() == Outcome::Conflicting);
    assert(s.vendor_capabilities.first.max_filters == 0x78); // Never silently replace with a preferred reply.
    assert(observer.ObserveResetBoundary());
    s = observer.ReadSerialized();
    assert(s.reset_epoch == 1 && s.total_replies == 3 && !s.local_version.count && !s.vendor_capabilities.count);
    assert(s.vendor_capabilities.EffectiveOutcome() == Outcome::NotObserved);
    for (unsigned i = 3; i < MaximumReplies; ++i) assert(observer.ObserveComplete(LocalVersion, input.data(), 9));
    assert(!observer.ObserveComplete(LocalVersion, inaccessible, 9));
    s = observer.ReadSerialized();
    assert(s.limit_reached && s.total_replies == MaximumReplies && s.boot_tag == 1234);
    assert(observer.ObserveResetBoundary());
    assert(!observer.ObserveComplete(VendorCapabilities, inaccessible, 9));
    assert(observer.ReadSerialized().vendor_capabilities.EffectiveOutcome() == Outcome::NotObserved);
    assert(munmap(memory, page * 2) == 0);
    std::puts("PASS controller metadata core: lengths, statuses, all opcodes, guarded reads, conflicts, epochs, limit");
}
