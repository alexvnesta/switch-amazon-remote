// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstddef>
#include <cstdint>

namespace amazon_remote::controller_capabilities {
// Metadata-only observation core, NOT a driver hook or a command sender.
// The integration must supply validated received Command Complete framing,
// serialize the single writer, observe reset boundaries in order, and arrange
// safe snapshot access outside the callback. No SDK, heap, locks or I/O here.
inline constexpr std::uint16_t LocalVersion = 0x1001, VendorCapabilities = 0xfd53;
inline constexpr std::uint32_t MaximumReplies = 64;
enum class Outcome : std::uint32_t {
    NotObserved, Reported, ControllerError, Malformed, Conflicting
};
struct Reply {
    Outcome outcome{};
    std::uint32_t status{}, length{};
    std::uint32_t hci_version{}, hci_revision{}, lmp_version{}, manufacturer{}, lmp_subversion{};
    std::uint32_t filtering{}, max_filters{}, scan_storage_bytes{}, uninterpreted_extension_bytes{};
    bool operator==(const Reply &) const = default;
};
static_assert(sizeof(Reply) == 48);

inline bool Selected(std::uint16_t opcode) {
    return opcode == LocalVersion || opcode == VendorCapabilities;
}
// Unknown opcodes leave out untouched and never dereference payload. Selected
// malformed responses clear previous fields. Error status reads ONE byte only.
// Even a successful extended capability reply reads only its 9-byte prefix.
inline bool Decode(std::uint16_t opcode, const std::uint8_t *payload,
                   std::size_t length, Reply &out) {
    if (!Selected(opcode)) return false;
    out = {};
    out.outcome = Outcome::Malformed;
    if (!payload || length < 1 || length > 252) return true;
    out.length = static_cast<std::uint32_t>(length);
    out.status = payload[0];
    if (out.status) { out.outcome = Outcome::ControllerError; return true; }
    if (opcode == LocalVersion) {
        if (length != 9) return true;
        out.hci_version = payload[1];
        out.hci_revision = payload[2] | (std::uint32_t(payload[3]) << 8);
        out.lmp_version = payload[4];
        out.manufacturer = payload[5] | (std::uint32_t(payload[6]) << 8);
        out.lmp_subversion = payload[7] | (std::uint32_t(payload[8]) << 8);
    } else {
        // Legacy v0.55 prefix, or prefix + complete version field. Extensions
        // are counted, never copied or interpreted as a wake capability.
        if (length < 9 || length == 10 || payload[6] > 1) return true;
        out.filtering = payload[6];
        out.max_filters = payload[7];
        out.scan_storage_bytes = payload[3] | (std::uint32_t(payload[4]) << 8);
        out.uninterpreted_extension_bytes = static_cast<std::uint32_t>(length - 9);
    }
    out.outcome = Outcome::Reported;
    return true;
}

struct Entry {
    Reply first{};
    std::uint32_t count{}, conflicting{};
    Outcome EffectiveOutcome() const { return conflicting ? Outcome::Conflicting : first.outcome; }
};
struct Snapshot {
    std::uint32_t schema{1}, boot_tag{}, reset_epoch{}, total_replies{}, limit_reached{}, epoch_exhausted{};
    Entry local_version{}, vendor_capabilities{};
};
static_assert(sizeof(Snapshot) == 136);

// Owner supplies a nonzero per-boot tag before callbacks can arrive. It is a
// correlation token, NOT controller identity, build attestation or authenticity.
// No reset of this object is offered: a new capture requires a fresh instance.
class Observer {
public:
    bool Start(std::uint32_t boot_tag) {
        if (!boot_tag || state_.boot_tag) return false;
        state_.boot_tag = boot_tag;
        return true;
    }
    bool ObserveComplete(std::uint16_t opcode, const std::uint8_t *payload, std::size_t length) {
        if (!Selected(opcode) || !state_.boot_tag || state_.epoch_exhausted) return false;
        if (state_.total_replies == MaximumReplies) {
            state_.limit_reached = 1;
            return false; // Do not dereference further payloads after the limit.
        }
        Reply reply{};
        Decode(opcode, payload, length, reply);
        auto &entry = opcode == LocalVersion ? state_.local_version : state_.vendor_capabilities;
        if (!entry.count) entry.first = reply;
        else if (!(entry.first == reply)) entry.conflicting = 1;
        ++entry.count;
        ++state_.total_replies;
        return true;
    }
    // Notification of an independently observed reset boundary, never a request
    // to reset the controller. Even failed reset attempts must separate epochs.
    bool ObserveResetBoundary() {
        if (!state_.boot_tag || state_.epoch_exhausted) return false;
        state_.local_version = {};
        state_.vendor_capabilities = {};
        if (state_.reset_epoch == UINT32_MAX) {
            state_.epoch_exhausted = 1;
            return false;
        }
        ++state_.reset_epoch;
        return true;
    }
    // Caller must serialize with the writer. No thread-safety is implied.
    Snapshot ReadSerialized() const { return state_; }
private:
    Snapshot state_{};
};
}
