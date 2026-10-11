// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <limits>

namespace amazon_remote::bond {
// Off-device integration contract. The private store has host coverage, but no
// production driver caller yet. These references must come from a trusted driver
// adapter, never an SD command, public IPC argument, address/name or build ID.
// Key bytes stay inside that adapter and its private transactional store.
struct Owner {
    std::uint64_t driver_instance{}, connection_epoch{}, local_identity{}, peer_identity{};
    std::uint32_t handle{};
    constexpr bool operator==(const Owner &) const = default;
    constexpr bool Valid() const {
        return driver_instance && connection_epoch && local_identity && peer_identity &&
               handle && handle != 0xffffffff;
    }
};
struct Request {
    Owner owner{};
    std::uint64_t enrollment{}, previous_generation{}, started_ms{}, deadline_ms{};
    std::uint32_t profile_version{};
    std::uint8_t minimum_key_bytes{16};
    bool user_approved{}, fingerprint_validated{};
    constexpr bool operator==(const Request &) const = default;
};
struct Completion {
    Owner owner{};
    std::uint64_t enrollment{}, candidate{};
    std::uint8_t key_bytes{};
    bool pairing_succeeded{}, bonding_negotiated{}, distribution_complete{},
         reusable_encryption_key{}, link_encrypted{}, debug_key{}, mitm_protected{};
    constexpr bool operator==(const Completion &) const = default;
};
enum class OpenResult { Opened, RejectedWithoutWrites, Unknown };
enum class CommitResult { Committed, NotCommitted, Unknown };
enum class Outcome {
    RejectedEvidence, StoreNotOpened, PreviousRecordRetained, RecoveryRequired,
    Saved, AlreadyConsumed
};
struct Receipt {
    Outcome outcome{Outcome::RejectedEvidence};
    std::uint64_t generation{}; // Nonzero only after verified committed readback.
};

constexpr bool Eligible(const Request &r, const Completion &c, std::uint64_t now) {
    // No fixed assumptions about LE Legacy versus Secure Connections or the
    // presence of an IRK. The private adapter must validate the negotiated key
    // set and reusable key, including local identity/peer binding and lengths.
    // Just Works may be bonded without MITM protection. Never claim otherwise.
    return r.owner.Valid() && r.owner == c.owner && r.user_approved &&
        r.fingerprint_validated && r.enrollment && r.enrollment == c.enrollment &&
        r.profile_version && r.started_ms && r.deadline_ms > r.started_ms &&
        now >= r.started_ms && now < r.deadline_ms &&
        r.previous_generation < std::numeric_limits<std::uint64_t>::max() &&
        r.minimum_key_bytes >= 7 && r.minimum_key_bytes <= 16 &&
        c.key_bytes >= r.minimum_key_bytes && c.key_bytes <= 16 && c.candidate &&
        c.pairing_succeeded && c.bonding_negotiated && c.distribution_complete &&
        c.reusable_encryption_key && c.link_encrypted && !c.debug_key;
}

// One owned enrollment transaction. The owner serializes callbacks and storage;
// this class is not a lock or an IPC authorization boundary. Rejected/unknown
// outcomes never cause another pair, deletion, global reset or automatic retry.
class Transaction {
    bool consumed_{};
public:
    Transaction() = default;
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;
    template<class Store> Receipt Run(const Request &r, const Completion &c,
                                     std::uint64_t now, Store &store) {
        if (consumed_) return {Outcome::AlreadyConsumed, 0};
        consumed_ = true;
        if (!Eligible(r,c,now)) return {};
        const auto generation = r.previous_generation + 1;
        // Begin checks the exact private owner/profile and prior generation.
        // No writes on RejectedWithoutWrites. Unknown must not be guessed away.
        const auto opened = store.Begin(r);
        if (opened == OpenResult::RejectedWithoutWrites) return {Outcome::StoreNotOpened,0};
        if (opened != OpenResult::Opened) return {Outcome::RecoveryRequired,0};
        // Stage copies the candidate in PRIVATE storage, not in the observer,
        // Setup, diagnostics or FTP-visible SD config. VerifyStaged checks full
        // bytes/length/schema and all owner/key/distribution metadata there,
        // including a final live owner/epoch check serialized with commit.
        if (!store.Stage(c,generation) || !store.VerifyStaged(r,c,generation))
            return {store.Abort() ? Outcome::PreviousRecordRetained : Outcome::RecoveryRequired,0};
        const auto committed = store.Commit();
        if (committed == CommitResult::NotCommitted)
            return {store.Abort() ? Outcome::PreviousRecordRetained : Outcome::RecoveryRequired,0};
        // Commit errors may mean the new record reached storage. Do not delete,
        // roll back, retry or reconnect automatically after an ambiguous commit.
        if (committed != CommitResult::Committed) return {Outcome::RecoveryRequired,0};
        if (!store.VerifyCommitted(r,c,generation)) return {Outcome::RecoveryRequired,0};
        return {Outcome::Saved,generation};
    }
};
// A Saved receipt is local persistence only. It is NOT proof the remote kept
// its keys, a native reload succeeded, or an ordinary-mode reconnect works.
// PreviousRecordRetained likewise does NOT mean the remote still accepts it.
}
