// Target-only BTM cache diagnostics; not identity/ownership/admission. GPL-2.0-only.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace amazon_remote {
inline constexpr std::size_t ScanCacheRecordBytes = 328;
inline constexpr std::size_t ScanCacheAddressOffset = 1;
inline constexpr std::size_t ScanCacheCapacity = 10;
struct ScanCacheSummary {
    bool valid{}, at_capacity{};
    std::uint8_t count{}, target_matches{};
    std::size_t first_target_index{ScanCacheCapacity};
};
inline bool ValidCensusTarget(const std::array<std::uint8_t, 6> &target) {
    return !std::all_of(target.begin(), target.end(), [](auto x) { return x == 0; }) &&
        !std::all_of(target.begin(), target.end(), [](auto x) { return x == 0xff; });
}
// A failed IPC, invalid count/extent or invalid target returns no data. Never
// interpret SDK unknown bytes or adopt a cache entry as an owned connection.
inline ScanCacheSummary InspectScanCache(bool success, const void *data, std::size_t size,
    std::uint8_t count, const std::array<std::uint8_t, 6> &target) {
    ScanCacheSummary out{};
    if (!success || count > ScanCacheCapacity || !ValidCensusTarget(target) ||
        size < std::size_t(count) * ScanCacheRecordBytes || (!data && count)) return out;
    out.valid = true; out.count = count; out.at_capacity = count == ScanCacheCapacity;
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::equal(target.begin(), target.end(), bytes + i * ScanCacheRecordBytes + ScanCacheAddressOffset)) continue;
        if (!out.target_matches) out.first_target_index = i;
        ++out.target_matches;
    }
    return out;
}
inline bool CopyFirstTargetScanRecord(bool success, const void *data, std::size_t size,
    std::uint8_t count, const std::array<std::uint8_t, 6> &target,
    std::array<std::uint8_t, ScanCacheRecordBytes> &out) {
    const auto summary = InspectScanCache(success, data, size, count, target);
    if (!summary.valid || !summary.target_matches) return false;
    std::memcpy(out.data(), static_cast<const std::uint8_t *>(data) + summary.first_target_index * ScanCacheRecordBytes, out.size());
    return true;
}
}
