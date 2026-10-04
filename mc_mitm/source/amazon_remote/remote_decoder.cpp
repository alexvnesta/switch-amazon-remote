// SPDX-License-Identifier: MIT
#include "remote_decoder.hpp"
#include <algorithm>

namespace amazon_remote {
namespace {
int Index(std::uint8_t id) { return id == 1 ? 0 : id == 2 ? 1 : id == 0xef ? 2 : -1; }
bool Less(const Usage &a, const Usage &b) { return a.page < b.page || (a.page == b.page && a.code < b.code); }
}
bool HeldUsages::Contains(std::uint16_t page, std::uint16_t code) const {
    for (std::size_t i = 0; i < count; ++i) if (values[i] == Usage{page, code}) return true;
    return false;
}
bool HeldUsages::operator==(const HeldUsages &other) const {
    if (count != other.count) return false;
    for (std::size_t i = 0; i < count; ++i) if (!(values[i] == other.values[i])) return false;
    return true;
}

bool RemoteDecoder::Configure(const std::uint8_t *descriptor, std::size_t length) {
    configured_ = false;
    layouts_ = {};
    held_ = {};
    if (!descriptor || length != 149) return false; // Captured PID0427 complete descriptor size.
    std::array<bool, 3> found{};
    std::uint32_t page = 0, maximum = 0, width = 0, slots = 0, report_id = 0;
    int collections = 0;
    for (std::size_t i = 0; i < length;) {
        std::uint8_t prefix = descriptor[i++];
        if (prefix == 0xfe) return false; // Long items unsupported by this narrow profile.
        std::size_t size = prefix & 3;
        if (size == 3) size = 4;
        if (length - i < size) return false;
        std::uint32_t value = 0;
        for (std::size_t n = 0; n < size; ++n) value |= std::uint32_t(descriptor[i + n]) << (8 * n);
        i += size;
        unsigned type = (prefix >> 2) & 3, tag = prefix >> 4;
        if (type == 1) {
            if (tag == 0) page = value;
            else if (tag == 2) maximum = value;
            else if (tag == 7) width = value;
            else if (tag == 8) { if (value == 0 || value > 255) return false; report_id = value; }
            else if (tag == 9) slots = value;
            else if (tag == 10 || tag == 11) return false; // Global stack absent from captured descriptor.
        } else if (type == 0) {
            if (tag == 10) ++collections;
            else if (tag == 12) { if (--collections < 0) return false; }
            else if (tag == 8) {
                int index = Index(static_cast<std::uint8_t>(report_id));
                if (index >= 0) {
                    if (found[index] || (value & 3) != 0 || page > 65535 || maximum > 65535 ||
                        (width != 8 && width != 16) || slots == 0 || slots > 3) return false;
                    layouts_[index] = {static_cast<std::uint16_t>(page), static_cast<std::uint16_t>(maximum),
                                       static_cast<std::uint8_t>(width / 8), static_cast<std::uint8_t>(slots)};
                    found[index] = true;
                }
            }
        }
    }
    // Deliberately recognize only the observed PID0427 input-array shape.
    // Do not silently interpret another Amazon generation using this profile.
    configured_ = collections == 0 && found[0] && found[1] && found[2] &&
        layouts_[0].page == 7 && layouts_[0].width_bytes == 1 && layouts_[0].slots == 3 && layouts_[0].maximum == 255 &&
        layouts_[1].page == 12 && layouts_[1].width_bytes == 2 && layouts_[1].slots == 2 && layouts_[1].maximum == 0x29c &&
        layouts_[2].page == 255 && layouts_[2].width_bytes == 1 && layouts_[2].slots == 3 && layouts_[2].maximum == 255;
    return configured_;
}

const ReportLayout *RemoteDecoder::Layout(std::uint8_t id) const {
    int index = Index(id);
    return configured_ && index >= 0 ? &layouts_[index] : nullptr;
}

Update RemoteDecoder::Ingest(std::uint8_t id, const std::uint8_t *payload, std::size_t length) {
    if (!configured_) return Update::UnsupportedDescriptor;
    int index = Index(id);
    if (index < 0) return Update::Ignored; // Battery/audio/vendor controls are not navigation.
    const auto &layout = layouts_[index];
    const std::size_t required = layout.width_bytes * layout.slots;
    if (!payload || length < required || length > 80) return Update::Invalid;
    HeldUsages next;
    for (std::size_t slot = 0; slot < layout.slots; ++slot) {
        std::size_t offset = slot * layout.width_bytes;
        std::uint16_t usage = payload[offset];
        if (layout.width_bytes == 2) usage |= std::uint16_t(payload[offset + 1]) << 8;
        if (usage > layout.maximum) return Update::Invalid;
        if (usage != 0 && !next.Contains(layout.page, usage)) next.values[next.count++] = {layout.page, usage};
    }
    std::sort(next.values.begin(), next.values.begin() + next.count, Less);
    if (next == held_[index]) return Update::Unchanged;
    held_[index] = next;
    return Update::Changed;
}

HeldUsages RemoteDecoder::Held() const {
    HeldUsages all;
    for (const auto &report : held_) for (std::size_t i = 0; i < report.count; ++i)
        if (!all.Contains(report.values[i].page, report.values[i].code)) all.values[all.count++] = report.values[i];
    std::sort(all.values.begin(), all.values.begin() + all.count, Less);
    return all;
}
bool RemoteDecoder::Disconnect() { bool changed = Held().count != 0; held_ = {}; return changed; }
std::uint32_t RemoteDecoder::Buttons() const {
    // Correlated with the user's deliberate button sequence on 2026-10-03.
    // These include Amazon-specific keyboard Back=F1 and OK=keypad Enter(58).
    auto held = Held();
    struct Mapping { std::uint16_t page, usage; Button button; };
    static constexpr Mapping mappings[] = {
        {7, 0x52, Up}, {7, 0x51, Down}, {7, 0x50, Left}, {7, 0x4f, Right},
        {7, 0x58, Confirm}, {7, 0xf1, Back},
        {12, 0x223, Home}, {12, 0x40, Menu}, {12, 0xcd, PlayPause},
        {12, 0xb4, Rewind}, {12, 0xb3, FastForward}, {12, 0x221, Voice},
        {255, 0xa1, App1}, {255, 0xa2, App2}, {255, 0xa3, App3}, {255, 0xa4, App4}
    };
    std::uint32_t result = 0;
    for (const auto &mapping : mappings) if (held.Contains(mapping.page, mapping.usage)) result |= mapping.button;
    return result;
}
} // namespace amazon_remote
