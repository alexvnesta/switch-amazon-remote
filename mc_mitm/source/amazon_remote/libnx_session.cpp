// Independently implemented public libnx bt/btmu integration. GPL-2.0-only.
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "libnx_session.hpp"
#include "cached_characteristic_getter.hpp"
#include "k7q3m7_descriptor.hpp"
#include "diagnostic_log.hpp"
#include "ble_preflight.hpp"
#include "scan_cache_census.hpp"
#include "input_diagnostic_policy.hpp"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace amazon_remote {
namespace {
// btGetLeEventInfo's public user payload and managed notify view share this
// prefix. Compile-time guards prevent silently reusing it after SDK ABI drift.
using NotifyEvent = decltype(BtdrvBleEventInfo{}.client_notify);
static_assert(sizeof(BtdrvBleEventInfo) == sizeof(BtdrvLeEventInfo));
static_assert(offsetof(NotifyEvent, result) == offsetof(BtdrvLeEventInfo, unk_x0));
static_assert(offsetof(NotifyEvent, conn_id) == offsetof(BtdrvLeEventInfo, unk_x4));
static_assert(offsetof(NotifyEvent, type) == offsetof(BtdrvLeEventInfo, unk_x8));
static_assert(offsetof(NotifyEvent, serv_uuid) == offsetof(BtdrvLeEventInfo, uuid0));
static_assert(offsetof(NotifyEvent, char_uuid) == offsetof(BtdrvLeEventInfo, uuid1));
static_assert(offsetof(NotifyEvent, desc_uuid) == offsetof(BtdrvLeEventInfo, uuid2));
static_assert(offsetof(NotifyEvent, size) == offsetof(BtdrvLeEventInfo, size));
static_assert(offsetof(NotifyEvent, data) == offsetof(BtdrvLeEventInfo, data));
static_assert(BtdrvBleEventType_ClientNotify == 8);
static_assert(sizeof(BtdrvBleScanResult) == ScanCacheRecordBytes);
static_assert(offsetof(BtdrvBleScanResult, addr) == ScanCacheAddressOffset);
static_assert(sizeof(BtdrvAddress) == 6);
static_assert(sizeof(BtmGattCharacteristic) == 0x24 && offsetof(BtmGattCharacteristic, properties) == 0x1e);
static_assert(sizeof(BtmGattDescriptor) == 0x20);
static_assert(static_cast<unsigned>(Authentication::NoMitm) == BtdrvGattAuthReqType_NoMitm);
Result Invalid() { return MAKERESULT(Module_Libnx, LibnxError_BadInput); }
BtdrvGattAttributeUuid Uuid(std::uint16_t value) {
    BtdrvGattAttributeUuid uuid{}; uuid.size = 2; uuid.uuid[0] = value & 0xff; uuid.uuid[1] = value >> 8; return uuid;
}
std::uint16_t ShortUuid(const BtdrvGattAttributeUuid &uuid) {
    return uuid.size == 2 ? static_cast<std::uint16_t>(uuid.uuid[0] | (std::uint16_t(uuid.uuid[1]) << 8)) : 0;
}
BtdrvGattId Id(const Attribute &a) { BtdrvGattId id{}; id.instance_id = a.instance; id.uuid = Uuid(a.uuid); return id; }
bool Timeout(Result result) { return result == MAKERESULT(Module_Kernel, KernelError_TimedOut); }
std::array<std::uint8_t, 6> CensusTarget(BtdrvAddress target) {
    std::array<std::uint8_t, 6> out{}; std::copy(std::begin(target.address), std::end(target.address), out.begin()); return out;
}
void CensusLog(Logger logger, const char *name, Result rc, std::uint8_t count, const ScanCacheSummary &summary) {
    if (!logger) return;
    char message[256]{};
    std::snprintf(message, sizeof message,
        "cache=%s API_Result=%08lx count=%u valid=%u capacity10=%u exact_target_matches=%u; cache_is_not_identity_ownership_or_admission=1",
        name, static_cast<unsigned long>(rc), count, summary.valid, summary.at_capacity, summary.target_matches);
    logger(message);
}
void CensusTargetLog(Logger logger, const char *name, const void *data, std::size_t size, std::uint8_t count,
    const std::array<std::uint8_t, 6> &target) {
    if (!logger) return;
    std::array<std::uint8_t, ScanCacheRecordBytes> record{};
    if (!CopyFirstTargetScanRecord(true, data, size, count, target, record)) return;
    for (std::size_t offset = 0; offset < record.size(); offset += 32) {
        char hex[65]{}, message[160]{};
        FormatHex(hex, sizeof hex, record.data() + offset, std::min<std::size_t>(32, record.size() - offset));
        std::snprintf(message, sizeof message, "cache=%s exact_target_raw offset=%zu bytes=%s unknown_fields_uninterpreted=1", name, offset, hex);
        logger(message);
    }
}
}
Result ReadOnlyScanCacheCensus(BtdrvAddress target, Logger logger) {
    const auto target_bytes = CensusTarget(target);
    if (!ValidCensusTarget(target_bytes)) return Invalid();
    auto rc = btmuInitialize();
    if (logger) {
        char message[160]{};
        std::snprintf(message, sizeof message, "Read-only cache census btmuInitialize Result=%08lx; getters only, no scan/connect/bond/data-path registration", static_cast<unsigned long>(rc));
        logger(message);
    }
    if (R_FAILED(rc)) return rc;
    Result first_error = 0;
    std::array<BtdrvBleScanResult, ScanCacheCapacity> results{};
    for (const bool smart : {false, true}) {
        results.fill({}); u8 count = 0;
        rc = smart ? btmuGetBleScanResultsForSmartDevice(results.data(), results.size(), &count)
            : btmuGetBleScanResultsForGeneral(results.data(), results.size(), &count);
        const auto summary = InspectScanCache(R_SUCCEEDED(rc), results.data(), sizeof results, count, target_bytes);
        const char *name = smart ? "smart" : "general";
        CensusLog(logger, name, rc, count, summary);
        if (summary.valid && summary.target_matches) CensusTargetLog(logger, name, results.data(), sizeof results, count, target_bytes);
        if (!first_error && (R_FAILED(rc) || !summary.valid)) first_error = R_FAILED(rc) ? rc : Invalid();
    }
    btmuExit();
    if (logger) logger("Read-only cache census finished; no scan was started/stopped, no cache candidate adopted. Empty user buckets do not prove no over-air target.");
    return first_error;
}
LibnxSession::LibnxSession(Logger logger, bool fingerprint_only, bool input_diagnostic)
    : client_(K7q3m7Descriptor, sizeof K7q3m7Descriptor, fingerprint_only), logger_(logger), identity_test_(fingerprint_only), input_diagnostic_(fingerprint_only && input_diagnostic) {}
