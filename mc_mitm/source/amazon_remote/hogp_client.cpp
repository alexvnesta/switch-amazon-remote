// Independently implemented public-API HOGP client prototype. GPL-2.0-only.
#include "hogp_client.hpp"
#include <algorithm>
#include <cstring>

namespace amazon_remote {
namespace {
constexpr std::uint16_t Hid = 0x1812, DeviceInformation = 0x180a, Pnp = 0x2a50;
constexpr std::uint16_t ReportMap = 0x2a4b, ReportUuid = 0x2a4d, Reference = 0x2908, Ccc = 0x2902;
constexpr std::uint8_t Notify = 0x10;
std::uint16_t Read16(const std::uint8_t *p) { return p[0] | (std::uint16_t(p[1]) << 8); }
}
bool SelectFingerprintServices(const Attribute *services, std::size_t count,
                               std::array<Attribute, 2> &out) {
    if (!services || !count || count >= 100) return false;
    std::array<Attribute, 2> selected{};
    for (std::size_t i = 0; i < count; ++i) {
        const auto &a = services[i];
        if (a.uuid != DeviceInformation && a.uuid != Hid) continue;
        const std::size_t index = a.uuid == DeviceInformation ? 0 : 1;
        if (selected[index].handle || a.kind != Kind::Service || !a.primary ||
            !a.handle || a.end < a.handle) return false;
        selected[index] = a;
    }
    if (!selected[0].handle || !selected[1].handle ||
        !(selected[0].end < selected[1].handle || selected[1].end < selected[0].handle)) return false;
    out = selected;
    return true;
}
bool HogpClient::Fail(Error e) { error_ = e; phase_ = Phase::Failed; outstanding_ = false; return false; }
void HogpClient::Disconnect() {
    ++generation_; connection_ = 0; count_ = report_count_ = cursor_ = 0;
    outstanding_ = input_continuation_ = false; phase_ = Phase::Disconnected; error_ = Error::None;
    attributes_.fill({}); reports_.fill({}); hid_ = {}; device_information_ = {}; pnp_ = {}; map_ = {};
}
void HogpClient::Connect(std::uint32_t id) { Disconnect(); connection_ = id; phase_ = Phase::Cache; }
const Attribute *HogpClient::Find(std::uint16_t handle) const {
    for (std::size_t i = 0; i < count_; ++i) if (attributes_[i].handle == handle) return &attributes_[i];
    return nullptr;
}
bool HogpClient::Append(std::uint32_t id, const Attribute *batch, std::size_t n) {
    if (id != connection_ || phase_ != Phase::Cache) return false;
    // Managed BLE event batches have ten slots, independent of aggregate capacity.
    if (n > 10 || (n && !batch)) return Fail(Error::BadBatch);
    if (n > AttributeCapacity - count_) return Fail(Error::Capacity);
    // Validate the WHOLE batch before writing any part of it.
    for (std::size_t i = 0; i < n; ++i) {
        if (!batch[i].handle || static_cast<unsigned>(batch[i].kind) > 3 ||
            (batch[i].kind == Kind::Service && batch[i].end < batch[i].handle)) return Fail(Error::BadAttribute);
        if (Find(batch[i].handle)) return Fail(Error::Duplicate);
        for (std::size_t j = 0; j < i; ++j) if (batch[j].handle == batch[i].handle) return Fail(Error::Duplicate);
    }
    if (n) std::copy(batch, batch + n, attributes_.begin() + count_);
    count_ += n; return true;
}
bool HogpClient::Layout() {
    std::sort(attributes_.begin(), attributes_.begin() + count_, [](const Attribute &a, const Attribute &b) { return a.handle < b.handle; });
    if (identity_only_ && !input_continuation_) {
        // Identity-only discovery needs neither reports nor their descriptors.
        // Keep this separate so legacy navigation layout admission is unchanged.
        std::array<Attribute, 2> required{};
        std::size_t services = 0;
        for (std::size_t i = 0; i < count_; ++i) {
            const auto &a = attributes_[i];
            if (a.kind != Kind::Service || (a.uuid != DeviceInformation && a.uuid != Hid)) continue;
            if (services == required.size()) return Fail(Error::BadLayout);
            required[services++] = a;
        }
        std::array<Attribute, 2> selected{};
        if (!SelectFingerprintServices(required.data(), services, selected)) return Fail(Error::BadLayout);
        device_information_ = selected[0]; hid_ = selected[1];
        for (std::size_t i = 0; i < count_; ++i) {
            const auto &a = attributes_[i];
            if (a.kind != Kind::Characteristic) continue;
            if (a.uuid == Pnp) {
                if (pnp_.handle || a.handle <= device_information_.handle || a.handle > device_information_.end) return Fail(Error::BadLayout);
                pnp_ = a;
            }
            if (a.uuid == ReportMap) {
                if (map_.handle || a.handle <= hid_.handle || a.handle > hid_.end) return Fail(Error::BadLayout);
                map_ = a;
            }
        }
        return pnp_.handle && map_.handle ? true : Fail(Error::BadLayout);
    }
    for (std::size_t i = 0; i < count_; ++i) {
        const auto &a = attributes_[i];
        if (a.kind != Kind::Service) continue;
        if (a.uuid == Hid) { if (hid_.handle) return Fail(Error::BadLayout); hid_ = a; }
        if (a.uuid == DeviceInformation) { if (device_information_.handle) return Fail(Error::BadLayout); device_information_ = a; }
    }
    if (!hid_.handle || !device_information_.handle) return Fail(Error::BadLayout);
    for (std::size_t i = 0; i < count_; ++i) {
        const auto &a = attributes_[i];
        if (a.kind != Kind::Characteristic) continue;
        if (a.handle > device_information_.handle && a.handle <= device_information_.end && a.uuid == Pnp) {
            if (pnp_.handle) return Fail(Error::BadLayout);
            pnp_ = a;
        }
        if (a.handle <= hid_.handle || a.handle > hid_.end) continue;
        if (a.uuid == ReportMap) { if (map_.handle) return Fail(Error::BadLayout); map_ = a; }
        if (a.uuid != ReportUuid) continue;
        if (report_count_ == ReportCapacity) return Fail(Error::Capacity);
        auto &r = reports_[report_count_++]; r.characteristic = a;
        // Descriptors belong to the immediately preceding characteristic.
        for (std::size_t j = i + 1; j < count_ && attributes_[j].handle <= hid_.end; ++j) {
            const auto &d = attributes_[j];
            if (d.kind == Kind::Characteristic || d.kind == Kind::Service) break;
            if (d.kind != Kind::Descriptor) continue;
            if (d.uuid == Reference) { if (r.reference.handle) return Fail(Error::BadLayout); r.reference = d; }
            if (d.uuid == Ccc) { if (r.ccc.handle) return Fail(Error::BadLayout); r.ccc = d; }
        }
        if (!r.reference.handle) return Fail(Error::BadLayout);
    }
    return pnp_.handle && map_.handle && report_count_ ? true : Fail(Error::BadLayout);
}
bool HogpClient::FinishCache(std::uint32_t id) {
    if (id != connection_ || phase_ != Phase::Cache) return false;
    if (!Layout()) return false;
    phase_ = Phase::Identity; return true;
}
bool HogpClient::BeginSecurityRead() {
    if (!identity_only_ || phase_ != Phase::FingerprintVerified || outstanding_ || input_continuation_) return false;
    phase_ = Phase::SecurityRead;
    return true;
}
bool HogpClient::BeginVerifiedInputs(const Attribute *batch, std::size_t n) {
    if (!identity_only_ || phase_ != Phase::SecurityReadVerified || outstanding_ || input_continuation_) return false;
    // At most sixteen report records and two standard descriptors per record.
    if (!batch || !n || n > ReportCapacity * 3 || n > AttributeCapacity-count_) return Fail(Error::BadBatch);
    unsigned reports = 0;
    std::uint16_t previous_handle = hid_.handle;
    std::uint16_t current_characteristic = 0;
    bool reference = false, ccc = false;
    for (std::size_t i = 0; i < n; ++i) {
        const auto &a = batch[i];
        if (a.handle <= previous_handle || a.handle <= hid_.handle || a.handle > hid_.end || Find(a.handle)) return Fail(Error::BadLayout);
        previous_handle = a.handle;
        if (a.kind == Kind::Characteristic && a.uuid == ReportUuid) {
            if (current_characteristic && !reference) return Fail(Error::BadReference);
            // UUID + instance, not an invented handle index, addresses bt reads.
            for (std::size_t j = 0; j < i; ++j)
                if (batch[j].kind == Kind::Characteristic && batch[j].instance == a.instance) return Fail(Error::AmbiguousInput);
            if (++reports > ReportCapacity) return Fail(Error::Capacity);
            current_characteristic = a.handle; reference = ccc = false;
        } else if (a.kind == Kind::Descriptor && current_characteristic && a.instance == 0 && (a.uuid == Reference || a.uuid == Ccc)) {
            bool &seen = a.uuid == Reference ? reference : ccc;
            if (seen) return Fail(Error::BadReference);
            seen = true;
        } else return Fail(Error::BadLayout);
    }
    if (reports < 2 || !reference) return Fail(Error::BadReference);
    std::copy(batch, batch+n, attributes_.begin()+count_); count_ += n;
    input_continuation_ = true;
    hid_ = {}; device_information_ = {}; pnp_ = {}; map_ = {};
    report_count_ = cursor_ = 0; reports_.fill({});
    if (!Layout()) return false;
    phase_ = Phase::References;
    return true;
}
bool HogpClient::Next(Request &r, std::uint64_t now) {
    if (outstanding_ || phase_ == Phase::Failed || phase_ == Phase::Disconnected || phase_ == Phase::Cache || phase_ == Phase::Ready || phase_ == Phase::FingerprintVerified || phase_ == Phase::SecurityReadVerified) return false;
    r = {}; r.connection = connection_; r.generation = generation_; r.operation = Operation::ReadCharacteristic;
    if (phase_ == Phase::Identity) { r.service = device_information_; r.characteristic = pnp_; }
    if (phase_ == Phase::Map) { r.service = hid_; r.characteristic = map_; }
    if (phase_ == Phase::SecurityRead) { r.service = hid_; r.characteristic = map_; r.authentication = Authentication::NoMitm; }
    if (input_continuation_) r.authentication = Authentication::NoMitm;
    if (phase_ == Phase::References) {
        auto &report = reports_[cursor_];
        r.operation = Operation::ReadDescriptor; r.service = hid_; r.characteristic = report.characteristic; r.descriptor = report.reference;
    }
    if (phase_ == Phase::Subscribe) {
        while (cursor_ < report_count_ && !reports_[cursor_].selected) ++cursor_;
        if (cursor_ == report_count_) { phase_ = Phase::Ready; return false; }
        auto &report = reports_[cursor_]; r.service = hid_; r.characteristic = report.characteristic;
        if (!report.registered) r.operation = Operation::RegisterNotification;
        else { r.operation = Operation::WriteDescriptor; r.descriptor = report.ccc; r.value[0] = 1; r.value[1] = 0; r.size = 2; }
    }
    pending_ = r; outstanding_ = true; started_ms_ = now; return true;
}
bool HogpClient::Matches(const Request &r) const {
    const auto same = [](const Attribute &a, const Attribute &b) {
        return a.kind == b.kind && a.handle == b.handle && a.end == b.end && a.uuid == b.uuid &&
            a.properties == b.properties && a.instance == b.instance && a.primary == b.primary;
    };
    return outstanding_ && r.generation == generation_ && r.connection == connection_ &&
        r.operation == pending_.operation && r.authentication == pending_.authentication && r.size == pending_.size && r.value == pending_.value && same(r.service, pending_.service) &&
        same(r.characteristic, pending_.characteristic) && same(r.descriptor, pending_.descriptor);
}
bool HogpClient::SelectInputs() {
    bool keyboard = false, consumer = false;
    for (std::size_t i = 0; i < report_count_; ++i) {
        auto &r = reports_[i];
        if (r.type != 1 || (r.id != 1 && r.id != 2)) continue;
        if (!(r.characteristic.properties & Notify)) return Fail(Error::NotifyUnavailable);
        if (!r.ccc.handle) return Fail(Error::BadReference);
        bool &seen = r.id == 1 ? keyboard : consumer;
        if (seen) return Fail(Error::AmbiguousInput);
        seen = true; r.selected = true;
    }
    // With this exact report map, these are the only subscribed payload lengths (3 and 4).
    // UUID-only driver notifications cannot distinguish extra 3-byte vendor endpoints.
    if (!keyboard || !consumer) return Fail(Error::BadReference);
    return true;
}
bool HogpClient::Complete(const Request &r, bool success, const std::uint8_t *data, std::size_t size) {
    if (!Matches(r)) return false; // Late completion after reconnect must not affect new state.
    outstanding_ = false;
    if (!success) return Fail(Error::OperationFailed);
    if (size && !data) return Fail(Error::UnexpectedResponse);
    if (phase_ == Phase::Identity) {
        // PnP sources 1=Bluetooth SIG, 2=USB IF. Mac reports source2; raw GATT is
        // not captured yet. Both defined sources require exact VID/PID and map.
        if (size != 7 || (data[0] != 1 && data[0] != 2) || Read16(data + 1) != 0x0171 || Read16(data + 3) != 0x0427) return Fail(Error::WrongIdentity);
        phase_ = Phase::Map;
    } else if (phase_ == Phase::Map || phase_ == Phase::SecurityRead) {
        if (!expected_map_ || !expected_map_size_ || size != expected_map_size_ || std::memcmp(data, expected_map_, size)) return Fail(Error::WrongReportMap);
        cursor_ = 0;
        phase_ = phase_ == Phase::SecurityRead ? Phase::SecurityReadVerified : identity_only_ ? Phase::FingerprintVerified : Phase::References;
    } else if (phase_ == Phase::References) {
        if (size != 2 || data[1] < 1 || data[1] > 3) return Fail(Error::BadReference);
        auto &report = reports_[cursor_]; report.id = data[0]; report.type = data[1]; report.reference_read = true;
        if (++cursor_ == report_count_) { if (!SelectInputs()) return false; cursor_ = 0; phase_ = Phase::Subscribe; }
    } else if (phase_ == Phase::Subscribe) {
        auto &report = reports_[cursor_];
        if (r.operation == Operation::RegisterNotification) report.registered = true;
        else { report.ccc_written = true; ++cursor_; }
    } else return Fail(Error::UnexpectedResponse);
    return true;
}
bool HogpClient::Tick(std::uint64_t now) {
    if (outstanding_ && now >= started_ms_ && now - started_ms_ >= TimeoutMilliseconds) return Fail(Error::Timeout);
    return phase_ != Phase::Failed;
}
bool HogpClient::Notification(std::uint32_t id, std::uint16_t service, std::uint16_t characteristic,
                              const std::uint8_t *data, std::size_t size, InputReport &out) const {
    if (phase_ != Phase::Ready || id != connection_ || service != Hid || characteristic != ReportUuid || !data) return false;
    if (size != 3 && size != 4) return false;
    out = {static_cast<std::uint8_t>(size == 3 ? 1 : 2), data, size}; return true;
}
}
