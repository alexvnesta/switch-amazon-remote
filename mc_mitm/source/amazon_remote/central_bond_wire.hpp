// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
namespace amazon_remote::central_bond {
inline constexpr std::uint32_t Magic=0x31425241,Version=1;
inline constexpr std::uint32_t BeginCommand=65010,StatusCommand=65011,CancelCommand=65012;
enum class Store : std::uint32_t { Unknown, Empty, Saved, Error };
enum class Phase : std::uint32_t { Idle, Hello, Encrypting, Saving, Ready, Failed, Cancelled };
struct Begin { std::uint32_t nonce,connection; std::uint64_t owner_epoch; };
// Public metadata only. Neither native event bytes nor keys can fit this API.
struct Status {
    std::uint32_t magic{Magic},version{Version};
    Store store{}; Phase phase{};
    std::uint32_t nonce{},connection{},result{},native_status{255};
    std::uint64_t owner_epoch{},generation{};
    std::uint32_t encrypted{},key_bytes{};
};
static_assert(sizeof(Begin)==16 && sizeof(Status)==56);
// C1 keeps private storage validity separate from native connection readiness.
inline constexpr std::uint32_t PreparePeerCommand=65013,PeerStatusCommand=65014;
inline constexpr std::uint32_t PeerMagic=0x31505241,PeerVersion=1;
enum class PeerPhase : std::uint32_t { Idle, Queued, Waiting, Ready, Failed, Cancelled };
// IPC input must remain a plain trivial aggregate for Stratosphere serialization.
struct PreparePeer { std::uint32_t nonce,reserved;std::uint64_t capture_deadline_ms; };
struct PeerStatus {
    std::uint32_t magic{PeerMagic},version{PeerVersion};
    PeerPhase phase{};std::uint32_t nonce{},result{},existed{255},device_type{255},address_type{255};
    std::uint64_t started_ms{},capture_deadline_ms{},generation{};
};
static_assert(sizeof(PreparePeer)==16 && sizeof(PeerStatus)==56);
// C2 exposes four bounded, target-matched RECEIVED command-status records.
// The window nonce identifies host capture scope, not a native request token.
inline constexpr std::uint32_t HciTraceCommand=65015,HciTraceMagic=0x32485241;
struct HciStatusEntry { std::uint64_t boot_ms{};std::uint32_t status{},own_address_type{}; };
struct HciTrace {
    std::uint32_t magic{HciTraceMagic},version{1},window_nonce{},count{},supported{},dropped{};
    std::uint64_t capture_start_ms{};
    HciStatusEntry entries[4]{};
};
static_assert(sizeof(HciStatusEntry)==16 && sizeof(HciTrace)==96);
#ifdef ARMODULE_WAKE_A3
inline constexpr std::uint32_t WakeCommand=65016,WakeStatusCommand=65017;
struct WakeRequest { std::uint32_t serial,nonce,connection,reserved;std::uint64_t owner_epoch; };
struct WakeStatus {
    std::uint32_t magic{0x33575241},version{1},serial{},ready{},remaining{},nonce{},connection{},reserved{};
    std::uint64_t owner_epoch{};
};
static_assert(sizeof(WakeRequest)==24 && sizeof(WakeStatus)==40);
#endif
}
