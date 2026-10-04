#include "../mc_mitm/source/amazon_remote/ble_preflight.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    using namespace amazon_remote;
    assert(DecodeFlag(true, 1, 0) == FlagState::False);
    assert(DecodeFlag(true, 1, 1) == FlagState::True);
    for (std::uint64_t size : {0ull, 2ull, 4ull, 8ull})
        assert(DecodeFlag(true, size, 1) == FlagState::Unknown);
    assert(DecodeFlag(false, 1, 1) == FlagState::Unknown);
    assert(DecodeFlag(true, 1, 2) == FlagState::Unknown);
    assert(DecodeFlag(true, 1, 255) == FlagState::Unknown);
    const FlagState flags[] = {FlagState::Unknown, FlagState::False, FlagState::True};
    for (auto ble : flags) for (auto config : flags) for (auto debug : flags) for (auto radio : flags) {
        BlePreflight value{ble, config, debug, radio};
        assert(value.ExplicitlyBlocked() == (ble == FlagState::True || config == FlagState::True ||
            debug == FlagState::True || radio == FlagState::False));
    }
    assert(!BlePreflight{}.ExplicitlyBlocked()); // Unknown must never masquerade as disabled.
    for (unsigned count = 0; count <= 255; ++count) assert(ValidScanCount(count) == (count <= 10));
    const std::uint8_t expected[] = {0xfb,0x34,0x9b,0x5f,0x80,0,0,0x80,0,0x10,0,0,0x12,0x18,0,0};
    for (unsigned i = 0; i < 16; ++i) assert(HidServiceUuid128[i] == expected[i]);
    std::puts("BLE preflight tests passed (81 state combinations, invalid-size/value guards)");
}
