// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "controller_capabilities.hpp"
#include <limits>

namespace amazon_remote::controller_query {
inline constexpr std::uint32_t Magic = 0x31514341, Version = 1;
inline constexpr std::uint32_t BeginCommand = 65018, ReadCommand = 65019;
inline constexpr std::uint64_t WindowMs = 12000;
enum class Phase : std::uint32_t { Idle, Waiting, Response, SubmissionFailed, TimedOut, Interrupted, Conflict };
struct Wire {
    std::uint32_t magic{Magic}, version{Version}, nonce{};
    Phase phase{};
    std::uint64_t started_ms{}, deadline_ms{}, finished_ms{};
    std::uint32_t issued{}, submission_done{}, submission_result{}, envelope_status{};
    std::uint32_t reply_count{}, late_count{}, reserved0{}, reserved1{};
    controller_capabilities::Reply reply{};
};
static_assert(sizeof(Wire) == 120);

// Initial boot has no pending PSC transition. Once one arrives, only a
// successful acknowledgement of an awake request permits a new submission.
class PowerAdmission {
public:
    void Request(std::uint32_t state, std::uint32_t result) {
        allowed_ = false; pending_awake_ = !result && state <= 1;
    }
    void Acknowledge(std::uint32_t result) {
        allowed_ = pending_awake_ && !result; pending_awake_ = false;
    }
    bool Allowed() const { return allowed_; }
private:
    bool allowed_{true}, pending_awake_{};
};

// Single-owner state machine. Runtime serializes every method with one mutex,
// never held across an IPC, a wait or file I/O. No callbacks retain event data.
class Query {
public:
    bool Begin(std::uint32_t nonce, std::uint64_t now) {
        if (wire_.nonce || !nonce || !now || now > UINT64_MAX - WindowMs) return false;
        wire_.nonce = nonce; wire_.phase = Phase::Waiting;
        wire_.started_ms = now; wire_.deadline_ms = now + WindowMs;
        return true;
    }
    bool Dispatch(std::uint64_t now) {
        Poll(now);
        if (wire_.phase != Phase::Waiting || wire_.issued) return false;
        wire_.issued = 1;
        return true;
    }
    void Submission(std::uint32_t result, std::uint64_t now) {
        Poll(now);
        if (!wire_.nonce || wire_.submission_done) return;
        wire_.submission_done = 1; wire_.submission_result = result;
        if (wire_.phase != Phase::Waiting) return;
        if (result || !wire_.issued) Finish(Phase::SubmissionFailed, now);
        else Complete(now);
    }
    void Interrupt(std::uint64_t now) {
        if (wire_.phase == Phase::Waiting) Finish(Phase::Interrupted, now);
    }
    void Poll(std::uint64_t now) {
        if (wire_.phase != Phase::Waiting) return;
        if (now < wire_.started_ms) Finish(Phase::Interrupted, wire_.started_ms);
        else if (now >= wire_.deadline_ms) Finish(Phase::TimedOut, now);
    }
    // The existing custom-event envelope: u32 type, u16 opcode/status/size,
    // followed by return parameters. This handles ONLY our one fixed query.
    // Matching late replies are consumed without payload reads or rearming.
    bool Consume(const void *data, std::size_t size, std::uint64_t now) {
        if (!wire_.issued || !data || size < 6) return false;
        const auto *p = static_cast<const std::uint8_t *>(data);
        if (U32(p) != 1 || U16(p + 4) != controller_capabilities::VendorCapabilities) return false;
        Poll(now);
        if (wire_.phase != Phase::Waiting) {
            if (wire_.late_count != UINT32_MAX) ++wire_.late_count;
            return true;
        }
        if (wire_.reply_count) { Finish(Phase::Conflict, now); return true; }
        wire_.reply_count = 1;
        wire_.reply = {};
        if (size < 10) wire_.reply.outcome = controller_capabilities::Outcome::Malformed;
        else {
            wire_.envelope_status = U16(p + 6);
            const auto length = U16(p + 8);
            if (length > 252 || size - 10 < length) wire_.reply.outcome = controller_capabilities::Outcome::Malformed;
            else if (!wire_.envelope_status)
                controller_capabilities::Decode(controller_capabilities::VendorCapabilities, p + 10, length, wire_.reply);
        }
        Complete(now);
        return true;
    }
    bool LegacyBlocked(std::uint16_t opcode) const {
        // Reserve before creating the worker. Retain this opcode after timeout
        // to prevent an untagged late reply from being reassigned to a retry.
        return (wire_.nonce && (!wire_.submission_done || wire_.phase == Phase::Waiting)) ||
               (wire_.issued && (wire_.phase != Phase::Response ||
                                opcode == controller_capabilities::VendorCapabilities));
    }
    Wire Read(std::uint64_t now) { Poll(now); return wire_; }
private:
    static std::uint16_t U16(const std::uint8_t *p) { return p[0] | (std::uint16_t(p[1]) << 8); }
    static std::uint32_t U32(const std::uint8_t *p) { return U16(p) | (std::uint32_t(U16(p + 2)) << 16); }
    void Finish(Phase phase, std::uint64_t now) {
        wire_.phase = phase; wire_.finished_ms = now < wire_.started_ms ? wire_.started_ms : now;
    }
    void Complete(std::uint64_t now) {
        if (wire_.submission_done && !wire_.submission_result && wire_.reply_count) Finish(Phase::Response, now);
    }
    Wire wire_{};
};
}