void LibnxSession::Log(const char *format, ...) {
    if (!logger_) return;
    char message[256]; va_list args; va_start(args, format); std::vsnprintf(message, sizeof message, format, args); va_end(args); logger_(message);
}
Result LibnxSession::Initialize(BtdrvAddress target) {
    if (initialized_) return Invalid();
    target_ = target;
    const auto trace = [&](const char *api, Result result) {
        char message[256]{};
        FormatApiResult(message, sizeof message, api, result, unsigned(GetPhase()), unsigned(GetError()));
        Log("%s", message);
    };
    Result rc = btdevInitialize(); trace("btdevInitialize", rc); if (R_FAILED(rc)) return rc;
    initialized_ = btdev_initialized_ = true;
    rc = btdevAcquireBleGattOperationEvent(&operation_event_);
    trace("btdevAcquireBleGattOperationEvent", rc);
    if (R_FAILED(rc)) { Close(); return rc; }
    event_open_ = true;
    rc = btdevAcquireBleServiceDiscoveryEvent(&discovery_event_);
    trace("btdevAcquireBleServiceDiscoveryEvent", rc);
    if (R_FAILED(rc)) { Close(); return rc; }
    discovery_open_ = true;
    const auto hid = Uuid(0x1812), information = Uuid(0x180a);
    rc = btdevRegisterGattOperationNotification(&hid);
    trace("btdevRegisterGattOperationNotification(HID1812)", rc);
    if (R_SUCCEEDED(rc)) {
        hid_path_ = true; rc = btdevRegisterGattOperationNotification(&information);
        trace("btdevRegisterGattOperationNotification(DI180A)", rc);
    }
    if (R_SUCCEEDED(rc)) information_path_ = true;
    if (R_FAILED(rc)) { Close(); return rc; }
    if (!decoder_.Configure(K7q3m7Descriptor, sizeof K7q3m7Descriptor)) { Close(); return Invalid(); }
    Log("btdev initialized; driver/system initialization untouched"); return 0;
}
Result LibnxSession::InitializeLinkOnly(BtdrvAddress target) {
    if (identity_test_ || initialized_ || link_attempted_ || scanning_ || !ValidCensusTarget(CensusTarget(target))) return Invalid();
    const auto rc = btmuInitialize();
    Log("API=btmuInitialize(link-only) Result=%08lx", static_cast<unsigned long>(rc));
    if (R_FAILED(rc)) return rc;
    initialized_ = link_only_ = true; target_ = target;
    Log("LINK_ONLY initialization: no bt service, GATT paths/events, pairing events, MTU, reads, subscriptions, auth or output");
    return 0;
}
Result LibnxSession::InitializeIdentityTest(BtdrvAddress target) {
    if (!identity_test_ || initialized_ || link_attempted_ || scanning_ || !ValidCensusTarget(CensusTarget(target))) return Invalid();
    const auto rc = Initialize(target);
    if (R_FAILED(rc)) return rc;
    link_only_ = true;
    if (input_diagnostic_) {
        auto pairing_rc = btdevAcquireBlePairingEvent(&pairing_event_);
        Log("API=btdevAcquireBlePairingEvent(input diagnostic) Result=%08lx; generic hints are not target/bond proof", static_cast<unsigned long>(pairing_rc));
        if (R_FAILED(pairing_rc)) { Close(); return pairing_rc; }
        pairing_event_open_ = true;
        Log("INPUT_DIAGNOSTIC: exact fingerprint first; one NoMitm-requested map read; bounded ReportReference/CCC-only continuation. No explicit bond/PIN/SSP replies, vendor writes or controller output.");
    } else Log("FINGERPRINT_ONLY user GATT paths registered; only PnP/report-map reads AuthNone allowed; no pairing, CCC writes, notifications or output");
    return 0;
}
Result LibnxSession::StartLinkOnlyScan(std::uint64_t now) {
    if (!initialized_ || !link_only_ || link_attempted_ || scanning_ || !now) return Invalid();
    auto rc = btmuAcquireBleConnectionEvent(&connection_event_);
    Log("API=btmuAcquireBleConnectionEvent(link-only) Result=%08lx", static_cast<unsigned long>(rc));
    if (R_FAILED(rc)) return rc;
    connection_event_open_ = true;
    rc = btmuAcquireBleScanEvent(&scan_event_);
    Log("API=btmuAcquireBleScanEvent(link-only) Result=%08lx", static_cast<unsigned long>(rc));
    if (R_FAILED(rc)) return rc;
    scan_event_open_ = true;
    BtdrvGattAttributeUuid uuid{}; uuid.size = HidServiceUuid128.size();
    std::copy(HidServiceUuid128.begin(), HidServiceUuid128.end(), uuid.uuid);
    rc = btmuStartBleScanForSmartDevice(&uuid);
    Log("API=btmuStartBleScanForSmartDevice(HID128 link-only) Result=%08lx; HID16 matching remains unverified", static_cast<unsigned long>(rc));
    if (R_FAILED(rc)) return rc;
    scanning_ = true; scan_started_ms_ = armTicksToNs(armGetSystemTick()) / 1000000;
    last_scan_poll_ms_ = last_connection_poll_ms_ = 0;
    general_cache_seen_ = smart_cache_seen_ = general_target_dumped_ = smart_target_dumped_ = false;
    Log("A starts scan only; physical ZL plus fresh target-only managed sighting required for one link request; no cache auto-connect");
    return 0;
}
Result LibnxSession::ConfirmObservedLink(const SightingWire &sighting, std::uint64_t now, std::uint64_t capture_deadline) {
    if (!initialized_ || !link_only_ || !scanning_ || link_attempted_ || connection_requested_ || have_connection_) return Invalid();
    const auto assessment = AssessSighting(sighting, CensusTarget(target_), scan_started_ms_, now);
    Log("Physical ZL sighting gate assessment=%u captured_boot_ms=%llu scan_start=%llu now=%llu", unsigned(assessment),
        static_cast<unsigned long long>(sighting.captured_boot_ms), static_cast<unsigned long long>(scan_started_ms_), static_cast<unsigned long long>(now));
    if (assessment != SightingAssessment::FreshCandidate) return 0;
    if (identity_test_ && (capture_deadline <= now || capture_deadline-now > 60000)) return Invalid();
    capture_deadline_ms_ = capture_deadline;
    // Conservatively block repeat attempts across process exit/relaunch. Only
    // the operator may remove this file after a confirmed physical reboot.
    link_attempted_ = true;
    FILE *marker = std::fopen("sdmc:/config/amazon-remote/link-test-pending.txt", "wx");
    if (!marker) { Log("Pending marker could not be created exclusively; FAIL CLOSED, no connection IPC, reboot/recovery required"); return Invalid(); }
    const bool wrote = std::fprintf(marker, "prototype=0.1.10 input_diagnostic=%u boot_ms=%llu\nReboot must be confirmed before operator removes this marker.\n", input_diagnostic_, static_cast<unsigned long long>(now)) > 0;
    const bool flushed = std::fflush(marker) == 0;
    const bool synced = ::fsync(::fileno(marker)) == 0;
    const bool closed = std::fclose(marker) == 0;
    if (!wrote || !flushed || !synced || !closed) { Log("Pending marker persistence failed; FAIL CLOSED, no connection IPC, marker retained"); return Invalid(); }
    Log("Persistent pending marker retained on ALL outcomes; no public address-cancel API; physical reboot required after this test");
    const auto rc = RequestConnection(&sighting);
    return rc;
}
Result LibnxSession::Connect(bool filtered_discovery, bool authenticated_reads) {
    if (identity_test_ || !initialized_ || connection_requested_ || have_connection_ || scanning_) return Invalid();
    authenticated_reads_ = authenticated_reads;
    auto rc = btdevAcquireBleConnectionStateChangedEvent(&connection_event_);
    if (R_FAILED(rc)) { Log("acquire connection event failed Result=%08lx", static_cast<unsigned long>(rc)); return rc; }
    connection_event_open_ = true;
    rc = btdevAcquireBlePairingEvent(&pairing_event_);
    if (R_FAILED(rc)) { Log("acquire pairing event failed Result=%08lx", static_cast<unsigned long>(rc)); return rc; }
    pairing_event_open_ = true;
    Log("Authentication policy: %s; successful IPC/event is not proof of bonding", authenticated_reads_ ? "NoMitm on owned-target GATT reads/writes (may trigger pairing/encryption)" : "None (legacy baseline)");
    if (!filtered_discovery) return RequestConnection();
    // An experimental smart scan, NOT an unfiltered/generic scan. Never use a
    // fabricated manufacturer filter or infer absence from a negative result.
    rc = btdevAcquireBleScanEvent(&scan_event_);
    if (R_FAILED(rc)) return rc;
    scan_event_open_ = true;
    BtdrvGattAttributeUuid uuid{}; uuid.size = HidServiceUuid128.size();
    std::copy(HidServiceUuid128.begin(), HidServiceUuid128.end(), uuid.uuid);
    rc = btdevStartBleScanSmartDevice(&uuid);
    Log("API=btdevStartBleScanSmartDevice(HID128) Result=%08lx; HID16 matching is unverified; exclusive custom-client test only", static_cast<unsigned long>(rc));
    if (R_FAILED(rc)) return rc;
    scanning_ = true; scan_started_ms_ = last_scan_poll_ms_ = 0;
    general_cache_seen_ = smart_cache_seen_ = general_target_dumped_ = smart_target_dumped_ = false;
    Log("Filtered discovery started; exact configured MAC only; 20-second deadline; no automatic fallback or ambient pairing");
    return 0;
}
Result LibnxSession::RequestConnection(const SightingWire *sighting) {
    if (!initialized_ || connection_requested_ || have_connection_) return Invalid();
    // Do not adopt or later disconnect another client's pre-existing target.
    std::array<BtdrvBleConnectionInfo, 16> connections{}; u8 count = 0;
    auto rc = link_only_ ? btmuBleGetConnectionState(connections.data(), connections.size(), &count) : btdevGetBleConnectionInfoList(connections.data(), connections.size(), &count);
    Log("API=connection-list precheck Result=%08lx count=%u", static_cast<unsigned long>(rc), count);
    if (R_FAILED(rc)) return rc;
    if (count >= connections.size()) return Invalid();
    for (std::size_t i = 0; i < count; ++i)
        if (!std::memcmp(&connections[i].addr, &target_, sizeof target_)) return Invalid();
    // Discard a previously signalled discovery indication before our connect.
    // BTM's discovery event carries no connection identity; coexistence remains
    // explicitly unsupported with another client registering these UUID paths.
    if (!link_only_ || identity_test_) {
        rc = eventWait(&discovery_event_, 0);
        if (R_FAILED(rc) && !Timeout(rc)) return rc;
    }
    discovery_signalled_ = false;
    if (link_only_) {
        // File flush, IPC and logging may have blocked since the physical ZL
        // check. Reassess with the real clock immediately before the request.
        const auto dispatch_ms = armTicksToNs(armGetSystemTick()) / 1000000;
        if (!sighting || (identity_test_ && dispatch_ms >= capture_deadline_ms_) ||
            AssessSighting(*sighting, CensusTarget(target_), scan_started_ms_, dispatch_ms) != SightingAssessment::FreshCandidate) {
            Log("Final presence gate expired/invalid; NO connection IPC; pending marker retained, reboot/recovery required");
            return Invalid();
        }
    }
    // btmu performs connection/client management; no private MissionControl BLE code.
    rc = link_only_ ? btmuBleConnect(target_) : btdevConnectToGattServer(target_);
    Log("API=%s Result=%08lx", link_only_ ? "btmuBleConnect(link-only physical ZL)" : "btdevConnectToGattServer", static_cast<unsigned long>(rc));
    if (R_SUCCEEDED(rc)) {
        connection_requested_ = true;
        requested_ms_ = link_only_ ? armTicksToNs(armGetSystemTick()) / 1000000 : 0;
        Log("targeted GATT connection requested");
    }
    return rc;
}
Result LibnxSession::StopOwnScan() {
    if (!scanning_) return 0;
    const auto rc = link_only_ ? btmuStopBleScanForSmartDevice() : btdevStopBleScanSmartDevice();
    Log("API=btdevStopBleScanSmartDevice(own successful start) Result=%08lx", static_cast<unsigned long>(rc));
    if (R_SUCCEEDED(rc)) scanning_ = false;
    return rc;
}
void LibnxSession::LogScanCacheSummary(const char *name, Result result, std::uint8_t count,
    const void *data, std::size_t size) {
    const bool smart = std::strcmp(name, "smart") == 0;
    auto &seen = smart ? smart_cache_seen_ : general_cache_seen_;
    auto &previous_count = smart ? smart_cache_count_ : general_cache_count_;
    auto &previous_matches = smart ? smart_cache_matches_ : general_cache_matches_;
    auto &previous_result = smart ? smart_cache_result_ : general_cache_result_;
    auto &dumped = smart ? smart_target_dumped_ : general_target_dumped_;
    const auto target = CensusTarget(target_);
    const auto summary = InspectScanCache(R_SUCCEEDED(result), data, size, count, target);
    if (!seen || count != previous_count || summary.target_matches != previous_matches || result != previous_result) {
        CensusLog(logger_, name, result, count, summary);
        seen = true; previous_count = count; previous_matches = summary.target_matches; previous_result = result;
    }
    // At most one bounded target-only dump per bucket per successful scan.
    if (summary.valid && summary.target_matches && !dumped) {
        CensusTargetLog(logger_, name, data, size, count, target);
        dumped = true;
    }
}
Result LibnxSession::PollScan(std::uint64_t now) {
    if (!scan_started_ms_) { scan_started_ms_ = now; Log("Filtered scan clock started boot_ms=%llu", static_cast<unsigned long long>(now)); }
    const auto event_rc = eventWait(&scan_event_, 0);
    if (R_FAILED(event_rc) && !Timeout(event_rc)) return event_rc;
    // Signals are only hints. Bound IPC rate even when continuously signalled.
    if (!last_scan_poll_ms_ || now - last_scan_poll_ms_ >= 500) {
        last_scan_poll_ms_ = now;
        std::array<BtdrvBleScanResult, 10> results{}; u8 count = 0;
        const auto general_rc = btmuGetBleScanResultsForGeneral(results.data(), results.size(), &count);
        LogScanCacheSummary("general", general_rc, count, results.data(), sizeof results);
        // General is diagnostics ONLY: never connects/adopts based on that cache.
        results.fill({}); count = 0;
        const auto rc = btmuGetBleScanResultsForSmartDevice(results.data(), results.size(), &count);
        LogScanCacheSummary("smart", rc, count, results.data(), sizeof results);
        if (R_FAILED(rc)) { Log("API=btmuGetBleScanResultsForSmartDevice Result=%08lx", static_cast<unsigned long>(rc)); return rc; }
        if (!ValidScanCount(count)) { Log("Rejected impossible scan result count=%u", count); return Invalid(); }
        if (count == results.size()) Log("Scan list at capacity; unreturned advertisers may exist");
        for (std::size_t i = 0; i < count; ++i) {
            if (link_only_) break; // caches never authorize this diagnostic link
            if (std::memcmp(&results[i].addr, &target_, sizeof target_)) continue;
            Log("Exact configured MAC observed in filtered results; raw scan bytes follow (undocumented fields not interpreted)");
            const auto *bytes = reinterpret_cast<const u8 *>(&results[i]);
            for (std::size_t offset = 0; offset < sizeof results[i]; offset += 32) {
                char hex[65]{}; FormatHex(hex, sizeof hex, bytes + offset, std::min<std::size_t>(32, sizeof results[i] - offset));
                Log("target scan offset=%zu bytes=%s", offset, hex);
            }
            auto stop_rc = StopOwnScan(); if (R_FAILED(stop_rc)) return stop_rc;
            return RequestConnection();
        }
    }
    if (!connection_requested_ && now - scan_started_ms_ >= (identity_test_ ? 60000u : 20000u)) {
        Log("Filtered scan INCONCLUSIVE: target not observed; HID16-vs-HID128 normalization is unverified. Not proof remote is absent or BLE disabled.");
        auto rc = StopOwnScan(); return R_FAILED(rc) ? rc : Invalid();
    }
    return 0;
}
Result LibnxSession::LoadCache(std::uint32_t connection) {
    if (identity_test_) return LoadFingerprintCache(connection);
    // SDK array limits are100. Treat a full return as potential truncation and reject.
    std::array<BtdevGattService, 100> services{};
    std::array<BtdevGattCharacteristic, 100> characteristics{};
    std::array<BtdevGattDescriptor, 100> descriptors{};
    std::array<Attribute, 10> batch{}; std::size_t batch_size = 0;
    const auto binding = [&]() -> Result {
        if (!identity_test_) return 0;
        const auto rc = CheckCurrentTarget();
        if (R_FAILED(rc)) return rc;
        return armTicksToNs(armGetSystemTick()) / 1000000-linked_ms_ < 20000 ? 0 : Invalid();
    };
    auto append = [&](Attribute a) {
        batch[batch_size++] = a;
        if (batch_size == batch.size()) { if (!client_.Append(connection, batch.data(), batch_size)) return false; batch_size = 0; }
        return true;
    };
    u8 service_count = 0;
    auto rc = binding(); if (R_FAILED(rc)) return rc;
    rc = btdevGetGattServices(connection, services.data(), services.size(), &service_count);
    if (R_FAILED(rc)) return rc;
    if (!service_count) return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    if (service_count >= services.size()) return Invalid();
    client_.Connect(connection);
    for (std::size_t s = 0; s < service_count; ++s) {
        auto &service = services[s];
        if (service.instance_id > 255) return Invalid();
        char uuid[65]{}; FormatHex(uuid, sizeof uuid, service.attr.uuid.uuid, std::min<std::size_t>(service.attr.uuid.size, sizeof service.attr.uuid.uuid));
        Log("inventory service handle=%04x end=%04x uuid=%s short=%04x instance=%u primary=%u", service.attr.handle, service.end_group_handle, uuid,
            ShortUuid(service.attr.uuid), service.instance_id, service.primary_service);
        if (!append({Kind::Service, service.attr.handle, service.end_group_handle, ShortUuid(service.attr.uuid), 0,
                     static_cast<u8>(service.instance_id), service.primary_service})) return Invalid();
        u8 characteristic_count = 0;
        rc = binding(); if (R_FAILED(rc)) return rc;
        rc = btdevGattServiceGetCharacteristics(&service, characteristics.data(), characteristics.size(), &characteristic_count);
        if (R_FAILED(rc)) return rc;
        if (characteristic_count >= characteristics.size()) return Invalid();
        for (std::size_t c = 0; c < characteristic_count; ++c) {
            auto &characteristic = characteristics[c];
            if (characteristic.instance_id > 255) return Invalid();
            FormatHex(uuid, sizeof uuid, characteristic.attr.uuid.uuid, std::min<std::size_t>(characteristic.attr.uuid.size, sizeof characteristic.attr.uuid.uuid));
            Log("inventory characteristic service=%04x handle=%04x uuid=%s short=%04x instance=%u properties=%02x", service.attr.handle,
                characteristic.attr.handle, uuid, ShortUuid(characteristic.attr.uuid), characteristic.instance_id, characteristic.properties);
            if (!append({Kind::Characteristic, characteristic.attr.handle, 0, ShortUuid(characteristic.attr.uuid),
                         characteristic.properties, static_cast<u8>(characteristic.instance_id), false})) return Invalid();
            u8 descriptor_count = 0;
            rc = binding(); if (R_FAILED(rc)) return rc;
            rc = btdevGattCharacteristicGetDescriptors(&characteristic, descriptors.data(), descriptors.size(), &descriptor_count);
            if (R_FAILED(rc)) return rc;
            if (descriptor_count >= descriptors.size()) return Invalid();
            for (std::size_t d = 0; d < descriptor_count; ++d) {
                const auto &descriptor = descriptors[d];
                FormatHex(uuid, sizeof uuid, descriptor.attr.uuid.uuid, std::min<std::size_t>(descriptor.attr.uuid.size, sizeof descriptor.attr.uuid.uuid));
                Log("inventory descriptor char=%04x handle=%04x uuid=%s short=%04x", characteristic.attr.handle, descriptor.attr.handle, uuid, ShortUuid(descriptor.attr.uuid));
                if (!append({Kind::Descriptor, descriptor.attr.handle, 0, ShortUuid(descriptor.attr.uuid), 0, 0, false})) return Invalid();
            }
        }
    }
    if (batch_size && !client_.Append(connection, batch.data(), batch_size)) return Invalid();
    Log("bounded SDK discovery: %zu attributes", client_.Count());
    return client_.FinishCache(connection) ? 0 : Invalid();
}
Result LibnxSession::LoadFingerprintCache(std::uint32_t connection) {
    // This two-read experiment does not require a complete HOGP cache. Keep
    // unrelated SDK records uninterpreted; do not guess that end0 means Included
    // or relax the full client's strict service-range validation.
    if (!identity_test_ || connection != connection_) return Invalid();
    const auto binding = [&]() -> Result {
        const auto rc = CheckCurrentTarget();
        if (R_FAILED(rc)) return rc;
        return armTicksToNs(armGetSystemTick()) / 1000000-linked_ms_ < 20000 ? 0 : Invalid();
    };
    std::array<BtdevGattService,100> services{};
    std::array<BtdevGattCharacteristic,100> characteristics{};
    std::array<Attribute,100> service_views{};
    std::array<Attribute,2> selected{};
    auto rc = binding(); if (R_FAILED(rc)) return rc;
    u8 count = 0;
    rc = btdevGetGattServices(connection, services.data(), services.size(), &count);
    Log("Fingerprint service getter Result=%08lx count=%u", static_cast<unsigned long>(rc), count);
    if (R_FAILED(rc)) return rc;
    if (!count) return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    if (count >= services.size()) return Invalid();
    for (std::size_t i = 0; i < count; ++i) {
        const auto &s = services[i];
        const auto uuid = ShortUuid(s.attr.uuid);
        const bool required = uuid == 0x180a || uuid == 0x1812;
        Log("Fingerprint service handle=%04x end=%04x short=%04x primary=%u required=%u; unrelated_records_uninterpreted=1",
            s.attr.handle, s.end_group_handle, uuid, s.primary_service, required);
        if (required && (s.instance_id > 255 || s.attr.connection_handle != connection)) return Invalid();
        service_views[i] = {Kind::Service, s.attr.handle, s.end_group_handle, uuid, 0,
            static_cast<u8>(required ? s.instance_id : 0), s.primary_service};
    }
    if (!SelectFingerprintServices(service_views.data(), count, selected)) {
        Log("Required DI/HID services missing, ambiguous, non-primary or invalid/overlapping ranges; no identity reads");
        return Invalid();
    }
    client_.Connect(connection);
    if (!client_.Append(connection, selected.data(), selected.size())) return Invalid();
    for (const auto &wanted : selected) {
        BtdevGattService *native = nullptr;
        for (std::size_t i = 0; i < count; ++i)
            if (services[i].attr.handle == wanted.handle && ShortUuid(services[i].attr.uuid) == wanted.uuid) {
                if (native) return Invalid();
                native = &services[i];
            }
        if (!native) return Invalid();
        rc = binding(); if (R_FAILED(rc)) return rc;
        u8 char_count = 0;
        rc = btdevGattServiceGetCharacteristics(native, characteristics.data(), characteristics.size(), &char_count);
        Log("Fingerprint characteristic getter service=%04x Result=%08lx count=%u", wanted.uuid, static_cast<unsigned long>(rc), char_count);
        if (R_FAILED(rc)) return rc;
        if (char_count >= characteristics.size()) return Invalid();
        const auto expected = wanted.uuid == 0x180a ? 0x2a50 : 0x2a4b;
        Attribute candidate{}; bool found = false;
        for (std::size_t i = 0; i < char_count; ++i) {
            const auto &c = characteristics[i];
            if (ShortUuid(c.attr.uuid) != expected) continue;
            if (found || c.attr.connection_handle != connection || c.instance_id > 255 ||
                c.attr.handle <= wanted.handle || c.attr.handle > wanted.end) return Invalid();
            found = true;
            candidate = {Kind::Characteristic, c.attr.handle, 0, static_cast<u16>(expected), c.properties, static_cast<u8>(c.instance_id), false};
        }
        if (!found) { Log("Required fingerprint characteristic missing service=%04x char=%04x; no fallback", wanted.uuid, expected); return Invalid(); }
        Log("Selected fingerprint char service=%04x char=%04x handle=%04x instance=%u properties=%02x; properties_not_fabricated=1",
            wanted.uuid, candidate.uuid, candidate.handle, candidate.instance, candidate.properties);
        if (!client_.Append(connection, &candidate, 1)) return Invalid();
    }
    rc = binding(); if (R_FAILED(rc)) return rc;
    Log("Fingerprint-only cache projected: %zu attributes, no descriptor enumeration, no unrelated-service adoption", client_.Count());
    return client_.FinishCache(connection) ? 0 : Invalid();
}
Result LibnxSession::ReadInputInventory(const char *label, bool adopt) {
    if (!input_diagnostic_ || !fingerprint_logged_ ||
        (adopt && (!security_read_logged_ || client_.GetPhase() != Phase::SecurityReadVerified))) return Invalid();
    const auto hid = client_.HidService();
    if (!hid.primary || hid.uuid != 0x1812 || !hid.handle || hid.end < hid.handle) return Invalid();
    const auto binding = [&]() -> Result {
        const auto result = CheckCurrentTarget();
        if (R_FAILED(result)) return result;
        return armTicksToNs(armGetSystemTick()) / 1000000-linked_ms_ < 90000 ? 0 : Invalid();
    };
    auto rc = binding(); if (R_FAILED(rc)) return rc;
    std::array<BtmGattCharacteristic,100> characteristics{}; u8 count = 0;
    rc = btmuGetGattCharacteristics(connection_, hid.handle, characteristics.data(), characteristics.size(), &count);
    Log("HID_METADATA %s Result=%08lx count=%u sdk_record_bytes=%zu; no format guessing or property fabrication", label, static_cast<unsigned long>(rc), count, sizeof(BtmGattCharacteristic));
    if (R_FAILED(rc)) return rc;
    if (!count || count >= characteristics.size()) return Invalid();
    for (std::size_t i = 0; i < count; ++i) {
        const auto &c = characteristics[i];
        Log("HID_METADATA %s index=%zu handle=%04x uuid=%04x instance=%u properties=%02x", label, i, c.handle, ShortUuid(c.uuid), c.instance_id, c.properties);
        for(std::size_t offset=0;offset<sizeof c;offset+=32) {
            char raw[65]{};
            if(!FormatHex(raw,sizeof raw,reinterpret_cast<const u8 *>(&c)+offset,std::min<std::size_t>(32,sizeof c-offset))) return Invalid();
            Log("HID_METADATA_RAW %s index=%zu offset=%zu bytes=%s",label,i,offset,raw);
        }
    }
    rc = binding(); if (R_FAILED(rc)) return rc;
    if (!adopt) return 0; // Pre-security observation never admits reports.
    std::sort(characteristics.begin(), characteristics.begin()+count,
        [](const auto &a, const auto &b) { return a.handle < b.handle; });
    std::array<Attribute,HogpClient::ReportCapacity*3> reports{}; std::size_t size = 0;
    std::array<BtmGattDescriptor,100> descriptors{};
    unsigned report_count = 0;
    // Validate all characteristic boundaries before querying any descriptors.
    for (std::size_t i = 0; i < count; ++i) {
        const auto &c = characteristics[i];
        if (c.handle <= hid.handle || c.handle > hid.end || (i && c.handle == characteristics[i-1].handle)) return Invalid();
        if (ShortUuid(c.uuid) != 0x2a4d) continue;
        if (c.instance_id > 255 || ++report_count > HogpClient::ReportCapacity) return Invalid();
        for (std::size_t j = 0; j < i; ++j)
            if (ShortUuid(characteristics[j].uuid) == 0x2a4d && characteristics[j].instance_id == c.instance_id) {
                Log("INPUT_METADATA_AMBIGUOUS: distinct Report handles share UUID/instance; no descriptor reads or notification writes. Raw records preserved; instance values will NOT be invented.");
                return Invalid();
            }
    }
    if (report_count < 2) return Invalid();
    // BTM may expose zeros: do not invent Notify. Match the independent public
    // driver cache by exact HID service UUID/instance and Report UUID/instance.
    // All records are validated before any observed driver property is adopted.
    std::array<u8,100> driver_properties{}; std::array<bool,100> driver_seen{};
    bool properties_missing=false;
    for(std::size_t i=0;i<count;++i)
        if(ShortUuid(characteristics[i].uuid)==0x2a4d && !characteristics[i].properties) properties_missing=true;
    if(properties_missing) {
        rc=binding(); if(R_FAILED(rc)) return rc;
        CachedCharacteristicGetter getter;
        rc=getter.Open();
        Log("PROPERTY_CACHE getter-only service open Result=%08lx; no driver initialization or event consumption",static_cast<unsigned long>(rc));
        if(R_FAILED(rc)) return rc;
        const auto service=Id(hid); const auto filter=Uuid(0x2a4d);
        BtdrvGattId previous{};
        for(unsigned n=0;n<report_count;++n) {
            rc=binding(); if(R_FAILED(rc)) return rc;
            CachedCharacteristicOut out{};
            rc=n ? getter.Next(connection_,service,previous,filter,out) : getter.First(connection_,service,filter,out);
            Log("PROPERTY_CACHE query=%s ordinal=%u Result=%08lx uuid=%04x instance=%u properties=%02x",
                n?"next":"first",n,static_cast<unsigned long>(rc),ShortUuid(out.id.uuid),out.id.instance_id,out.properties);
            if(R_FAILED(rc)) return rc;
            rc=binding(); if(R_FAILED(rc)) return rc;
            if(ShortUuid(out.id.uuid)!=0x2a4d) { Log("PROPERTY_CACHE wrong UUID; no adoption"); return Invalid(); }
            std::size_t match=count;
            for(std::size_t i=0;i<count;++i)
                if(ShortUuid(characteristics[i].uuid)==0x2a4d && characteristics[i].instance_id==out.id.instance_id) {
                    if(match!=count) return Invalid();
                    match=i;
                }
            if(match==count || driver_seen[match]) { Log("PROPERTY_CACHE missing or repeated instance; no adoption"); return Invalid(); }
            if(characteristics[match].properties && characteristics[match].properties!=out.properties) {
                Log("PROPERTY_CACHE nonzero BTM/driver conflict; no adoption"); return Invalid();
            }
            driver_seen[match]=true; driver_properties[match]=out.properties; previous=out.id;
        }
        for(std::size_t i=0;i<count;++i) if(ShortUuid(characteristics[i].uuid)==0x2a4d) {
            if(!driver_seen[i]) return Invalid();
            Log("PROPERTY_CACHE matched handle=%04x instance=%u btm=%02x driver=%02x; observed properties, not fabricated",
                characteristics[i].handle,characteristics[i].instance_id,characteristics[i].properties,driver_properties[i]);
            characteristics[i].properties=driver_properties[i];
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        const auto &c = characteristics[i];
        if (ShortUuid(c.uuid) != 0x2a4d) continue;
        rc = binding(); if (R_FAILED(rc)) return rc;
        u8 descriptor_count = 0; descriptors.fill({});
        rc = btmuGetGattDescriptors(connection_, c.handle, descriptors.data(), descriptors.size(), &descriptor_count);
        Log("Report descriptor getter char=%04x instance=%u Result=%08lx count=%u", c.handle, c.instance_id, static_cast<unsigned long>(rc), descriptor_count);
        if (R_FAILED(rc)) return rc;
        if (!descriptor_count || descriptor_count >= descriptors.size()) return Invalid();
        std::sort(descriptors.begin(), descriptors.begin()+descriptor_count,
            [](const auto &a, const auto &b) { return a.handle < b.handle; });
        reports[size++] = {Kind::Characteristic, c.handle, 0, 0x2a4d, c.properties, static_cast<u8>(c.instance_id), false};
        const auto boundary = i+1 < count ? characteristics[i+1].handle : static_cast<std::uint32_t>(hid.end)+1;
        bool reference = false, ccc = false;
        for (std::size_t d = 0; d < descriptor_count; ++d) {
            const auto &descriptor = descriptors[d]; const auto uuid = ShortUuid(descriptor.uuid);
            char raw[2*sizeof(BtmGattDescriptor)+1]{};
            FormatHex(raw, sizeof raw, reinterpret_cast<const u8 *>(&descriptor), sizeof descriptor);
            Log("Report descriptor char=%04x handle=%04x uuid=%04x raw=%s", c.handle, descriptor.handle, uuid, raw);
            if (descriptor.handle <= c.handle || descriptor.handle >= boundary ||
                (d && descriptor.handle == descriptors[d-1].handle)) return Invalid();
            if (uuid != 0x2908 && uuid != 0x2902) continue; // Unrelated/vendor descriptors never adopted.
            bool &seen = uuid == 0x2908 ? reference : ccc;
            if (seen || size == reports.size()) return Invalid();
            seen = true;
            reports[size++] = {Kind::Descriptor, descriptor.handle, 0, uuid, 0, 0, false};
        }
        if (!reference) return Invalid();
    }
    rc = binding(); if (R_FAILED(rc)) return rc;
    if (!client_.BeginVerifiedInputs(reports.data(), size)) {
        Log("INPUT_METADATA_REJECTED: phase=%u error=%u; no notifications/writes authorized", unsigned(GetPhase()), unsigned(GetError()));
        return Invalid();
    }
    for(std::size_t i=0;i<count;++i) if(ShortUuid(characteristics[i].uuid)==0x2a4d)
        Log("INPUT_REPORT_METADATA handle=%04x instance=%u properties=%02x notify=%u",
            characteristics[i].handle,characteristics[i].instance_id,characteristics[i].properties,
            bool(characteristics[i].properties&0x10));
    Log("INPUT_METADATA_ADMITTED: %u Report characteristics, %zu projected records; read references with NoMitm, then only IDs1/2 may subscribe if Notify and CCC are present", report_count, size);
    return 0;
}
Result LibnxSession::Execute(const Request &r) {
    if (!have_connection_ || r.connection != connection_) return Invalid();
    if (identity_test_) {
        if (authenticated_reads_ || r.generation != client_.Generation()) return Invalid();
        const auto phase = client_.GetPhase();
        const bool fingerprint = (phase == Phase::Identity || phase == Phase::Map) && IsFingerprintRead(r) && r.authentication == Authentication::None;
        const bool input = input_diagnostic_ && IsInputDiagnosticRequest(r, phase, fingerprint_logged_, security_read_logged_, inputs_loaded_);
        if (!fingerprint && !input) return Invalid();
    }
    const auto service = Id(r.service), characteristic = Id(r.characteristic), descriptor = Id(r.descriptor);
    Log("request op=%u service=%04x char=%04x handle=%04x instance=%u descriptor=%04x auth_request=%u", unsigned(r.operation), r.service.uuid, r.characteristic.uuid, r.characteristic.handle, r.characteristic.instance, r.descriptor.uuid, unsigned(r.authentication));
    const u8 auth = authenticated_reads_ ? static_cast<u8>(BtdrvGattAuthReqType_NoMitm) : static_cast<u8>(r.authentication);
    if (identity_test_) {
        const auto rc = CheckCurrentTarget();
        const auto current = armTicksToNs(armGetSystemTick()) / 1000000;
        if (R_FAILED(rc) || !client_.Tick(current) || (input_diagnostic_ && current-linked_ms_ >= 90000)) return R_FAILED(rc) ? rc : Invalid();
    }
    switch (r.operation) {
        case Operation::ReadCharacteristic: return btLeClientReadCharacteristic(r.connection, r.service.primary, &service, &characteristic, auth);
        case Operation::ReadDescriptor: return btLeClientReadDescriptor(r.connection, r.service.primary, &service, &characteristic, &descriptor, auth);
        case Operation::RegisterNotification: return btLeClientRegisterNotification(r.connection, r.service.primary, &service, &characteristic);
        case Operation::WriteDescriptor: return btLeClientWriteDescriptor(r.connection, r.service.primary, &service, &characteristic, &descriptor, r.value.data(), r.size, auth);
    }
    return Invalid();
}
Result LibnxSession::OnOperation() {
    // Raw event preserves original status and size; SDK helper otherwise clamps size.
    BtdrvBleEventInfo event{}; BtdrvBleEventType type{};
    auto rc = btGetLeEventInfo(&event, sizeof event, &type); if (R_FAILED(rc)) return rc;
    if (type != BtdrvBleEventType_ClientNotify) return Invalid();
    const auto &op = event.client_notify;
    if (op.conn_id != connection_) return 0;
    if (identity_test_) {
        if ((op.type & 4) && (!input_diagnostic_ || !Ready())) return 0;
        rc = CheckCurrentTarget(); if (R_FAILED(rc)) return rc;
        if (input_diagnostic_ && armTicksToNs(armGetSystemTick()) / 1000000-linked_ms_ >= 90000) return Invalid();
    }
    Log("user GATT event conn=%u result=%u flags=%02x service=%04x char=%04x desc=%04x size=%u", op.conn_id, op.result, op.type,
        ShortUuid(op.serv_uuid), ShortUuid(op.char_uuid), ShortUuid(op.desc_uuid), op.size);
    if (op.size > sizeof op.data) return Invalid();
    // Do not capture vendor/voice or foreign-service payloads in this diagnostic.
    if (identity_test_ && (op.type & 4) &&
        (op.type != 4 || op.result || ShortUuid(op.serv_uuid) != 0x1812 || ShortUuid(op.char_uuid) != 0x2a4d || (op.size != 3 && op.size != 4))) return Invalid();
    if (op.type & 4) {
        for (std::size_t offset = 0; offset < op.size; offset += 32) {
            char hex[65]{}; const auto count = std::min<std::size_t>(32, op.size - offset);
            FormatHex(hex, sizeof hex, op.data + offset, count);
            Log("raw notification conn=%u size=%u offset=%zu bytes=%s", op.conn_id, op.size, offset, hex);
        }
        if (!op.size) Log("raw notification conn=%u size=0 bytes=", op.conn_id);
    }
    if ((op.type & ~7U) || ((op.type & 4) && (op.type & 2))) return Invalid();
    if (op.type & 4) { // public LeEventInfo flags: bit2=notification, bit0=indication
        if (op.result) return Invalid();
        if (!Ready()) return 0; // A selected endpoint may notify before both CCC writes complete.
        InputReport report;
        if (!client_.Notification(op.conn_id, ShortUuid(op.serv_uuid), ShortUuid(op.char_uuid), op.data, op.size, report)) return Invalid();
        const auto previous = decoder_.Buttons();
        auto result = decoder_.Ingest(report.id, report.payload, report.size);
        if (result == Update::Invalid || result == Update::UnsupportedDescriptor) return Invalid();
        Log("input report=%02x buttons=%08lx update=%u", report.id, static_cast<unsigned long>(decoder_.Buttons()), unsigned(result));
        if (previous != decoder_.Buttons()) {
            char delta[240]{}; FormatButtons(delta, sizeof delta, previous, decoder_.Buttons()); Log("buttons %s", delta);
        }
        return 0;
    }
    if (!pending_operation_) return 0;
    const auto expected_flags = request_.operation == Operation::ReadDescriptor ? 2 : request_.operation == Operation::WriteDescriptor ? 3 : 0;
    if (op.type != expected_flags || ShortUuid(op.serv_uuid) != request_.service.uuid || ShortUuid(op.char_uuid) != request_.characteristic.uuid ||
        ((op.type & 2) && ShortUuid(op.desc_uuid) != request_.descriptor.uuid)) return Invalid();
    if (request_.operation == Operation::ReadCharacteristic &&
        (request_.characteristic.uuid == 0x2a50 || request_.characteristic.uuid == 0x2a4b)) {
        // Preserve bounded raw identity evidence before a fail-closed gate stops.
        for (std::size_t offset = 0; offset < op.size; offset += 32) {
            char hex[65]{}; const auto count = std::min<std::size_t>(32, op.size - offset);
            FormatHex(hex, sizeof hex, op.data + offset, count);
            Log("raw char=%04x offset=%zu bytes=%s", request_.characteristic.uuid, offset, hex);
        }
    }
    if (request_.operation == Operation::ReadDescriptor && request_.descriptor.uuid == 0x2908) {
        char hex[65]{}; FormatHex(hex, sizeof hex, op.data, std::min<std::size_t>(op.size, 32));
        Log("report reference char=%04x descriptor=%04x result=%u size=%u bytes=%s", request_.characteristic.handle, request_.descriptor.handle, op.result, op.size, hex);
        if (op.size == 2) Log("report reference id=%02x type=%u selected_candidate=%u", op.data[0], op.data[1], op.data[1] == 1 && (op.data[0] == 1 || op.data[0] == 2));
    }
    if (identity_test_) {
        const auto current = armTicksToNs(armGetSystemTick()) / 1000000;
        if (!client_.Tick(current) || (input_diagnostic_ && current-linked_ms_ >= 90000)) return Invalid();
    }
    pending_operation_ = false;
    if(client_.Complete(request_, op.result == 0, op.data, op.size)) return 0;
    if(GetError()==Error::NotifyUnavailable)
        Log("INPUT_STOP_NOTIFY_UNAVAILABLE: selected input endpoint has no observed Notify bit; no registration or CCC write. Properties were not invented.");
    else if(GetError()==Error::BadReference)
        Log("INPUT_STOP_BAD_REFERENCE: selected input reference/CCC missing or invalid; no fallback subscription.");
    return Invalid();
}
void LibnxSession::ResetInput() {
    if (decoder_.Buttons()) { char delta[240]{}; FormatButtons(delta, sizeof delta, decoder_.Buttons(), 0); Log("reset buttons %s", delta); }
    decoder_.Disconnect(); client_.Disconnect(); pending_operation_ = false; cache_loaded_ = mtu_requested_ = discovery_signalled_ = false;
}
Result LibnxSession::Poll(std::uint64_t now) {
    if (!initialized_) return Invalid();
    if (link_only_) return PollLinkOnly(now);
    if (scanning_) return PollScan(now);
    for (auto *event : {&connection_event_, &pairing_event_}) {
        const auto hint = eventWait(event, 0);
        if (R_FAILED(hint) && !Timeout(hint)) return hint;
        if (R_SUCCEEDED(hint)) Log("%s event signalled (not target-specific success proof)", event == &connection_event_ ? "connection" : "pairing");
    }
    std::array<BtdrvBleConnectionInfo, 16> connections{}; u8 n = 0;
    auto rc = btdevGetBleConnectionInfoList(connections.data(), connections.size(), &n);
    if (R_FAILED(rc)) return rc;
    if (n >= connections.size()) return Invalid();
    rc = eventWait(&discovery_event_, 0);
    if (R_SUCCEEDED(rc)) discovery_signalled_ = true;
    else if (!Timeout(rc)) return rc;
    bool present = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (std::memcmp(&connections[i].addr, &target_, sizeof target_)) continue;
        present = true;
        if (!have_connection_) {
            if (!connection_requested_) return Invalid();
            connection_ = connections[i].connection_handle; have_connection_ = true; connection_requested_ = false; requested_ms_ = now; Log("target connected: handle=%u", connection_);
        }
        else if (connections[i].connection_handle != connection_) return Invalid();
    }
    if (!present) {
        if (have_connection_) { Log("target disconnected; held input cleared"); ResetInput(); have_connection_ = false; subscription_count_ = 0; }
        if (connection_requested_) {
            if (!requested_ms_) { requested_ms_ = now; Log("waiting for target connection; deadline=30000ms poll_start_boot_ms=%llu", static_cast<unsigned long long>(requested_ms_)); }
            if (now - requested_ms_ > 30000) {
                Log("timeout waiting for target connection; elapsed_ms=%llu poll_start_boot_ms=%llu target not present in connection list",
                    static_cast<unsigned long long>(now - requested_ms_), static_cast<unsigned long long>(requested_ms_));
                return Invalid();
            }
        }
        return 0;
    }
    if (!mtu_requested_) {
        rc = btdevConfigureBleMtu(connection_, 0x200);
        if (R_FAILED(rc)) return rc;
        mtu_requested_ = true;
    }
    u16 mtu = 0; rc = btdevGetBleMtu(connection_, &mtu);
    if (R_FAILED(rc)) return rc;
    if (mtu < sizeof K7q3m7Descriptor + 3) {
        if (now - requested_ms_ >= 10000) return Invalid();
        return 0;
    }
    if (!cache_loaded_) {
        // Do not parse a partially populated BTM cache before discovery completion.
        if (!discovery_signalled_) {
            if (now - requested_ms_ >= 20000) return Invalid();
            return 0;
        }
        rc = LoadCache(connection_);
        if (R_FAILED(rc)) { // Only empty service cache can legitimately be pending.
            if (rc == MAKERESULT(Module_Libnx, LibnxError_NotInitialized) && now - requested_ms_ < 20000) return 0;
            return rc;
        }
        cache_loaded_ = true;
    }
    rc = eventWait(&operation_event_, 0);
    if (R_SUCCEEDED(rc)) { rc = OnOperation(); if (R_FAILED(rc)) return rc; }
    else if (!Timeout(rc)) return rc;
    if (!client_.Tick(now)) return Invalid();
    if (!pending_operation_) {
        Request next;
        if (client_.Next(next, now)) {
            request_ = next; rc = Execute(request_);
            if (R_FAILED(rc)) { client_.Complete(request_, false, nullptr, 0); return rc; }
            if (request_.operation == Operation::RegisterNotification) {
                // Registration is synchronous IPC; subsequent CCC write has async completion.
                if (subscription_count_ >= subscriptions_.size()) return Invalid();
                subscriptions_[subscription_count_++] = request_;
                if (!client_.Complete(request_, true, nullptr, 0)) return Invalid();
            } else pending_operation_ = true;
        }
    }
    return 0;
}
Result LibnxSession::PollLinkOnly(std::uint64_t now) {
    if (scanning_) { const auto rc = PollScan(now); if (R_FAILED(rc)) return rc; }
    if (!connection_requested_) return 0;
    const auto hint = eventWait(&connection_event_, 0);
    if (R_FAILED(hint) && !Timeout(hint)) return hint;
    if (!last_connection_poll_ms_ || now - last_connection_poll_ms_ >= 500) {
        last_connection_poll_ms_ = now;
        std::array<BtdrvBleConnectionInfo,16> connections{}; u8 count = 0;
        const auto rc = btmuBleGetConnectionState(connections.data(), connections.size(), &count);
        Log("API=btmuBleGetConnectionState(link-only) Result=%08lx count=%u", static_cast<unsigned long>(rc), count);
        if (R_FAILED(rc)) return rc;
        if (count >= connections.size()) return Invalid();
        bool found = false;
        for (std::size_t i = 0; i < count; ++i) {
            if (std::memcmp(&connections[i].addr, &target_, sizeof target_)) continue;
            if (found || (have_connection_ && connection_ != connections[i].connection_handle)) return Invalid();
            found = true;
            if (!have_connection_) {
                connection_ = connections[i].connection_handle; have_connection_ = true;
                linked_ms_ = armTicksToNs(armGetSystemTick()) / 1000000;
                Log("Exact configured target handle=%lu observed; fingerprint_unverified=1 authenticated_identity_unverified=1 driver_client_ownership_unproven=1", static_cast<unsigned long>(connection_));
                const auto stop = StopOwnScan(); if (R_FAILED(stop)) return stop;
            }
        }
        if (have_connection_ && !found) { Log("LINK_ONLY target handle no longer present; no reconnect"); return Invalid(); }
    }
    if (!have_connection_ && now - requested_ms_ >= 30000) {
        Log("LINK_ONLY request timed out; cannot cancel pending address; reboot required, persistent marker retained");
        const auto rc = StopOwnScan(); return R_FAILED(rc) ? rc : Invalid();
    }
    return identity_test_ && have_connection_ ? PollIdentity(armTicksToNs(armGetSystemTick()) / 1000000) : 0;
}
Result LibnxSession::CheckCurrentTarget() {
    if (!have_connection_ || !identity_test_) return Invalid();
    std::array<BtdrvBleConnectionInfo,16> connections{}; u8 count = 0;
    const auto rc = btmuBleGetConnectionState(connections.data(), connections.size(), &count);
    if (R_FAILED(rc)) return rc;
    if (count >= connections.size()) return Invalid();
    unsigned matches = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const bool address = !std::memcmp(&connections[i].addr, &target_, sizeof target_);
        const bool handle = connections[i].connection_handle == connection_;
        if (address != handle) return Invalid(); // Address changed handle or handle reassigned.
        if (address) ++matches;
    }
    return matches == 1 ? 0 : Invalid();
}
Result LibnxSession::PollIdentity(std::uint64_t now) {
    if (!client_.Tick(now)) return Invalid(); // Expire before consuming completion.
    if (input_diagnostic_ && now-linked_ms_ >= 90000) {
        Log("INPUT_DIAGNOSTIC fixed90s connected-session limit reached; no reconnect or automatic security retry");
        return Invalid();
    }
    if (input_diagnostic_ && pairing_event_open_) {
        const auto hint = eventWait(&pairing_event_, 0);
        if (R_SUCCEEDED(hint)) Log("Generic BTM pairing hint observed; no target-specific security/bond claim; no PIN/SSP response performed");
        else if (!Timeout(hint)) return hint;
    }
    if (client_.GetPhase() == Phase::FingerprintVerified) {
        if (!fingerprint_logged_) {
            fingerprint_logged_ = true;
            Log("FINGERPRINT_VERIFIED: VID0171/PID0427 and exact149-byte HID map matched; NOT authenticated identity/bonding/button support.");
        }
        if (!input_diagnostic_) return 0;
        auto inventory_rc = ReadInputInventory("before_security", false);
        if (R_FAILED(inventory_rc)) return inventory_rc;
        if (!client_.BeginSecurityRead()) return Invalid();
        Log("SECURITY_READ: one exact-map read requesting NoMitm. May initiate encryption/pairing; no successful security or bond assumed, no AuthNone fallback.");
    }
    if (client_.GetPhase() == Phase::SecurityReadVerified) {
        if (!security_read_logged_) {
            security_read_logged_ = true;
            Log("SECURITY_READ_COMPLETED: exact149-byte map returned with NoMitm requested. Encryption and durable bond remain UNVERIFIED.");
        }
        auto inventory_rc = ReadInputInventory("after_security", true);
        if (R_FAILED(inventory_rc)) return inventory_rc;
        inputs_loaded_ = true;
    }
    if (Ready() && !input_ready_logged_) {
        input_ready_logged_ = true;
        Log("INPUT_SUBSCRIPTIONS_READY: only report IDs1/2 selected via ReportReference and two standard CCC0100 writes; press remote buttons individually. Logging only: no virtual-controller output, pairing/encryption unverified.");
    }
    if (!cache_loaded_ && now - linked_ms_ >= 20000) { Log("Fingerprint cache/discovery deadline expired; no fallback/authentication"); return Invalid(); }
    auto rc = eventWait(&discovery_event_, 0);
    if (R_SUCCEEDED(rc)) { discovery_signalled_ = true; Log("User discovery hint received; not connection identity proof"); }
    else if (!Timeout(rc)) return rc;
    if (!cache_loaded_ && (!last_identity_poll_ms_ || now-last_identity_poll_ms_ >= 250)) {
        last_identity_poll_ms_ = now;
        rc = CheckCurrentTarget(); if (R_FAILED(rc)) return rc;
        if (!mtu_requested_) {
            Log("Requesting public MTU512 for149-byte fingerprint read; auth=None, no pairing requested");
            rc = CheckCurrentTarget(); if (R_FAILED(rc)) return rc;
            rc = btdevConfigureBleMtu(connection_, 0x200);
            Log("API=btdevConfigureBleMtu Result=%08lx", static_cast<unsigned long>(rc));
            if (R_FAILED(rc)) return rc;
            mtu_requested_ = true;
        }
        u16 mtu = 0; rc = btdevGetBleMtu(connection_, &mtu);
        Log("API=btdevGetBleMtu Result=%08lx mtu=%u", static_cast<unsigned long>(rc), mtu);
        if (R_FAILED(rc)) return rc;
        const auto current = armTicksToNs(armGetSystemTick()) / 1000000;
        if (current-linked_ms_ >= 20000) return Invalid();
        if (mtu < sizeof K7q3m7Descriptor + 3) {
            if (current-linked_ms_ >= 10000) return Invalid();
            return 0;
        }
        if (!discovery_signalled_) return 0;
        rc = CheckCurrentTarget(); if (R_FAILED(rc)) return rc;
        rc = LoadCache(connection_);
        if (armTicksToNs(armGetSystemTick()) / 1000000-linked_ms_ >= 20000) return Invalid();
        if (R_FAILED(rc)) {
            if (rc == MAKERESULT(Module_Libnx, LibnxError_NotInitialized)) return 0;
            return rc;
        }
        rc = CheckCurrentTarget(); if (R_FAILED(rc)) return rc;
        cache_loaded_ = true;
    }
    if (!cache_loaded_) return 0;
    if (!client_.Tick(armTicksToNs(armGetSystemTick()) / 1000000)) return Invalid();
    rc = eventWait(&operation_event_, 0);
    if (R_SUCCEEDED(rc)) { rc = OnOperation(); if (R_FAILED(rc)) return rc; }
    else if (!Timeout(rc)) return rc;
    if (!client_.Tick(armTicksToNs(armGetSystemTick()) / 1000000)) return Invalid();
    if (!pending_operation_) {
        Request next;
        if (client_.Next(next, armTicksToNs(armGetSystemTick()) / 1000000)) {
            request_ = next; rc = Execute(request_);
            if (R_FAILED(rc)) { client_.Complete(request_, false, nullptr, 0); return rc; }
            if (request_.operation == Operation::RegisterNotification) {
                if (subscription_count_ >= subscriptions_.size()) return Invalid();
                subscriptions_[subscription_count_++] = request_;
                if (!client_.Complete(request_, true, nullptr, 0)) return Invalid();
            } else pending_operation_ = true;
        }
    }
    return 0;
}
void LibnxSession::Close() {
    if (!initialized_) return;
    const auto cleanup = [&](const char *operation, Result rc) {
        if (R_FAILED(rc)) Log("cleanup %s failed: Result=%08lx", operation, static_cast<unsigned long>(rc));
    };
    cleanup("stop own filtered scan", StopOwnScan());
    for (std::size_t i = 0; i < subscription_count_; ++i) {
        const auto &r = subscriptions_[i]; const auto service = Id(r.service), characteristic = Id(r.characteristic);
        if (identity_test_ && R_FAILED(CheckCurrentTarget())) {
            Log("cleanup report deregistration skipped: configured address/handle no longer bound; no operation on a reassigned handle");
            break;
        }
        cleanup("deregister own report", btLeClientDeregisterNotification(r.connection, r.service.primary, &service, &characteristic));
    }
    subscription_count_ = 0;
    if (link_only_) {
        if (link_attempted_) Log("LINK_ONLY close: no MAC-only disconnect/adoption; pending marker retained; reboot required regardless of result");
    } else if (have_connection_) cleanup("disconnect target", btdevDisconnectFromGattServer(connection_));
    else if (connection_requested_) {
        // Catch a request that completed after our last poll. A still-pending
        // request cannot be cancelled by address with public btdev APIs.
        std::array<BtdrvBleConnectionInfo, 16> connections{}; u8 count = 0;
        if (R_SUCCEEDED(btdevGetBleConnectionInfoList(connections.data(), connections.size(), &count)) && count < connections.size())
            for (std::size_t i = 0; i < count; ++i)
                if (!std::memcmp(&connections[i].addr, &target_, sizeof target_)) cleanup("disconnect late target", btdevDisconnectFromGattServer(connections[i].connection_handle));
    }
    const auto hid = Uuid(0x1812), information = Uuid(0x180a);
    if (information_path_) cleanup("unregister information path", btdevUnregisterGattOperationNotification(&information));
    if (hid_path_) cleanup("unregister HID path", btdevUnregisterGattOperationNotification(&hid));
    if (event_open_) eventClose(&operation_event_);
    if (discovery_open_) eventClose(&discovery_event_);
    if (scan_event_open_) eventClose(&scan_event_);
    if (connection_event_open_) eventClose(&connection_event_);
    if (pairing_event_open_) eventClose(&pairing_event_);
    if (btdev_initialized_) btdevExit(); else btmuExit();
    ResetInput();
    initialized_ = event_open_ = discovery_open_ = connection_requested_ = have_connection_ = information_path_ = hid_path_ = false;
    scan_event_open_ = connection_event_open_ = pairing_event_open_ = authenticated_reads_ = false;
    btdev_initialized_ = false;
    // Do not erase a failed-stop ownership marker before logging it.
    if (scanning_) Log("WARNING: owned scan stop failed during exit; another scan must not be started until system recovery");
}
}
