// SPDX-License-Identifier: MIT
// Original implementation for the captured Amazon VID0171/PID0427 descriptor.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace amazon_remote {
struct Usage {
    std::uint16_t page = 0;
    std::uint16_t code = 0;
    bool operator==(const Usage &other) const { return page == other.page && code == other.code; }
};
struct HeldUsages {
    std::array<Usage, 8> values{};
    std::size_t count = 0;
    bool Contains(std::uint16_t page, std::uint16_t code) const;
    bool operator==(const HeldUsages &other) const;
};
enum class Update { Changed, Unchanged, Ignored, Invalid, UnsupportedDescriptor };
enum Button : std::uint32_t {
    Up = 1u << 0, Down = 1u << 1, Left = 1u << 2, Right = 1u << 3,
    Confirm = 1u << 4, Back = 1u << 5, Home = 1u << 6, Menu = 1u << 7,
    PlayPause = 1u << 8, Rewind = 1u << 9, FastForward = 1u << 10,
    Voice = 1u << 11, App1 = 1u << 12, App2 = 1u << 13,
    App3 = 1u << 14, App4 = 1u << 15
};
struct ReportLayout {
    std::uint16_t page = 0;
    std::uint16_t maximum = 0;
    std::uint8_t width_bytes = 0;
    std::uint8_t slots = 0;
};

// Transport must supply the report ID separately (e.g. GATT Report Reference).
// Payload excludes that ID. No notification-byte/report-ID heuristic is used.
class RemoteDecoder {
public:
    bool Configure(const std::uint8_t *descriptor, std::size_t length);
    Update Ingest(std::uint8_t report_id, const std::uint8_t *payload, std::size_t length);
    bool Disconnect();
    HeldUsages Held() const;
    std::uint32_t Buttons() const;
    const ReportLayout *Layout(std::uint8_t report_id) const;
    bool IsConfigured() const { return configured_; }
private:
    std::array<ReportLayout, 3> layouts_{};
    std::array<HeldUsages, 3> held_{};
    bool configured_ = false;
};
} // namespace amazon_remote
