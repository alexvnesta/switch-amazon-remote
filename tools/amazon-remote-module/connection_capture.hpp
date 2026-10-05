// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../../mc_mitm/source/amazon_remote/connection_evidence.hpp"
#include <cstdio>
namespace armodule {
// Consumes read-only snapshots, never events from a driver queue. No MAC,
// foreign callback, bond/key data or raw memory appears in diagnostic output.
class ConnectionCapture {
public:
    using Logger=void (*)(const char *);
    std::uint32_t Count() const { return count_; }
    std::uint32_t Overflow() const { return overflow_; }
    bool Accept(const amazon_remote::ConnectionEvidenceWire &wire,std::uint64_t now,Logger logger) {
        if (!amazon_remote::ValidConnectionEvidence(wire) || wire.target_count<count_ || wire.overflow<overflow_) return false;
        for (std::uint32_t i=0;i<wire.target_count;++i) {
            if (wire.events[i].boot_ms>now) return false;
            if (i<count_ && std::memcmp(&wire.events[i],&accepted_[i],sizeof accepted_[i])) return false;
        }
        for (std::uint32_t i=count_;i<wire.target_count;++i) {
            const auto &e=wire.events[i];char line[256]{};
            std::snprintf(line,sizeof line,"TARGET_CONNECTION_CALLBACK index=%lu boot_ms=%llu result=%08lx status=%u client_if=%u connection_id=%lu reason_raw=%04x; passive_target_match_only=1 request_correlation_unproven=1",
                static_cast<unsigned long>(i),static_cast<unsigned long long>(e.boot_ms),
                static_cast<unsigned long>(e.result),unsigned(e.status),unsigned(e.client_if),
                static_cast<unsigned long>(e.connection_id),unsigned(e.reason));
            if (logger) logger(line);
        }
        if (wire.overflow!=overflow_) {
            char line[112]{};std::snprintf(line,sizeof line,"TARGET_CONNECTION_CALLBACK overflow=%lu; first8retained=1 later_callbacks_unavailable=1",static_cast<unsigned long>(wire.overflow));
            if (logger) logger(line);
        }
        accepted_=wire.events;count_=wire.target_count;overflow_=wire.overflow;return true;
    }
private:
    std::array<amazon_remote::ConnectionEvidenceEvent,amazon_remote::ConnectionEvidenceCapacity> accepted_{};
    std::uint32_t count_{},overflow_{};
};
}
