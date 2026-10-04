// Independently implemented bounded advertisement admission. GPL-2.0-only.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace amazon_remote {
using AdvertisementAddress = std::array<std::uint8_t, 6>;
enum class AdvertisementParseStatus { Complete, Empty, InvalidInput, TooLong, Malformed };
struct AdvertisementObservation {
    AdvertisementParseStatus status{AdvertisementParseStatus::Empty};
    std::size_t parsed_elements{};
    std::size_t error_offset{};
    std::uint32_t errors{};
    bool hid_service_16{};
    bool amazon_company{};
    bool captured_manufacturer_prefix{};
    bool Valid() const { return status == AdvertisementParseStatus::Complete || status == AdvertisementParseStatus::Empty; }
};
struct AdvertisementDecision {
    AdvertisementObservation observation{};
    bool known_address{};
    bool advertisement_candidate{};
    // This admits a candidate for further identity reads, never a controller.
    bool eligible_for_identity_reads{};
};
struct ManufacturerPrefixFilter {
    std::uint8_t ad_type{0xff};
    std::array<std::uint8_t, 29> pattern{};
    std::array<std::uint8_t, 29> mask{};
    std::uint8_t payload_size{};
};
// Canonical on-air AD TLVs: [length including type, type, payload]. NOT an
// array of BtdrvBleAdvertisement structs; that SDK's size needs adapter proof.
AdvertisementObservation ParseAdvertisement(const std::uint8_t *data, std::size_t length);
AdvertisementDecision AssessAdvertisement(const std::uint8_t *data, std::size_t length,
    const AdvertisementAddress &event_address, const AdvertisementAddress &configured_address);
// Captured prefix, not a reverse-engineered manufacturer layout or PID proof.
// The driver adapter must explicitly set its proven adv.size convention.
ManufacturerPrefixFilter CapturedManufacturerFilter();
}
