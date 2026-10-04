// GPL-2.0-only.
#include "../mc_mitm/source/amazon_remote/scan_cache_census.hpp"
#include <cassert>
#include <cstdio>
using namespace amazon_remote;
int main() {
    const std::array<std::uint8_t, 6> target{0x02,0xab,0xcd,0x12,0x34,0x56};
    std::array<std::uint8_t, ScanCacheCapacity * ScanCacheRecordBytes> bytes{};
    std::array<std::uint8_t, ScanCacheRecordBytes> out{}; out.fill(0xcc);
    auto summary = InspectScanCache(true, nullptr, 0, 0, target); assert(summary.valid && !summary.target_matches);
    assert(!InspectScanCache(true, nullptr, sizeof bytes, 1, target).valid);
    assert(!InspectScanCache(false, bytes.data(), bytes.size(), 10, target).valid);
    std::array<std::uint8_t, 6> zero{}, broadcast{}; broadcast.fill(0xff);
    assert(!InspectScanCache(true, bytes.data(), bytes.size(), 10, zero).valid);
    assert(!InspectScanCache(true, bytes.data(), bytes.size(), 10, broadcast).valid);
    for (unsigned n = 0; n <= 255; ++n) {
        summary = InspectScanCache(true, bytes.data(), bytes.size(), n, target);
        assert(summary.valid == (n <= 10));
        if (summary.valid) assert(summary.count == n && summary.at_capacity == (n == 10) && !summary.target_matches);
    }
    std::copy(target.begin(), target.end(), bytes.begin() + ScanCacheRecordBytes * 3 + ScanCacheAddressOffset);
    bytes[3 * ScanCacheRecordBytes] = 0xff; // Unknown byte0 never gate/interpret.
    const auto original = bytes;
    summary = InspectScanCache(true, bytes.data(), bytes.size(), 10, target);
    assert(summary.valid && summary.target_matches == 1 && summary.first_target_index == 3);
    assert(CopyFirstTargetScanRecord(true, bytes.data(), bytes.size(), 10, target, out));
    assert(out[0] == 0xff && std::equal(target.begin(), target.end(), out.begin() + ScanCacheAddressOffset));
    assert(bytes == original);
    const auto expected = out;
    // Every foreign record byte can change without changing target-only output.
    for (unsigned i = 0; i < bytes.size(); ++i) if (i < 3 * ScanCacheRecordBytes || i >= 4 * ScanCacheRecordBytes) bytes[i] ^= 0x55;
    assert(CopyFirstTargetScanRecord(true, bytes.data(), bytes.size(), 10, target, out) && out == expected);
    for (unsigned size = 0; size < bytes.size(); ++size)
        assert(!InspectScanCache(true, bytes.data(), size, 10, target).valid);
    const auto before = out;
    assert(!CopyFirstTargetScanRecord(true, bytes.data(), bytes.size(), 3, target, out) && out == before);
    assert(!CopyFirstTargetScanRecord(false, bytes.data(), bytes.size(), 10, target, out) && out == before);
    auto wrong = target; wrong[5] ^= 1;
    assert(!InspectScanCache(true, bytes.data(), bytes.size(), 10, wrong).target_matches);
    auto reversed = target; std::reverse(reversed.begin(), reversed.end());
    assert(!InspectScanCache(true, bytes.data(), bytes.size(), 10, reversed).target_matches);
    std::copy(target.begin(), target.end(), bytes.begin() + 5 * ScanCacheRecordBytes + ScanCacheAddressOffset);
    summary = InspectScanCache(true, bytes.data(), bytes.size(), 10, target);
    assert(summary.target_matches == 2 && summary.first_target_index == 3);
    std::puts("PASS target-only cache census, counts0..255, all truncations, immutable source, foreign exclusion, exact/reversed/mismatching address and duplicate targets");
}
