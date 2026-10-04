// Independent target-only passive sighting primitive. GPL-2.0-only.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace amazon_remote {
using SightingAddress = std::array<std::uint8_t, 6>;
inline constexpr std::uint32_t SightingMagic = 0x41525347u, SightingVersion = 1;
struct SightingWire {
    std::uint32_t magic{}, version{};
    std::uint64_t captured_boot_ms{};
    std::array<std::uint8_t, 328> raw{};
};
static_assert(sizeof(SightingWire) == 344 && offsetof(SightingWire, raw) == 16);
static_assert(std::is_trivially_copyable_v<SightingWire> && std::is_standard_layout_v<SightingWire>);
enum class SightingAssessment { Invalid, Stale, Future, NotCandidate, FreshCandidate };
namespace sighting_detail {
inline bool Usable(const SightingAddress &a) {
    return !std::all_of(a.begin(),a.end(),[](auto b) { return b == 0; }) &&
        !std::all_of(a.begin(),a.end(),[](auto b) { return b == 0xff; });
}
inline std::uint32_t U32(const std::uint8_t *p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
inline bool TargetView(const std::uint8_t *raw, const SightingAddress &target) {
    return Usable(target) && U32(raw) == 0 && raw[4] == 2 && raw[5] <= 2 && raw[6] <= 1 &&
        std::equal(target.begin(),target.end(),raw+7) && raw[323] <= 10;
}
}
// FreshCandidate is diagnostic corroboration ONLY. It never proves remote
// identity, pairing, callback-to-client correlation, or connection ownership.
inline SightingAssessment AssessSighting(const SightingWire &wire, const SightingAddress &target,
        std::uint64_t minimum_boot_ms, std::uint64_t now_boot_ms) {
    if (wire.magic != SightingMagic || wire.version != SightingVersion || !wire.captured_boot_ms ||
        !sighting_detail::TargetView(wire.raw.data(),target)) return SightingAssessment::Invalid;
    if (now_boot_ms < minimum_boot_ms || wire.captured_boot_ms > now_boot_ms) return SightingAssessment::Future;
    if (wire.captured_boot_ms < minimum_boot_ms || now_boot_ms-wire.captured_boot_ms > 3000) return SightingAssessment::Stale;
    bool hid = false, manufacturer = false;
    constexpr std::array<std::uint8_t,6> prefix{0x71,0x01,0x04,0x27,0x02,0x7d};
    for (std::size_t i = 0; i < wire.raw[323]; ++i) {
        const auto *ad = wire.raw.data()+13+i*31;
        // SDK size convention is unverified. Under the conservative on-air
        // interpretation it includes type. Never read beyond size-1 or29.
        if (!ad[0] || ad[0] > 30) return SightingAssessment::Invalid;
        const std::size_t payload_size = std::min<std::size_t>(ad[0]-1,29);
        const auto *payload = ad+2;
        if (ad[1] == 2 || ad[1] == 3) {
            if (payload_size < 2 || payload_size%2) return SightingAssessment::Invalid;
            for (std::size_t j = 0; j < payload_size; j += 2)
                if (payload[j] == 0x12 && payload[j+1] == 0x18) hid = true;
        } else if (ad[1] == 0xff) {
            if (payload_size < 2) return SightingAssessment::Invalid;
            if (payload_size >= prefix.size() && std::equal(prefix.begin(),prefix.end(),payload)) manufacturer = true;
        }
    }
    return hid && manufacturer ? SightingAssessment::FreshCandidate : SightingAssessment::NotCandidate;
}
class TargetSightingSnapshot {
public:
    // Nonzero even initial sequence is solely a wrap-boundary host-test seam.
    explicit TargetSightingSnapshot(std::uint32_t initial_sequence = 0) : sequence_(initial_sequence & ~1u), writer_sequence_(initial_sequence & ~1u) {
        for (auto &word : words_) word.store(0,std::memory_order_seq_cst);
    }
    // Configure once before the sole BLE producer starts; no concurrent config.
    bool Configure(const SightingAddress &target) {
        if (configured_.load(std::memory_order_acquire) || observed_.load(std::memory_order_acquire) || !sighting_detail::Usable(target)) return false;
        target_ = target; configured_.store(true,std::memory_order_release); return true;
    }
    // Sole controlling thread. Changing enable state advances a generation;
    // old/in-flight publications cannot become valid after disabling/rearming.
    void SetEnabled(bool enabled) {
        controller_generation_ += 2;
        enabled_generation_.store(controller_generation_ | std::uint32_t(enabled),std::memory_order_seq_cst);
    }
    // Sole producer. Raw SDK scan view only; preserves all328bytes untouched.
    bool Observe(std::uint32_t type, const std::uint8_t *data, std::size_t size, std::uint64_t boot_ms) {
        observed_.store(true,std::memory_order_relaxed);
        const auto generation = enabled_generation_.load(std::memory_order_seq_cst);
        if (!(generation & 1) || !configured_.load(std::memory_order_acquire) || type != 6 || !data || size < 328 ||
            !boot_ms || !sighting_detail::TargetView(data,target_)) return false;
        SightingWire wire{}; wire.magic = SightingMagic; wire.version = SightingVersion; wire.captured_boot_ms = boot_ms;
        std::copy_n(data,328,wire.raw.begin());
        if (enabled_generation_.load(std::memory_order_seq_cst) != generation) return false;
        sequence_.store(writer_sequence_+1,std::memory_order_seq_cst);
        for (std::size_t i = 0; i < WireWords; ++i) {
            std::uint32_t word{}; std::memcpy(&word,reinterpret_cast<const std::uint8_t *>(&wire)+i*4,4);
            words_[i].store(word,std::memory_order_seq_cst);
        }
        words_[WireWords].store(generation,std::memory_order_seq_cst);
        writer_sequence_ += 2; sequence_.store(writer_sequence_,std::memory_order_seq_cst);
        return enabled_generation_.load(std::memory_order_seq_cst) == generation;
    }
    // Any reader, two finite attempts. All shared payload words are atomic:
    // an unsuccessful recheck is never an excuse for a C++ non-atomic race.
    // Output remains unchanged on failure. No waits, heap, locks or IPC here.
    bool Read(SightingWire &out) const {
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
            const auto generation = enabled_generation_.load(std::memory_order_seq_cst);
            if (!(generation & 1)) return false;
            const auto before = sequence_.load(std::memory_order_seq_cst);
            if (before & 1) continue;
            SightingWire candidate{};
            for (std::size_t i = 0; i < WireWords; ++i) {
                const auto word = words_[i].load(std::memory_order_seq_cst);
                std::memcpy(reinterpret_cast<std::uint8_t *>(&candidate)+i*4,&word,4);
            }
            const auto published_generation = words_[WireWords].load(std::memory_order_seq_cst);
            const auto after = sequence_.load(std::memory_order_seq_cst);
            if (before != after || (after & 1) || generation != published_generation ||
                enabled_generation_.load(std::memory_order_seq_cst) != generation) continue;
            if (candidate.magic != SightingMagic || candidate.version != SightingVersion) return false;
            out = candidate; return true;
        }
        return false;
    }
private:
    static constexpr std::size_t WireWords = sizeof(SightingWire)/4;
    SightingAddress target_{};
    std::atomic<bool> configured_{false}, observed_{false};
    std::atomic<std::uint32_t> enabled_generation_{0}, sequence_{0};
    std::uint32_t controller_generation_{0}, writer_sequence_{0};
    std::array<std::atomic<std::uint32_t>,WireWords+1> words_{};
};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free && std::atomic<bool>::is_always_lock_free);
}
