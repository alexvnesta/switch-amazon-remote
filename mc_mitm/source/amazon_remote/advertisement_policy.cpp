// Independently implemented bounded advertisement admission. GPL-2.0-only.
#include "advertisement_policy.hpp"
#include <algorithm>

namespace amazon_remote {
namespace {
constexpr std::array<std::uint8_t, 6> CapturedPrefix{0x71, 0x01, 0x04, 0x27, 0x02, 0x7d};
AdvertisementObservation Fail(AdvertisementObservation value, AdvertisementParseStatus status, std::size_t offset) {
    value.status = status;
    value.error_offset = offset;
    ++value.errors;
    // Invalid records cannot retain an earlier admission hint.
    value.hid_service_16 = value.amazon_company = value.captured_manufacturer_prefix = false;
    return value;
}
bool UsableConfiguredAddress(const AdvertisementAddress &address) {
    return !std::all_of(address.begin(), address.end(), [](auto x) { return x == 0; }) &&
        !std::all_of(address.begin(), address.end(), [](auto x) { return x == 0xff; });
}
}
AdvertisementObservation ParseAdvertisement(const std::uint8_t *data, std::size_t length) {
    AdvertisementObservation result;
    if (length > 31) return Fail(result, AdvertisementParseStatus::TooLong, 31);
    if (length && !data) return Fail(result, AdvertisementParseStatus::InvalidInput, 0);
    if (!length) return result;
    result.status = AdvertisementParseStatus::Complete;
    std::size_t cursor = 0;
    while (cursor < length) {
        const auto start = cursor;
        const std::size_t field_length = data[cursor++];
        if (!field_length) {
            // A terminator followed exclusively by zero padding is valid.
            for (; cursor < length; ++cursor)
                if (data[cursor]) return Fail(result, AdvertisementParseStatus::Malformed, cursor);
            return result;
        }
        if (field_length > length - cursor)
            return Fail(result, AdvertisementParseStatus::Malformed, start);
        const auto type = data[cursor];
        const auto *payload = data + cursor + 1;
        const auto payload_size = field_length - 1;
        if (type == 0x02 || type == 0x03) { // Incomplete/complete 16-bit UUID lists.
            if (payload_size % 2)
                return Fail(result, AdvertisementParseStatus::Malformed, start);
            for (std::size_t i = 0; i < payload_size; i += 2)
                if (payload[i] == 0x12 && payload[i + 1] == 0x18) result.hid_service_16 = true;
        } else if (type == 0xff) {
            if (payload_size < 2)
                return Fail(result, AdvertisementParseStatus::Malformed, start);
            if (payload[0] == 0x71 && payload[1] == 0x01) result.amazon_company = true;
            if (payload_size >= CapturedPrefix.size() &&
                std::equal(CapturedPrefix.begin(), CapturedPrefix.end(), payload))
                result.captured_manufacturer_prefix = true;
        }
        // Unknown ADs, including all names, have no admission meaning.
        ++result.parsed_elements;
        cursor += field_length;
    }
    return result;
}
AdvertisementDecision AssessAdvertisement(const std::uint8_t *data, std::size_t length,
        const AdvertisementAddress &event_address, const AdvertisementAddress &configured_address) {
    AdvertisementDecision result;
    result.observation = ParseAdvertisement(data, length);
    result.known_address = UsableConfiguredAddress(configured_address) && event_address == configured_address;
    result.advertisement_candidate = result.observation.Valid() && result.observation.hid_service_16 &&
        result.observation.captured_manufacturer_prefix;
    result.eligible_for_identity_reads = result.known_address && result.advertisement_candidate;
    return result;
}
ManufacturerPrefixFilter CapturedManufacturerFilter() {
    ManufacturerPrefixFilter result;
    std::copy(CapturedPrefix.begin(), CapturedPrefix.end(), result.pattern.begin());
    std::fill_n(result.mask.begin(), CapturedPrefix.size(), 0xff);
    result.payload_size = CapturedPrefix.size();
    return result;
}
}
