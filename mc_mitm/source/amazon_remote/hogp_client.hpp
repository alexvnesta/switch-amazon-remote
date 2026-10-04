// Independently implemented public-API HOGP client prototype. GPL-2.0-only.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace amazon_remote {

enum class Kind : std::uint16_t { Included = 0, Characteristic = 1, Descriptor = 2, Service = 3 };
struct Attribute {
    Kind kind{};
    std::uint16_t handle{}, end{}, uuid{};
    std::uint8_t properties{}, instance{};
    bool primary{};
};
// Select only the two required primary services from a bounded discovery list.
// Unrelated records are ignored without reinterpreting their kind or range.
// Output is [Device Information, HID] and remains unchanged on failure.
bool SelectFingerprintServices(const Attribute *services, std::size_t count,
                               std::array<Attribute, 2> &out);
enum class Error {
    None, BadBatch, Capacity, Duplicate, BadAttribute, WrongConnection,
    BadLayout, WrongIdentity, WrongReportMap, BadReference, AmbiguousInput,
    OperationFailed, UnexpectedResponse, Timeout, BadNotification, NotifyUnavailable
};
enum class Phase { Disconnected, Cache, Identity, Map, References, Subscribe, Ready, Failed, FingerprintVerified, SecurityRead, SecurityReadVerified };
enum class Operation { ReadCharacteristic, ReadDescriptor, RegisterNotification, WriteDescriptor };
enum class Authentication : std::uint8_t { None = 0, NoMitm = 1 };
struct Request {
    Operation operation{};
    Authentication authentication{};
    std::uint32_t connection{}, generation{};
    Attribute service{}, characteristic{}, descriptor{};
    std::array<std::uint8_t, 2> value{};
    std::size_t size{};
};
// Defense-in-depth whitelist for identity-only transports. This does not prove
// request ownership; the caller must still validate its connection/generation.
inline bool IsFingerprintRead(const Request &request) {
    return request.operation == Operation::ReadCharacteristic &&
        ((request.service.uuid == 0x180a && request.characteristic.uuid == 0x2a50) ||
         (request.service.uuid == 0x1812 && request.characteristic.uuid == 0x2a4b));
}
struct InputReport {
    std::uint8_t id{};
    const std::uint8_t *payload{}; // GATT payload excludes report ID.
    std::size_t size{};
};

class HogpClient {
public:
    static constexpr std::size_t AttributeCapacity = 256;
    static constexpr std::size_t ReportCapacity = 16;
    static constexpr std::uint64_t TimeoutMilliseconds = 10000;
    // expected_map must remain valid for this client's lifetime.
    // Identity-only mode reads PnP and the exact report map, then stops. It never
    // authorizes input, subscriptions, report-reference reads, or descriptor writes.
    HogpClient(const std::uint8_t *expected_map, std::size_t map_size, bool identity_only = false)
        : expected_map_(expected_map), expected_map_size_(map_size), identity_only_(identity_only) {}
    void Connect(std::uint32_t connection);
    void Disconnect();
    bool Append(std::uint32_t connection, const Attribute *batch, std::size_t count);
    bool FinishCache(std::uint32_t connection);
    // Explicit continuation, never enabled by identity-only Next/Complete alone.
    // A successful NoMitm-requested read is NOT proof of encryption or a bond.
    bool BeginSecurityRead();
    // Append only bounded HID Report characteristics plus their standard
    // Reference/CCC descriptors, retaining this connection and generation.
    bool BeginVerifiedInputs(const Attribute *reports, std::size_t count);
    // One outstanding operation at a time. Never block the BLE event thread.
    bool Next(Request &request, std::uint64_t now_ms);
    bool Complete(const Request &request, bool success, const std::uint8_t *data, std::size_t size);
    bool Tick(std::uint64_t now_ms);
    bool Notification(std::uint32_t connection, std::uint16_t service_uuid,
                      std::uint16_t characteristic_uuid, const std::uint8_t *data,
                      std::size_t size, InputReport &out) const;
    Phase GetPhase() const { return phase_; }
    Error GetError() const { return error_; }
    std::size_t Count() const { return count_; }
    std::uint32_t Generation() const { return generation_; }
    const Attribute *Find(std::uint16_t handle) const;
    const Attribute &HidService() const { return hid_; }
private:
    struct Report {
        Attribute characteristic{}, reference{}, ccc{};
        std::uint8_t id{}, type{};
        bool reference_read{}, selected{}, registered{}, ccc_written{};
    };
    bool Fail(Error error);
    bool Layout();
    bool SelectInputs();
    bool Matches(const Request &request) const;
    std::array<Attribute, AttributeCapacity> attributes_{};
    std::array<Report, ReportCapacity> reports_{};
    std::size_t count_{}, report_count_{}, cursor_{};
    Attribute hid_{}, device_information_{}, pnp_{}, map_{};
    std::uint32_t connection_{}, generation_{};
    Phase phase_ = Phase::Disconnected;
    Error error_ = Error::None;
    Request pending_{};
    bool outstanding_{};
    std::uint64_t started_ms_{};
    const std::uint8_t *expected_map_;
    std::size_t expected_map_size_;
    const bool identity_only_;
    bool input_continuation_{};
};
}
