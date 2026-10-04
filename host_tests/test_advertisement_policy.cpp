// Independently implemented host tests. GPL-2.0-only.
#include "../mc_mitm/source/amazon_remote/advertisement_policy.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace amazon_remote;
int main() {
    // Synthesized canonical TLV envelope around actual CoreBluetooth fields.
    // Captured raw radio TLV layout is NOT available; do not claim otherwise.
    const std::vector<std::uint8_t> base{3,3,0x12,0x18,10,0xff,0x71,1,4,0x27,2,0x7d,0x9f,0x60,0x15};
    const AdvertisementAddress target{0x02,0xab,0xcd,0x12,0x34,0x56};
    AdvertisementAddress stranger{0x02,0xab,0xcd,0x12,0x34,0x57};
    auto decision = AssessAdvertisement(base.data(), base.size(), target, target);
    assert(decision.observation.Valid() && decision.observation.parsed_elements == 2);
    assert(decision.observation.amazon_company && decision.advertisement_candidate && decision.eligible_for_identity_reads);
    assert(!AssessAdvertisement(base.data(), base.size(), stranger, target).eligible_for_identity_reads);
    auto reversed = target; std::reverse(reversed.begin(), reversed.end());
    assert(!AssessAdvertisement(base.data(), base.size(), reversed, target).known_address);
    AdvertisementAddress zero{}, broadcast{}; broadcast.fill(0xff);
    assert(!AssessAdvertisement(base.data(), base.size(), zero, zero).known_address);
    assert(!AssessAdvertisement(base.data(), base.size(), broadcast, broadcast).known_address);
    for (auto tail : {std::array<std::uint8_t,3>{0x9f,0x60,0x15}, {0,0,0}, {0x9f,0x60,0x16}}) {
        auto sample = base; std::copy(tail.begin(), tail.end(), sample.begin() + 12);
        assert(AssessAdvertisement(sample.data(), sample.size(), target, target).eligible_for_identity_reads);
    }
    auto padded = base; padded.resize(31, 0);
    assert(ParseAdvertisement(padded.data(), padded.size()).Valid());
    padded[30] = 1;
    assert(!AssessAdvertisement(padded.data(), padded.size(), target, target).eligible_for_identity_reads);
    assert(ParseAdvertisement(nullptr, 0).status == AdvertisementParseStatus::Empty);
    assert(ParseAdvertisement(nullptr, 1).status == AdvertisementParseStatus::InvalidInput);
    padded.resize(32,0); assert(ParseAdvertisement(padded.data(), 32).status == AdvertisementParseStatus::TooLong);
    const std::uint8_t unknown[]{2,0x77,0xa0,3,9,'A','R'};
    auto names = AssessAdvertisement(unknown,sizeof(unknown),target,target);
    assert(names.observation.Valid() && names.observation.parsed_elements == 2 && !names.advertisement_candidate);
    const std::uint8_t odd_uuid[]{2,3,0x12};
    assert(!ParseAdvertisement(odd_uuid,sizeof(odd_uuid)).Valid());
    const std::uint8_t short_manufacturer[]{2,0xff,0x71};
    assert(!ParseAdvertisement(short_manufacturer,sizeof(short_manufacturer)).Valid());
    auto wrong_prefix = base; wrong_prefix[9] ^= 1;
    assert(ParseAdvertisement(wrong_prefix.data(),wrong_prefix.size()).amazon_company);
    assert(!AssessAdvertisement(wrong_prefix.data(),wrong_prefix.size(),target,target).advertisement_candidate);
    auto incomplete = base; incomplete[1] = 2;
    assert(ParseAdvertisement(incomplete.data(),incomplete.size()).hid_service_16);
    auto multiuuid = base; multiuuid.insert(multiuuid.begin()+4,{0x0f,0x18}); multiuuid[0] = 5;
    assert(ParseAdvertisement(multiuuid.data(),multiuuid.size()).hid_service_16);
    for (std::size_t size = 0; size < base.size(); ++size) {
        auto truncated = ParseAdvertisement(base.data(),size);
        assert(truncated.Valid() == (size == 0 || size == 4));
        if (!truncated.Valid()) assert(truncated.errors == 1 && !truncated.captured_manufacturer_prefix);
    }
    for (unsigned length = 1; length <= 255; ++length) {
        std::array<std::uint8_t,31> raw{}; raw[0] = length; raw[1] = 0x77;
        auto observation = ParseAdvertisement(raw.data(),raw.size());
        assert(observation.Valid() == (length <= 30));
    }
    auto filter = CapturedManufacturerFilter();
    assert(filter.ad_type == 0xff && filter.payload_size == 6);
    for (std::size_t i = 0; i < 29; ++i) {
        assert(filter.mask[i] == (i < 6 ? 0xff : 0));
        assert(filter.pattern[i] == (i < 6 ? base[6+i] : 0));
    }
    // Deterministic malformed/bounded stress, no external/device input required.
    std::uint32_t rng = 0x2741;
    for (unsigned iteration = 0; iteration < 20000; ++iteration) {
        std::array<std::uint8_t,31> raw{};
        for (auto &x : raw) { rng = rng * 1664525u + 1013904223u; x = rng >> 24; }
        auto observation = ParseAdvertisement(raw.data(),iteration%32);
        assert(observation.errors <= 1 && observation.parsed_elements <= 15);
        auto assessed = AssessAdvertisement(raw.data(),iteration%32,target,target);
        if (!observation.Valid()) assert(!assessed.eligible_for_identity_reads);
    }
    std::puts("PASS canonical AD bounds, HID16/company/prefix, strict known-address admission, padding, truncation, 20000 stress inputs");
}
