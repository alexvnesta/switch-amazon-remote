// Independently implemented BLE admission checks. GPL-2.0-only.
#pragma once
#include <cstddef>
#include <cstdint>
#include <array>

namespace amazon_remote {
enum class FlagState { Unknown, False, True };
inline FlagState DecodeFlag(bool success, std::uint64_t size, std::uint8_t value) {
    if (!success || size != 1 || value > 1) return FlagState::Unknown;
    return value ? FlagState::True : FlagState::False;
}
struct BlePreflight {
    FlagState ble_disabled{}, config_skip_boot{}, debug_skip_boot{}, radio_on{};
    bool ExplicitlyBlocked() const {
        return ble_disabled == FlagState::True || config_skip_boot == FlagState::True ||
            debug_skip_boot == FlagState::True || radio_on == FlagState::False;
    }
};
// Bluetooth-base HID UUID 00001812-0000-1000-8000-00805f9b34fb, on-air LE order.
inline constexpr std::array<std::uint8_t, 16> HidServiceUuid128{
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00};
inline bool ValidScanCount(std::uint8_t count) { return count <= 10; }
}
