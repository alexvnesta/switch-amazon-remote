// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "target_sighting.hpp"
namespace amazon_remote {
// A physical ZL decision uses the existing 3000ms sighting limit. Its durable
// marker and ownership checks may then consume at most 250ms. This is not a
// new sighting, a retry budget, or permission to outlive the capture lease.
inline constexpr std::uint64_t ConnectDispatchBudgetMs = 250;
inline bool ValidConnectDispatch(const SightingWire &sighting, const SightingAddress &target,
        std::uint64_t scan_start, std::uint64_t accepted, std::uint64_t dispatch) {
    return accepted && dispatch >= accepted && dispatch - accepted <= ConnectDispatchBudgetMs &&
        AssessSighting(sighting, target, scan_start, accepted) == SightingAssessment::FreshCandidate;
}
}
