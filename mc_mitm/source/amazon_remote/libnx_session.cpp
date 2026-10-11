// Independently implemented public libnx bt/btmu integration. GPL-2.0-only.
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "libnx_session.hpp"
#ifdef ARMODULE_WAKE_A3
#include <new>
#endif
#ifdef ARMODULE_CENTRAL_BOND_B1
#include "central_bond_wire.hpp"
#endif
#include "../../../tools/amazon-remote-pairing-audit/backend.hpp"
#include "cached_characteristic_getter.hpp"
#include "k7q3m7_descriptor.hpp"
#include "diagnostic_log.hpp"
#include "ble_preflight.hpp"
#include "scan_cache_census.hpp"
#include "input_diagnostic_policy.hpp"
#include "service_uuid_census.hpp"
#ifdef ARMODULE_CONNECT_DISPATCH
#include "connect_dispatch.hpp"
#endif
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace amazon_remote {
namespace {
#ifdef ARMODULE_RETRY_A5
using RetryConnectionEvent=decltype(BtdrvBleEventInfo{}.client_connection);
static_assert(BtdrvBleEventType_ClientConnection==4 && sizeof(RetryConnectionEvent)==20);
static_assert(offsetof(RetryConnectionEvent,result)==0 && offsetof(RetryConnectionEvent,status)==4 &&
    offsetof(RetryConnectionEvent,client_if)==5 && offsetof(RetryConnectionEvent,conn_id)==8 &&
    offsetof(RetryConnectionEvent,address)==12 && offsetof(RetryConnectionEvent,reason)==18);
#endif
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
LibnxSession::LibnxSession(Logger logger, bool fingerprint_only, bool input_diagnostic, bool system_gatt, bool native_requests, bool sustained_input, bool system_enrollment)
    : system_enrollment_(system_enrollment), client_(K7q3m7Descriptor, sizeof K7q3m7Descriptor, fingerprint_only), logger_(logger),
      gatt_(system_gatt && fingerprint_only && input_diagnostic),
      native_requests_(native_requests && system_gatt && fingerprint_only && input_diagnostic),
      identity_test_(fingerprint_only), input_diagnostic_(fingerprint_only && input_diagnostic),
      input_budget_(sustained_input && native_requests_
#ifdef ARMODULE_AUTOMATIC_A1
        , true
#endif
      ) {}
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
    rc=gatt_.Open();trace(gatt_.System()?"btmInitialize(system GATT; no radio initialization)":"foreground GATT path",rc);
    if (R_FAILED(rc)) { Close();return rc; }
    if (native_requests_) {
        rc=native_.Open();trace("smGetService(btdrv requests ONLY; no driver initialization)",rc);
        if (R_FAILED(rc)) { Close();return rc; } native_open_=true;
        Log("Native GATT request path: ARUID-free public driver commands; per-client bt event queue retained, routing UNVERIFIED. No global driver event reads.");
    }
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
#ifdef ARMODULE_WAKE_A3
    if(retired_count_){
        // Drain only this freshly opened client's queue, never the driver's
        // shared event stream. No old event can satisfy a new pending request.
        unsigned discarded{};
        for(;;){
            const auto event_rc=eventWait(&operation_event_,0);
            if(Timeout(event_rc))break;
            if(R_FAILED(event_rc) || discarded>=32){Close();return R_FAILED(event_rc)?event_rc:Invalid();}
            BtdrvBleEventInfo old{};BtdrvBleEventType type{};
            const auto read_rc=btGetLeEventInfo(&old,sizeof old,&type);
            if(R_FAILED(read_rc)){Close();return read_rc;}
            ++discarded;
        }
        Log("WAKE_A3_CLIENT_QUEUE drained=%u before any new request; new fingerprint/security/subscriptions required",discarded);
    }
#endif
    link_only_ = true;
    if (input_diagnostic_) {
#ifdef ARMODULE_CENTRAL_BOND_B1
        auto bond_rc=smGetService(&bond_service_,"mc");
        central_bond::Status state{};
        if(R_SUCCEEDED(bond_rc))bond_rc=serviceDispatch(&bond_service_,central_bond::StatusCommand,
            .buffer_attrs={SfBufferAttr_HipcPointer|SfBufferAttr_Out},.buffers={{&state,sizeof state}});
        if(R_FAILED(bond_rc) || state.magic!=central_bond::Magic || state.version!=1 ||
           (state.store!=central_bond::Store::Empty && state.store!=central_bond::Store::Saved)){
            Close();return R_FAILED(bond_rc)?bond_rc:Invalid();
        }
        bond_saved_=state.store==central_bond::Store::Saved;
        Log("B1_PRIVATE_STORE=%s; no key bytes in module or SD",bond_saved_?"SAVED":"EMPTY");
#if defined(ARMODULE_SAVED_DISCOVERY_B3) || defined(ARMODULE_NATIVE_PEER_C1)
        if(!bond_saved_){Log("B3_SAVED_BOND_REQUIRED: no discovery or enrollment fallback");Close();return Invalid();}
#endif
#ifdef ARMODULE_NATIVE_PEER_C1
        if(!state.generation){Close();return Invalid();}
        peer_generation_=state.generation;
#endif
#ifdef ARMODULE_NATIVE_HCI_C2
        central_bond::HciTrace trace{};
        bond_rc=serviceDispatch(&bond_service_,central_bond::HciTraceCommand,
            .buffer_attrs={SfBufferAttr_HipcPointer|SfBufferAttr_Out},.buffers={{&trace,sizeof trace}});
        if(R_FAILED(bond_rc) || trace.magic!=central_bond::HciTraceMagic || trace.version!=1 ||
           trace.supported!=1 || trace.window_nonce || trace.count || trace.dropped || trace.capture_start_ms){
            Log("C2_TRACE_COMPANION_NOT_READY: refusing new connection");Close();return R_FAILED(bond_rc)?bond_rc:Invalid();
        }
        Log("C2_TRACE_COMPANION_READY: received command-status metadata only; no radio call");
#endif
#endif
        auto pairing_rc = btdevAcquireBlePairingEvent(&pairing_event_);
        Log("API=btdevAcquireBlePairingEvent(input diagnostic) Result=%08lx; generic hints are not target/bond proof", static_cast<unsigned long>(pairing_rc));
        if (R_FAILED(pairing_rc)) { Close(); return pairing_rc; }
        pairing_event_open_ = true;
        Log("INPUT_DIAGNOSTIC: exact fingerprint first; one NoMitm-requested map read; bounded ReportReference/CCC-only continuation. No explicit bond/PIN/SSP replies, vendor writes or controller output.");
    } else Log("FINGERPRINT_ONLY user GATT paths registered; only PnP/report-map reads AuthNone allowed; no pairing, CCC writes, notifications or output");
    return 0;
}
Result LibnxSession::StartLinkOnlyScan(std::uint64_t now) {
#ifdef ARMODULE_NATIVE_PEER_C1
    // This separately built saved-peer test never falls back to discovery.
    (void)now;
    return Invalid();
#else
#ifdef ARMODULE_KNOWN_RECONNECT_CHECK
    if (known_reconnect_ready_ms_) return Invalid();
#endif
    if (!initialized_ || !link_only_ || link_attempted_ || scanning_ || !now) return Invalid();
    auto rc = btmuAcquireBleConnectionEvent(&connection_event_);
    Log("API=btmuAcquireBleConnectionEvent(link-only) Result=%08lx", static_cast<unsigned long>(rc));
    if (R_FAILED(rc)) return rc;
    connection_event_open_ = true;
    rc = btmuAcquireBleScanEvent(&scan_event_);
    Log("API=btmuAcquireBleScanEvent(link-only) Result=%08lx", static_cast<unsigned long>(rc));
    if (R_FAILED(rc)) return rc;
    scan_event_open_ = true;
#ifdef ARMODULE_MANUFACTURER_SCAN_CHECK
    // Exact first eight manufacturer bytes retained from the configured
    // K7Q3M7's captured AD. No wildcard, native Nintendo profile or bonding.
    BtdrvBleAdvertisePacketParameter parameter{}; parameter.company_id=0x0171;
    constexpr u8 pattern[]{0x04,0x27,0x02,0x7d,0,0};
    std::copy_n(pattern,6,parameter.pattern_data);
    rc=btmuStartBleScanForGeneral(parameter);
    Log("API=btmuStartBleScanForGeneral(exact captured manufacturer71010427027d0000) Result=%08lx; ordinary advertisement compatibility UNVERIFIED",static_cast<unsigned long>(rc));
#else
    BtdrvGattAttributeUuid uuid{}; uuid.size = HidServiceUuid128.size();
    std::copy(HidServiceUuid128.begin(), HidServiceUuid128.end(), uuid.uuid);
    rc = btmuStartBleScanForSmartDevice(&uuid);
    Log("API=btmuStartBleScanForSmartDevice(HID128 link-only) Result=%08lx; HID16 matching remains unverified", static_cast<unsigned long>(rc));
#endif
    if (R_FAILED(rc)) return rc;
    scanning_ = true; scan_started_ms_ = armTicksToNs(armGetSystemTick()) / 1000000;
    last_scan_poll_ms_ = last_connection_poll_ms_ = 0;
    general_cache_seen_ = smart_cache_seen_ = general_target_dumped_ = smart_target_dumped_ = false;
    Log("A starts scan only; physical ZL plus fresh target-only managed sighting required for one link request; no cache auto-connect");
    return 0;
#endif
}
Result LibnxSession::ConfirmObservedLink(const SightingWire &sighting, std::uint64_t now, std::uint64_t capture_deadline) {
    if (!initialized_ || !link_only_ || !scanning_ || link_attempted_ || connection_requested_ || have_connection_) return Invalid();
#ifdef ARMODULE_CONNECT_DISPATCH
    // Do not grant freshness from a caller's older loop timestamp.
    now = armTicksToNs(armGetSystemTick()) / 1000000;
#endif
    const auto assessment = AssessSighting(sighting, CensusTarget(target_), scan_started_ms_, now);
    Log("Physical ZL sighting gate assessment=%u captured_boot_ms=%llu scan_start=%llu now=%llu", unsigned(assessment),
        static_cast<unsigned long long>(sighting.captured_boot_ms), static_cast<unsigned long long>(scan_started_ms_), static_cast<unsigned long long>(now));
    if (assessment != SightingAssessment::FreshCandidate) return 0;
    if (identity_test_ && (capture_deadline <= now || capture_deadline-now > 60000)) return Invalid();
    capture_deadline_ms_ = capture_deadline;
    const auto marker_result = PersistLinkMarker(now);
    if (R_FAILED(marker_result)) return marker_result;
    return RequestConnection(&sighting, now);
}
Result LibnxSession::PersistLinkMarker(std::uint64_t now) {
    // Conservatively block repeat attempts across process exit/relaunch. Only
    // the operator may remove this file after a confirmed physical reboot.
    link_attempted_ = true;
#ifdef ARMODULE_AUTOMATIC_A1
    char attempt_path[160]{};
    std::snprintf(attempt_path, sizeof attempt_path,
        "sdmc:/config/amazon-remote-auto-a1/attempt-%llu.txt",
        static_cast<unsigned long long>(armGetSystemTick()));
    FILE *marker = std::fopen(attempt_path, "wx");
#else
    FILE *marker = std::fopen("sdmc:/config/amazon-remote/link-test-pending.txt", "wx");
#endif
    if (!marker) { Log("Pending marker could not be created exclusively; FAIL CLOSED, no connection IPC, reboot/recovery required"); return Invalid(); }
    const bool wrote = std::fprintf(marker, "prototype=0.1.10 input_diagnostic=%u boot_ms=%llu\nReboot must be confirmed before operator removes this marker.\n", input_diagnostic_, static_cast<unsigned long long>(now)) > 0;
    const bool flushed = std::fflush(marker) == 0;
    const bool synced = ::fsync(::fileno(marker)) == 0;
    const bool closed = std::fclose(marker) == 0;
    if (!wrote || !flushed || !synced || !closed) { Log("Pending marker persistence failed; FAIL CLOSED, no connection IPC, marker retained"); return Invalid(); }
#ifdef ARMODULE_RETRY_A5
    Log("RETRY_A5 attempt journal retained; only observed early terminal failure may permit another request; pending timeouts cannot retry");
#else
    Log("Persistent pending marker retained on ALL outcomes; no public address-cancel API; physical reboot required after this test");
#endif
    return 0;
}
#if defined(ARMODULE_KNOWN_RECONNECT_CHECK) || defined(ARMODULE_NATIVE_PEER_C1)
Result LibnxSession::PrepareKnownReconnect(std::uint64_t now,[[maybe_unused]] std::uint64_t deadline) {
    const auto actual = armTicksToNs(armGetSystemTick()) / 1000000;
    if (!initialized_ || !link_only_ || !identity_test_ || !input_diagnostic_ ||
        !native_requests_ || !native_open_ || !gatt_.SystemOpen() || scanning_ ||
        connection_event_open_ || link_attempted_ || connection_requested_ || have_connection_ ||
        known_reconnect_ready_ms_ || !now || now > actual || actual - now > 3000 ||
        !ValidCensusTarget(CensusTarget(target_))) return Invalid();
#ifdef ARMODULE_NATIVE_PEER_C1
    if(!bond_saved_ || !peer_generation_ || peer_nonce_ || deadline<=actual || deadline-actual>60000)return Invalid();
#endif
    const auto rc = btmuAcquireBleConnectionEvent(&connection_event_);
    Log("API=btmuAcquireBleConnectionEvent(known reconnect, no scan) Result=%08lx", static_cast<unsigned long>(rc));
    if (R_FAILED(rc)) return rc;
    connection_event_open_ = true;
#ifdef ARMODULE_NATIVE_PEER_C1
    peer_started_ms_=actual;peer_deadline_ms_=deadline;
    peer_nonce_=static_cast<std::uint32_t>(actual)^0x31505241u;if(!peer_nonce_)peer_nonce_=1;
    struct {u64 placeholder_pid;central_bond::PreparePeer request;} request{0,{peer_nonce_,0,deadline}};
    const auto result=serviceDispatchIn(&bond_service_,central_bond::PreparePeerCommand,request,.in_send_pid=true);
    Log("C1_PREPARE_REQUEST Result=%08lx; saved generation=%llu; queue acceptance is NOT native readiness",static_cast<unsigned long>(result),static_cast<unsigned long long>(peer_generation_));
    return result;
#else
    known_reconnect_ready_ms_ = armTicksToNs(armGetSystemTick()) / 1000000;
    Log("KNOWN_RECONNECT_PREPARED: configured address only; no scan, cache adoption, bond request or connection yet. Physical ZL required; fingerprint and input guards unchanged.");
    return known_reconnect_ready_ms_ ? 0 : Invalid();
#endif
}
Result LibnxSession::ConfirmKnownReconnect(std::uint64_t now, std::uint64_t deadline) {
    const auto actual = armTicksToNs(armGetSystemTick()) / 1000000;
#ifdef ARMODULE_NATIVE_PEER_C1
    const auto peer_rc=PollPeerPreparation(actual,true);
    if(R_FAILED(peer_rc) || deadline!=peer_deadline_ms_)return R_FAILED(peer_rc)?peer_rc:Invalid();
#endif
    if (!initialized_ || !link_only_ || !KnownReconnectPrepared() || scanning_ ||
        connection_requested_ || have_connection_ || now < known_reconnect_ready_ms_ ||
        now - known_reconnect_ready_ms_ >= 60000 || now > actual || actual - now > 3000 ||
        deadline <= actual || deadline - known_reconnect_ready_ms_ > 60000) return Invalid();
    capture_deadline_ms_ = deadline;
#ifdef ARMODULE_NATIVE_PEER_C1
    Log("C1_OPERATOR_CONNECT: physical ZL, native BLE identity readback ready; remote presence/encryption unproven. No pairing fallback.");
#else
    Log("KNOWN_RECONNECT_OPERATOR: physical ZL requested one configured-address connection. Presence/bonding unproven; no discovery candidate required. No native/managed bond action.");
#endif
    const auto rc = PersistLinkMarker(now);
    return R_FAILED(rc) ? rc : RequestConnection();
}
#endif
#ifdef ARMODULE_NATIVE_PEER_C1
Result LibnxSession::PollPeerPreparation(std::uint64_t now,bool force){
    if(!peer_nonce_ || !bond_saved_ || !peer_generation_ || now<peer_started_ms_ || now>=peer_deadline_ms_)return Invalid();
    if(!force && peer_poll_ms_ && now-peer_poll_ms_<100)return 0;
    peer_poll_ms_=now;
    central_bond::PeerStatus s{};
    auto rc=serviceDispatch(&bond_service_,central_bond::PeerStatusCommand,
        .buffer_attrs={SfBufferAttr_HipcPointer|SfBufferAttr_Out},.buffers={{&s,sizeof s}});
    if(R_FAILED(rc))return rc;
    if(s.magic!=central_bond::PeerMagic || s.version!=central_bond::PeerVersion || s.nonce!=peer_nonce_ ||
       s.generation!=peer_generation_ || s.capture_deadline_ms!=peer_deadline_ms_ ||
       s.started_ms<peer_started_ms_ || s.started_ms>now)return Invalid();
    if(s.phase==central_bond::PeerPhase::Failed || s.phase==central_bond::PeerPhase::Cancelled){
        Log("C1_PREPARE_FAILED phase=%u Result=%08lx existed=%u type=%u address_type=%u",unsigned(s.phase),static_cast<unsigned long>(s.result),s.existed,s.device_type,s.address_type);
        return s.result?s.result:Invalid();
    }
    if(s.phase==central_bond::PeerPhase::Ready){
        if(s.result || s.device_type!=2 || s.address_type!=0 || s.existed>1)return Invalid();
        if(!known_reconnect_ready_ms_){
            known_reconnect_ready_ms_=now;
            Log("C1_NATIVE_PEER_READY existed_before=%u type=BLE2 address_type=PUBLIC0; no connection or security request yet",s.existed);
        }
        return 0;
    }
    if(known_reconnect_ready_ms_ || now-peer_started_ms_>=3000 ||
       (s.phase!=central_bond::PeerPhase::Queued && s.phase!=central_bond::PeerPhase::Waiting))return Invalid();
    return 0;
}
#endif
#ifdef ARMODULE_NATIVE_HCI_C2
void LibnxSession::PollHciTrace(std::uint64_t now,bool force){
    if(hci_read_failed_ || !peer_nonce_ || (!force && hci_polled_ms_ && now>=hci_polled_ms_ && now-hci_polled_ms_<100))return;
    hci_polled_ms_=now;
    central_bond::HciTrace trace{};
    const auto rc=serviceDispatch(&bond_service_,central_bond::HciTraceCommand,
        .buffer_attrs={SfBufferAttr_HipcPointer|SfBufferAttr_Out},.buffers={{&trace,sizeof trace}});
    // The companion can receive a response during this IPC. Bound timestamps
    // by the clock after the read, not the older event-loop snapshot.
    const auto read_ms=armTicksToNs(armGetSystemTick())/1000000;
    bool valid=R_SUCCEEDED(rc) && read_ms>=now && trace.magic==central_bond::HciTraceMagic && trace.version==1 &&
        trace.supported==1 && trace.count<=4 && trace.count>=hci_logged_ &&
        (!trace.count || (trace.window_nonce==peer_nonce_ && trace.capture_start_ms>=peer_started_ms_ && trace.capture_start_ms<=read_ms));
    for(unsigned i=0;valid && i<trace.count;++i){const auto &e=trace.entries[i];
        valid=e.status<=255 && e.own_address_type<=3 && e.boot_ms>=trace.capture_start_ms && e.boot_ms<=read_ms &&
            (!i || e.boot_ms>=trace.entries[i-1].boot_ms);
    }
    if(!valid){hci_read_failed_=true;Log("C2_HCI_TRACE_UNAVAILABLE Result=%08lx; diagnostic failure only, connection behavior unchanged",static_cast<unsigned long>(rc));return;}
    while(hci_logged_<trace.count){const auto &e=trace.entries[hci_logged_++];
        Log("C2_HCI_COMMAND_STATUS index=%u boot_ms=%llu opcode=200d status=%02x own_address_type=%u target_command_matched=1 window_nonce=%u; command_status_not_link_or_bond_proof=1",
            hci_logged_,static_cast<unsigned long long>(e.boot_ms),e.status,e.own_address_type,trace.window_nonce);
    }
}
#endif
Result LibnxSession::Connect(bool filtered_discovery, bool authenticated_reads) {
#ifdef ARMODULE_NATIVE_PEER_C1
    // C1 admits connection only through its prepared-peer physical-ZL path.
    (void)filtered_discovery;(void)authenticated_reads;
    return Invalid();
#else
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
#endif
}
Result LibnxSession::RequestConnection(const SightingWire *sighting, [[maybe_unused]] std::uint64_t accepted_ms) {
    if (!initialized_ || connection_requested_ || have_connection_) return Invalid();
    [[maybe_unused]] std::uint64_t dispatch_ms{};
    // Do not adopt or later disconnect another client's pre-existing target.
    std::array<BtdrvBleConnectionInfo, 16> connections{}; u8 count = 0;
    auto rc = link_only_ ? btmuBleGetConnectionState(connections.data(), connections.size(), &count) : btdevGetBleConnectionInfoList(connections.data(), connections.size(), &count);
    Log("API=connection-list precheck Result=%08lx count=%u", static_cast<unsigned long>(rc), count);
    if (R_FAILED(rc)) return rc;
    if (count >= connections.size()) return Invalid();
    for (std::size_t i = 0; i < count; ++i)
        if (!std::memcmp(&connections[i].addr, &target_, sizeof target_)) return Invalid();
    // A successful empty user view does not exclude a system-only target.
    // Post1 observed exactly that after our user client closed. Borrow the
    // already-open system GATT client; never open another client or adopt,
    // disconnect or rewrite a pre-existing native connection.
    if (gatt_.SystemOpen()) {
        connections.fill({}); count = 0;
        rc = btmBleGetConnectionState(connections.data(), connections.size(), &count);
        Log("API=system connection-list precheck Result=%08lx count=%u", static_cast<unsigned long>(rc), count);
        if (R_FAILED(rc)) return rc;
        if (count >= connections.size()) return Invalid();
        for (std::size_t i = 0; i < count; ++i)
            if (!std::memcmp(&connections[i].addr, &target_, sizeof target_)) {
                Log("PREEXISTING_SYSTEM_TARGET: no connect or adoption; pending marker retained");
                return Invalid();
            }
    }
    // Discard a previously signalled discovery indication before our connect.
    // BTM's discovery event carries no connection identity; coexistence remains
    // explicitly unsupported with another client registering these UUID paths.
    if (!link_only_ || identity_test_) {
        rc = eventWait(&discovery_event_, 0);
        if (R_FAILED(rc) && !Timeout(rc)) return rc;
    }
    discovery_signalled_ = false;
    if (link_only_) {
        // File flush, IPC and logging may have blocked since physical ZL.
        // Recheck the lease and dispatch budget immediately before the request.
        dispatch_ms = armTicksToNs(armGetSystemTick()) / 1000000;
#if defined(ARMODULE_KNOWN_RECONNECT_CHECK) || defined(ARMODULE_NATIVE_PEER_C1)
        if (known_reconnect_ready_ms_) {
#ifdef ARMODULE_NATIVE_PEER_C1
            const auto ready_rc=PollPeerPreparation(dispatch_ms,true);
            if(R_FAILED(ready_rc))return ready_rc;
            dispatch_ms=armTicksToNs(armGetSystemTick())/1000000;
#endif
            if (sighting || !link_attempted_ || dispatch_ms < known_reconnect_ready_ms_ ||
                dispatch_ms - known_reconnect_ready_ms_ >= 60000 || dispatch_ms >= capture_deadline_ms_) {
                Log("Known reconnect deadline/clock invalid; NO connection IPC; marker retained");
                return Invalid();
            }
        } else
#endif
        if (!sighting || (identity_test_ && dispatch_ms >= capture_deadline_ms_) ||
#ifdef ARMODULE_CONNECT_DISPATCH
            !ValidConnectDispatch(*sighting, CensusTarget(target_), scan_started_ms_, accepted_ms, dispatch_ms)) {
            Log("P3_DISPATCH_REFUSED accepted_ms=%llu dispatch_ms=%llu budget_ms=%llu", static_cast<unsigned long long>(accepted_ms),
                static_cast<unsigned long long>(dispatch_ms), static_cast<unsigned long long>(ConnectDispatchBudgetMs));
#else
            AssessSighting(*sighting, CensusTarget(target_), scan_started_ms_, dispatch_ms) != SightingAssessment::FreshCandidate) {
#endif
            Log("Final presence gate expired/invalid; NO connection IPC; pending marker retained, reboot/recovery required");
            return Invalid();
        }
    }
    // btmu performs connection/client management; no private MissionControl BLE code.
#ifdef ARMODULE_RETRY_A5
    if(!early_retry_configured_) {
        if(!early_retry_.Configure(peer_nonce_,peer_started_ms_,capture_deadline_ms_))return Invalid();
        early_retry_configured_=true;
    }
    ConnectionEvidenceWire evidence{};central_bond::HciTrace hci{};
    rc=ReadRetryEvidence(evidence,hci);if(rc)return rc;
    dispatch_ms=armTicksToNs(armGetSystemTick())/1000000;
    if(!early_retry_.Begin(evidence,hci,dispatch_ms))return Invalid();
    early_request_completed_=false;early_settling_=false;
#endif
    rc = link_only_ ? btmuBleConnect(target_) : btdevConnectToGattServer(target_);
#ifdef ARMODULE_RETRY_A5
    Log("RETRY_A5_CONNECT attempt=%u Result=%08lx; same saved peer; no new pairing",early_retry_.Attempts(),static_cast<unsigned long>(rc));
#endif
#ifdef ARMODULE_CONNECT_DISPATCH
    // Log only after IPC. A log flush must not consume the checked budget.
    if (sighting) Log("P3_DISPATCH_REQUEST accepted_ms=%llu captured_ms=%llu dispatch_ms=%llu budget_ms=%llu", static_cast<unsigned long long>(accepted_ms),
        static_cast<unsigned long long>(sighting->captured_boot_ms), static_cast<unsigned long long>(dispatch_ms),
        static_cast<unsigned long long>(ConnectDispatchBudgetMs));
#endif
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
#ifdef ARMODULE_MANUFACTURER_SCAN_CHECK
    const auto rc=link_only_ ? btmuStopBleScanForGeneral() : btdevStopBleScanSmartDevice();
    Log("API=%s(own successful start) Result=%08lx",link_only_ ? "btmuStopBleScanForGeneral" : "btdevStopBleScanSmartDevice",static_cast<unsigned long>(rc));
#else
    const auto rc = link_only_ ? btmuStopBleScanForSmartDevice() : btdevStopBleScanSmartDevice();
    Log("API=btdevStopBleScanSmartDevice(own successful start) Result=%08lx", static_cast<unsigned long>(rc));
#endif
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
#ifdef ARMODULE_MANUFACTURER_SCAN_CHECK
        if(link_only_ && (R_FAILED(general_rc) || !ValidScanCount(count))) {
            Log("MANUFACTURER_SCAN_GENERAL_GETTER_REFUSED Result=%08lx count=%u; no cache admission",static_cast<unsigned long>(general_rc),count);
            return R_FAILED(general_rc) ? general_rc : Invalid();
        }
#endif
        // General is diagnostics ONLY: never connects/adopts based on that cache.
        results.fill({}); count = 0;
        const auto rc = btmuGetBleScanResultsForSmartDevice(results.data(), results.size(), &count);
        LogScanCacheSummary("smart", rc, count, results.data(), sizeof results);
#ifdef ARMODULE_MANUFACTURER_SCAN_CHECK
        // In S2 link-only mode smart is a secondary census, not the started
        // scan's getter. Neither bucket authorizes a connection.
        if(!link_only_) {
#endif
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
#ifdef ARMODULE_MANUFACTURER_SCAN_CHECK
        }
#endif
    }
    if (!connection_requested_ && now - scan_started_ms_ >= (identity_test_ ? 60000u : 20000u)) {
#ifdef ARMODULE_MANUFACTURER_SCAN_CHECK
        if(link_only_) Log("Manufacturer scan INCONCLUSIVE: target not observed; ordinary advertisement compatibility with the captured8-byte filter is unverified. Not proof remote is absent or BLE disabled.");
        else
#endif
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
    rc = gatt_.Services(connection, services.data(), services.size(), &service_count);
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
        rc = gatt_.Characteristics(&service, characteristics.data(), characteristics.size(), &characteristic_count);
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
            rc = gatt_.Descriptors(&characteristic, descriptors.data(), descriptors.size(), &descriptor_count);
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
    rc = gatt_.Services(connection, services.data(), services.size(), &count);
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
#ifdef ARMODULE_NAV17
        LogServiceUuid(s,i,"fingerprint");
#endif
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
        rc = gatt_.Characteristics(native, characteristics.data(), characteristics.size(), &char_count);
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
        return !input_budget_.Expired(linked_ms_,armTicksToNs(armGetSystemTick()) / 1000000) ? 0 : Invalid();
    };
    auto rc = binding(); if (R_FAILED(rc)) return rc;
    std::array<BtmGattCharacteristic,100> characteristics{}; u8 count = 0;
    rc = gatt_.RawCharacteristics(connection_, hid.handle, characteristics.data(), characteristics.size(), &count);
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
#ifdef ARMODULE_HID_INFO_H1
    // Reuse the existing post-security cache query. This does not read any value
    // yet and never changes which Report endpoints the normal path admits.
    std::array<Attribute,100> info_records{};
    bool info_bounds = true;
    for (std::size_t i = 0; i < count; ++i) {
        const auto &c = characteristics[i];
        if (ShortUuid(c.uuid) == 0x2a4a && c.instance_id > 255) info_bounds = false;
        info_records[i] = {Kind::Characteristic,c.handle,0,ShortUuid(c.uuid),c.properties,
                           static_cast<u8>(c.instance_id),false};
    }
    hid_information_ = {};
    if (!info_bounds || !SelectHidInformation(hid,info_records.data(),count,hid_information_))
        Log("HID_INFO_H1_UNAVAILABLE: unique bounded 2a4a not observed; no diagnostic read");
#endif
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
        rc = gatt_.RawDescriptors(connection_, c.handle, descriptors.data(), descriptors.size(), &descriptor_count);
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
        if (R_FAILED(rc) || !client_.Tick(current) || (input_diagnostic_ && input_budget_.Expired(linked_ms_,current))) return R_FAILED(rc) ? rc : Invalid();
    }
    if (native_requests_) {
        Result rc=Invalid();const char *api="invalid";
        switch (r.operation) {
            case Operation::ReadCharacteristic: api="btdrv ReadGattCharacteristic90";rc=native_.ReadCharacteristic(r.connection,r.service.primary,&service,&characteristic,auth);break;
            case Operation::ReadDescriptor: api="btdrv ReadGattDescriptor91";rc=native_.ReadDescriptor(r.connection,r.service.primary,&service,&characteristic,&descriptor,auth);break;
            case Operation::RegisterNotification: api="btdrv RegisterGattNotification94";rc=native_.Notify(r.connection,r.service.primary,&service,&characteristic,true);break;
            case Operation::WriteDescriptor: api="btdrv WriteGattDescriptor93 CCC0100 only";rc=native_.WriteCcc(r.connection,r.service.primary,&service,&characteristic,&descriptor,r.value.data(),r.size,auth);break;
        }
        Log("API=%s Result=%08lx",api,static_cast<unsigned long>(rc));return rc;
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
#ifdef ARMODULE_LATENCY_A6
    // Host dequeue timing, not an air-interface or physical-button timestamp.
    const auto received_ms=armTicksToNs(armGetSystemTick())/1000000;
#endif
    // Raw event preserves original status and size; SDK helper otherwise clamps size.
    BtdrvBleEventInfo event{}; BtdrvBleEventType type{};
    auto rc = btGetLeEventInfo(&event, sizeof event, &type); if (R_FAILED(rc)) return rc;
    if (type != BtdrvBleEventType_ClientNotify) return Invalid();
    const auto &op = event.client_notify;
    if (op.conn_id != connection_) return 0;
    if (identity_test_) {
        if ((op.type & 4) && (!input_diagnostic_ || !Ready())) return 0;
        rc = CheckCurrentTarget(); if (R_FAILED(rc)) return rc;
        if (input_diagnostic_ && input_budget_.Expired(linked_ms_,armTicksToNs(armGetSystemTick()) / 1000000)) return Invalid();
    }
    Log("user GATT event conn=%u result=%u flags=%02x service=%04x char=%04x desc=%04x size=%u", op.conn_id, op.result, op.type,
        ShortUuid(op.serv_uuid), ShortUuid(op.char_uuid), ShortUuid(op.desc_uuid), op.size);
    if (op.size > sizeof op.data) return Invalid();
#ifdef ARMODULE_HID_INFO_H1
    if (CompleteHidInformation(event)) return 0;
#endif
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
#ifdef ARMODULE_LATENCY_A6
        if(input_timing_.sequence!=UINT64_MAX)++input_timing_.sequence;
        input_timing_.received_ms=received_ms;
        input_timing_.decoded_ms=armTicksToNs(armGetSystemTick())/1000000;
        input_timing_.buttons=decoder_.Buttons();
#endif
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
        if (!client_.Tick(current) || (input_diagnostic_ && input_budget_.Expired(linked_ms_,current))) return Invalid();
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
#ifdef ARMODULE_HID_INFO_H1
    // Once per process, not once per reconnect. Retire pending observations at
    // every ownership boundary; never carry evidence into a new connection.
    hid_information_pending_ = false;
    hid_information_ = {};
    hid_information_ready_ms_ = 0;
#endif
    if (decoder_.Buttons()) { char delta[240]{}; FormatButtons(delta, sizeof delta, decoder_.Buttons(), 0); Log("reset buttons %s", delta); }
    decoder_.Disconnect(); client_.Disconnect(); pending_operation_ = false; cache_loaded_ = mtu_requested_ = discovery_signalled_ = false;
    input_budget_.Reset();connection_log_gate_.Reset();input_ready_logged_=false;
}
Result LibnxSession::Poll(std::uint64_t now) {
    if (!initialized_ || disconnect_state_ != DisconnectState::NotStarted) return Invalid();
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
        rc = gatt_.ConfigureMtu(connection_, 0x200);
        if (R_FAILED(rc)) return rc;
        mtu_requested_ = true;
    }
    u16 mtu = 0; rc = gatt_.GetMtu(connection_, &mtu);
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
#ifdef ARMODULE_RETRY_A5
Result LibnxSession::ReadRetryEvidence(ConnectionEvidenceWire &events,central_bond::HciTrace &hci) {
    if(!serviceIsActive(&bond_service_))return Invalid();
    auto rc=serviceDispatch(&bond_service_,65004,
        .buffer_attrs={SfBufferAttr_HipcPointer|SfBufferAttr_Out},.buffers={{&events,sizeof events}});
    if(rc)return rc;
    return serviceDispatch(&bond_service_,central_bond::HciTraceCommand,
        .buffer_attrs={SfBufferAttr_HipcPointer|SfBufferAttr_Out},.buffers={{&hci,sizeof hci}});
}
Result LibnxSession::ReadTargetAbsent(bool &absent) {
    absent=false;if(!initialized_ || !gatt_.SystemOpen())return Invalid();
    bool found=false;
    for(bool system:{true,false}) {
        std::array<BtdrvBleConnectionInfo,16> entries{};u8 count{};
        const auto rc=system?btmBleGetConnectionState(entries.data(),entries.size(),&count):
            btmuBleGetConnectionState(entries.data(),entries.size(),&count);
        if(rc || count>=entries.size())return rc?rc:Invalid();
        for(unsigned i=0;i<count;++i)if(!std::memcmp(&entries[i].addr,&target_,sizeof target_))found=true;
    }
    absent=!found;return 0;
}
Result LibnxSession::PollEarlyLinkRetry(std::uint64_t now,bool &settling) {
    settling=early_settling_;
    if(!connection_requested_ || have_connection_)return 0;
    if(now<early_polled_)return Invalid();
    if(early_polled_ && now-early_polled_<100)return 0;
    early_polled_=now;
    if(!early_retry_configured_ || bond_started_ || fingerprint_logged_ || cache_loaded_ ||
       inputs_loaded_ || subscription_count_ || pending_operation_ || scanning_)return Invalid();
    ConnectionEvidenceWire events{};central_bond::HciTrace hci{};
    auto rc=ReadRetryEvidence(events,hci);if(rc)return rc;
    now=armTicksToNs(armGetSystemTick())/1000000;
    const auto decision=early_retry_.Observe(events,hci,now);
    if(decision==EarlyLinkRetry::Decision::Refuse) {
        Log("RETRY_A5_REFUSED: ambiguous/unsupported callback evidence; no retry or cancellation");return Invalid();
    }
    if(decision==EarlyLinkRetry::Decision::Waiting)return 0;
    settling=early_settling_=true;
    if(early_logged_failure_!=early_retry_.FailureTime()) {
        early_logged_failure_=early_retry_.FailureTime();
        Log("RETRY_A5_EARLY_FAILURE attempt=%u reason_raw=003e; matching callback tuple and accepted target command; checking absent views",early_retry_.Attempts());
    }
    bool absent{};rc=ReadTargetAbsent(absent);if(rc)return rc;
    now=armTicksToNs(armGetSystemTick())/1000000;
    const auto action=early_retry_.Absent(now,absent);
    early_request_completed_=early_retry_.CompletedAbsent();
    if(action==EarlyLinkRetry::Decision::Refuse) {
        Log("RETRY_A5_STOP: absence/budget/capacity guard; no further request");return Invalid();
    }
    if(action!=EarlyLinkRetry::Decision::Retry)return 0;
    // No encryption was begun, so retain the same prepared saved peer and
    // live clients. No native-context reset or foreign handle cancellation.
    rc=PollPeerPreparation(now,true);if(rc)return rc;
    rc=PersistLinkMarker(now);if(rc)return rc;
    connection_requested_=false;last_connection_poll_ms_=0;
    Log("RETRY_A5_RETRY: terminal callback plus two absent views; backoff complete; fresh preflight before dispatch");
    return RequestConnection(); // repeats both list prechecks and evidence-history checks
}
#endif
Result LibnxSession::PollLinkOnly(std::uint64_t now) {
#ifdef ARMODULE_NATIVE_HCI_C2
    if(connection_requested_)PollHciTrace(now);
#endif
#ifdef ARMODULE_NATIVE_PEER_C1
    if(peer_nonce_ && !connection_requested_){const auto rc=PollPeerPreparation(now);if(R_FAILED(rc))return rc;}
#endif
    if (scanning_) { const auto rc = PollScan(now); if (R_FAILED(rc)) return rc; }
    if (!connection_requested_) return 0;
#ifdef ARMODULE_RETRY_A5
    bool settling{};const auto early_rc=PollEarlyLinkRetry(now,settling);
    if(early_rc || settling)return early_rc;
#endif
    const auto hint = eventWait(&connection_event_, 0);
    if (R_FAILED(hint) && !Timeout(hint)) return hint;
    if (!last_connection_poll_ms_ || now - last_connection_poll_ms_ >= 500) {
        last_connection_poll_ms_ = now;
        std::array<BtdrvBleConnectionInfo,16> connections{}; u8 count = 0;
        const auto rc = btmuBleGetConnectionState(connections.data(), connections.size(), &count);
        if (!input_budget_.Sustained() || R_FAILED(rc) || connection_log_gate_.Due(now,count))
            Log("API=btmuBleGetConnectionState(link-only) Result=%08lx count=%u", static_cast<unsigned long>(rc), count);
        if (R_FAILED(rc)) return rc;
        if (count >= connections.size()) return Invalid();
        bool found = false;
        for (std::size_t i = 0; i < count; ++i) {
            if (std::memcmp(&connections[i].addr, &target_, sizeof target_)) continue;
            if (found || (have_connection_ && connection_ != connections[i].connection_handle)) return Invalid();
            found = true;
            if (!have_connection_) {
#ifdef ARMODULE_WAKE_A3
                if(std::find(retired_connections_.begin(),retired_connections_.end(),connections[i].connection_handle)!=retired_connections_.end()){
                    Log("WAKE_A3 numeric connection id reused after verified retirement; not a session identity; fresh nonce/security required");
                }
#endif
                connection_ = connections[i].connection_handle; have_connection_ = true;
                linked_ms_ = armTicksToNs(armGetSystemTick()) / 1000000;
                Log("Exact configured target handle=%lu observed; fingerprint_unverified=1 authenticated_identity_unverified=1 driver_client_ownership_unproven=1", static_cast<unsigned long>(connection_));
                const auto stop = StopOwnScan(); if (R_FAILED(stop)) return stop;
            }
        }
        if (have_connection_ && !found) {
#ifdef ARMODULE_WAKE_A3
            completed_link_lost_=Ready() && bond_saved_ && bond_started_;
            if(completed_link_lost_)decoder_.Disconnect();
#endif
            Log("LINK_ONLY target handle no longer present; no reconnect"); return Invalid();
        }
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
namespace {
BtdrvBleAdvertisePacketParameter EnrollmentParameter() {
    // AD-derived query/enrollment hypothesis, NOT a proven vendor schema.
    BtdrvBleAdvertisePacketParameter p{};p.company_id=0x0171;
    const u8 bytes[]{0x04,0x27,0x02,0x7d,0,0};std::copy_n(bytes,6,p.pattern_data);return p;
}
}
void LibnxSession::EnrollmentFinish(EnrollmentState state,Result result) {
    enrollment_.state=unsigned(state);enrollment_.result=result;
    enrollment_.checksum=EnrollmentChecksum(enrollment_);
    Log("ENROLLMENT state=%u Result=%08lx request_issued=%u before=%u/%u after=%u/%u; listed_is_not_encryption_or_reboot_persistence=1",
        enrollment_.state,static_cast<unsigned long>(result),enrollment_.request_issued,
        enrollment_.before_count,enrollment_.before_matches,enrollment_.after_count,enrollment_.after_matches);
}
Result LibnxSession::ReadEnrollmentList(std::uint32_t &count,std::uint32_t &matches) {
    count=matches=0;std::array<BtdrvAddress,10> addresses{};u8 n{};
    const auto rc=system_enrollment_ ? gatt_.PairedAddresses(EnrollmentParameter(),addresses.data(),addresses.size(),&n) :
        btmuBleGetPairedDevices(EnrollmentParameter(),addresses.data(),addresses.size(),&n);
    if(R_FAILED(rc))return rc;
    count=n;if(n>addresses.size())return Invalid();
    for(unsigned i=0;i<n;++i)matches+=!std::memcmp(&addresses[i],&target_,sizeof target_);
    return matches<=1?0:Invalid();
}
#ifdef ARMODULE_NAV17
void LibnxSession::LogServiceUuid(const BtdevGattService &s,std::size_t index,const char *phase) {
    const auto uuid=InspectServiceUuid(s.attr.uuid.size,s.attr.uuid.uuid);
    char hex[33]{};
    if(uuid.valid_length)FormatHex(hex,sizeof hex,uuid.bytes.data(),uuid.size);
    Log("SERVICE_UUID phase=%s index=%zu conn=%lu handle=%04x end=%04x instance=%lu primary=%u size=%lu valid=%u raw=%s managed_profile_match=%u",
        phase,index,static_cast<unsigned long>(s.attr.connection_handle),s.attr.handle,s.end_group_handle,
        static_cast<unsigned long>(s.instance_id),s.primary_service,static_cast<unsigned long>(uuid.size),
        uuid.valid_length,uuid.valid_length?hex:"INVALID",uuid.managed_profile_match);
}
void LibnxSession::ReadServiceUuidCensus(std::uint64_t instance,Result before) {
    // Cached client ARUID, not a read of the BTM object's private identity.
    // Calling this accessor does not initialize applet or change ownership.
    const auto aruid=appletGetAppletResourceUserId();
    Log("SERVICE_CENSUS_BEGIN instance=%llu handle=%lu cached_client_aruid=%llu aruid_zero=%u binding_before=%08lx; getter-only server_identity_unverified=1",
        static_cast<unsigned long long>(instance),static_cast<unsigned long>(connection_),
        static_cast<unsigned long long>(aruid),aruid==0,static_cast<unsigned long>(before));
    if(R_FAILED(before))return;
    std::array<BtdevGattService,100> services{};u8 count=0;
    const auto rc=gatt_.Services(connection_,services.data(),services.size(),&count);
    unsigned invalid=0,matches=0,foreign=0;
    if(R_SUCCEEDED(rc) && count<=services.size()) {
        for(std::size_t i=0;i<count;++i) {
            const auto &s=services[i];
            const auto uuid=InspectServiceUuid(s.attr.uuid.size,s.attr.uuid.uuid);
            invalid+=!uuid.valid_length;matches+=uuid.managed_profile_match;
            foreign+=s.attr.connection_handle!=connection_;
            LogServiceUuid(s,i,"operator");
        }
    }
    const bool complete=R_SUCCEEDED(rc) && count<services.size() && !invalid && !foreign;
    Log("SERVICE_CENSUS_END instance=%llu Result=%08lx count=%u invalid_uuid=%u foreign_connection=%u managed_profile_matches=%u cache_rows_complete=%u; final_binding_in_context_record=1 physical_discovery_unverified=1",
        static_cast<unsigned long long>(instance),static_cast<unsigned long>(rc),count,invalid,foreign,matches,
        complete);
}
#endif
Result LibnxSession::ReadEnrollmentContext(std::uint64_t instance,EnrollmentContextRecord &out) {
    out={};
    if(enrollment_context_consumed_)return Invalid();
    enrollment_context_consumed_=true;
    const auto now=armTicksToNs(armGetSystemTick())/1000000;
    if(!instance || !now || !input_budget_.Sustained() || !initialized_ || !native_open_ || !native_requests_ ||
       !input_diagnostic_ || !have_connection_ || !connection_ || connection_==0xffffffff || !Ready() ||
       !fingerprint_logged_ || !security_read_logged_ || !inputs_loaded_ || decoder_.Buttons() ||
       !gatt_.System() || input_budget_.Expired(linked_ms_,now))return Invalid();
    out.instance=instance;out.expected_handle=connection_;
    out.before_binding=CheckCurrentTarget();
#ifdef ARMODULE_NAV17
    ReadServiceUuidCensus(instance,out.before_binding);
#endif
    // The existing SDK getter adapter is reused, but its user/system client
    // open/close operations are borrowed. Never replace or close the session's
    // clients. Only the temporary btmsys getter client is opened/closed here.
    struct BorrowedBackend:araudit::Backend {
        Result OpenBle(){return 0;} void CloseBle(){}
        Result OpenHid(){return 0;} void CloseHid(){}
    } backend;
    araudit::Address target{};std::memcpy(target.data(),target_.address,target.size());
    out.snapshot=araudit::CollectContext(backend,target);
    out.after_binding=CheckCurrentTarget();out.checksum=EnrollmentContextChecksum(out);
    Log("ENROLLMENT_CONTEXT getter-only instance=%llu expected_handle=%lu binding_before=%08lx binding_after=%08lx; no pair request",
        static_cast<unsigned long long>(instance),static_cast<unsigned long>(connection_),
        static_cast<unsigned long>(out.before_binding),static_cast<unsigned long>(out.after_binding));
    return 0; // Failed getters/binding checks remain visible in the record.
}
#ifdef ARMODULE_NATIVE_OWNER
arnativebond::OwnerResult LibnxSession::NativeBondOnce(std::uint64_t sequence,const arnativebond::OwnerEnvironment &environment,arnativebond::NativeIntent persist) {
    using namespace arnativebond;
    OwnerResult out{};out.sequence=sequence;
    struct Backend {
        LibnxSession &owner;const OwnerEnvironment &env;NativeIntent persist;OwnerResult &out;
        std::uint64_t Now(){return armTicksToNs(armGetSystemTick())/1000000;}
        bool Supported(){return hosversionGet()==MAKEHOSVERSION(22,5,0);}
        bool OperatorConfirmed(){return out.sequence!=0;}
        void Close(){} // Borrowed clients remain owned until normal cleanup.
        Result Open(){return owner.native_open_ && owner.native_.Active()?0:Refusal;}
        Result Enabled(u8 &v){return owner.native_.BondEnabled(v);}
        Result Request(BtdrvAddress target){return owner.native_.BondRequest(target);}
        bool PersistIntent(const Record &r){return persist && persist(r);}
        Result Guard(BtdrvAddress target,Binding &binding){
            binding={};if(out.count>=out.checks.size())return Refusal;
            auto &d=out.checks[out.count++];d.call=out.count;d.begin_ms=Now();d.instance=env.status.instance;
            const auto finish=[&](OwnerStage stage,Result rc){d.stage=unsigned(stage);d.result=rc;d.end_ms=Now();d.checksum=OwnerChecksum(d);return rc;};
            auto &s=owner;
            if(!env.pad_attached || !env.log_healthy || !s.input_budget_.Sustained() || !s.initialized_ ||
               !s.native_open_ || !s.native_requests_ || !s.native_.Active() || !s.gatt_.SystemOpen() ||
               !s.input_diagnostic_ || !s.have_connection_ || !s.connection_ || s.connection_==0xffffffff ||
               !s.Ready() || !s.fingerprint_logged_ || !s.security_read_logged_ || !s.inputs_loaded_ ||
               s.Buttons() || s.input_budget_.Expired(s.linked_ms_,Now()) ||
               std::memcmp(target.address,s.target_.address,sizeof target.address))return finish(OwnerStage::LocalState,Refusal);
            if(!SystemReadyBinding(env.status,env.startup,env.context,Now(),s.connection_))return finish(OwnerStage::Metadata,Refusal);
            d.user_result=s.CheckCurrentTarget();if(d.user_result)return finish(OwnerStage::UserBinding,d.user_result);
            std::array<BtdrvBleConnectionInfo,16> list{};u8 count{};
            d.system_result=btmBleGetConnectionState(list.data(),list.size(),&count);
            if(d.system_result)return finish(OwnerStage::SystemGetter,d.system_result);
            d.count=count;if(count>=list.size())return finish(OwnerStage::ListBounds,Refusal);
            std::array<araudit::Connection,16> normalized{};araudit::Address configured{};
            std::memcpy(configured.data(),target.address,configured.size());
            for(unsigned i=0;i<count;++i){normalized[i].handle=list[i].connection_handle;std::memcpy(normalized[i].address.data(),list[i].addr.address,configured.size());}
            araudit::ConnectionQuery q{};q.list.result=0;araudit::CountConnections(q,configured,normalized,count);
            d.matches=q.list.matches;d.invalid=q.list.invalid;d.handle=q.target_handle;
            if(q.list.result || q.list.invalid)return finish(OwnerStage::ListInvalid,Refusal);
            if(q.list.matches!=1 || q.target_handle!=s.connection_)return finish(OwnerStage::TargetBinding,Refusal);
            d.user_result=s.CheckCurrentTarget();if(d.user_result)return finish(OwnerStage::UserBinding,d.user_result);
            if(!SystemReadyBinding(env.status,env.startup,env.context,Now(),s.connection_) || s.Buttons())return finish(OwnerStage::Metadata,Refusal);
            binding={env.status.instance,s.connection_};return finish(OwnerStage::Passed,0);
        }
    } backend{*this,environment,persist,out};
    out.trial=native_bond_trial_.Attempt(backend,target_);
    // Even a pre-guard refusal is correlated with the command's exact owner.
    if(!out.trial.instance){out.trial.instance=environment.status.instance;out.trial.checksum=Checksum(out.trial);}
    out.checksum=OwnerResultChecksum(out);return out;
}
#endif
Result LibnxSession::EnrollOnce(std::uint64_t instance,EnrollmentIntent persist_intent) {
#ifdef ARMODULE_RECONNECT_CHECK
    // The reconnect experiment never repeats explicit managed enrollment.
    // Horizon may still negotiate security internally for normal GATT requests;
    // successful input alone must not be reported as saved-key reuse.
    (void)instance; (void)persist_intent;
    return Invalid();
#else
    if(enrollment_consumed_)return Invalid();
    enrollment_consumed_=true;enrollment_.instance=instance;
    enrollment_.requested_ms=armTicksToNs(armGetSystemTick())/1000000;
    const auto refuse=[&](Result rc){EnrollmentFinish(EnrollmentState::Refused,rc);return rc;};
    if(!instance || !persist_intent || !input_budget_.Sustained() || !initialized_ || !native_open_ || !native_requests_ ||
       !input_diagnostic_ || !have_connection_ || !connection_ || connection_==0xffffffff || !Ready() ||
       !fingerprint_logged_ || !security_read_logged_ || !inputs_loaded_ || decoder_.Buttons() ||
       (system_enrollment_ && !gatt_.System()) ||
       !enrollment_.requested_ms || input_budget_.Expired(linked_ms_,enrollment_.requested_ms))return refuse(Invalid());
    auto rc=CheckCurrentTarget();if(R_FAILED(rc))return refuse(rc);
    enrollment_.connection=connection_;
    enrollment_.before_result=ReadEnrollmentList(enrollment_.before_count,enrollment_.before_matches);
    if(R_FAILED(enrollment_.before_result))return refuse(enrollment_.before_result);
    if(enrollment_.before_matches) {EnrollmentFinish(EnrollmentState::AlreadyListed,0);return 0;}
    // Never request a new entry in a full scope, to avoid implicit eviction.
    if(enrollment_.before_count>=10)return refuse(Invalid());
    EnrollmentFinish(EnrollmentState::Prepared,0);
    if(!persist_intent(enrollment_))return refuse(Invalid());
    // Revalidate AFTER durable intent; a handle may change during SD I/O.
    rc=CheckCurrentTarget();if(R_FAILED(rc))return refuse(rc);
    const auto now=armTicksToNs(armGetSystemTick())/1000000;
    if(now<enrollment_.requested_ms || now-enrollment_.requested_ms>2000 || input_budget_.Expired(linked_ms_,now))return refuse(Invalid());
    enrollment_.request_issued=1;
    // Fixed route for this session: never retry a rejected request via another
    // service. System mode requires our already-open background BTM client.
    rc=system_enrollment_ ? gatt_.PairDevice(connection_,EnrollmentParameter()) :
        btmuBlePairDevice(connection_,EnrollmentParameter());
    Log("API=%s(operator enrollment; AD-derived0171/0427027d0000) Result=%08lx; one request only, parameter semantics/persistence UNVERIFIED",
        system_enrollment_?"btmBlePairDevice":"btmuBlePairDevice",static_cast<unsigned long>(rc));
    if(R_FAILED(rc)){EnrollmentFinish(EnrollmentState::Failed,rc);return rc;}
    enrollment_.deadline_ms=now+10000;EnrollmentFinish(EnrollmentState::Accepted,0);return 0;
#endif
}
void LibnxSession::PollEnrollment(std::uint64_t now) {
    if(enrollment_.state!=unsigned(EnrollmentState::Accepted))return;
    if(now<enrollment_.requested_ms || now>=enrollment_.deadline_ms){EnrollmentFinish(EnrollmentState::TimedOut,Invalid());return;}
    if(enrollment_.last_query_ms && now-enrollment_.last_query_ms<500)return;
    enrollment_.last_query_ms=now;
    const auto current=CheckCurrentTarget();
    if(R_FAILED(current)){EnrollmentFinish(EnrollmentState::Failed,current);return;}
    enrollment_.after_result=ReadEnrollmentList(enrollment_.after_count,enrollment_.after_matches);
    if(R_FAILED(enrollment_.after_result))EnrollmentFinish(EnrollmentState::Failed,enrollment_.after_result);
    else if(enrollment_.after_matches==1)EnrollmentFinish(EnrollmentState::Listed,0);
    else enrollment_.checksum=EnrollmentChecksum(enrollment_);
}
#ifdef ARMODULE_CENTRAL_BOND_B1
Result LibnxSession::PollCentralBond(bool &ready) {
    ready=false;bool present{};
    auto rc=ReadDisconnectPresence(present,true);
    if(R_FAILED(rc) || !present)return R_FAILED(rc)?rc:Invalid();
    if(!bond_started_){
#if defined(ARMODULE_SAVED_DISCOVERY_B3) || defined(ARMODULE_NATIVE_PEER_C1)
        central_bond::Status before{};
        rc=serviceDispatch(&bond_service_,central_bond::StatusCommand,
            .buffer_attrs={SfBufferAttr_HipcPointer|SfBufferAttr_Out},.buffers={{&before,sizeof before}});
        if(R_FAILED(rc) || before.magic!=central_bond::Magic || before.version!=central_bond::Version ||
           before.store!=central_bond::Store::Saved || !bond_saved_){
            Log("B3_SAVED_BOND_REQUIRED: native request withheld; no enrollment fallback");
            return R_FAILED(rc)?rc:Invalid();
        }
#endif
        bond_started_=true;bond_nonce_=std::uint32_t(armGetSystemTick());if(!bond_nonce_)bond_nonce_=1;
        // sf::ClientProcessId occupies a raw u64 as well as requesting the
        // kernel PID descriptor. The server replaces this zero with kernel PID.
        struct {u64 placeholder_pid;central_bond::Begin begin;} request{0,{bond_nonce_,connection_,linked_ms_}};
        rc=serviceDispatchIn(&bond_service_,central_bond::BeginCommand,request,.in_send_pid=true);
        Log("B1_BEGIN mode=%s result=%08lx; wait private native completion and storage readback",
            bond_saved_?"STORED_KEY":"ENROLL_ONCE",static_cast<unsigned long>(rc));
        if(R_FAILED(rc))return rc;
    }
    central_bond::Status state{};
    rc=serviceDispatch(&bond_service_,central_bond::StatusCommand,
        .buffer_attrs={SfBufferAttr_HipcPointer|SfBufferAttr_Out},.buffers={{&state,sizeof state}});
    if(R_FAILED(rc))return rc;
    if(state.magic!=central_bond::Magic || state.version!=1 || state.nonce!=bond_nonce_ ||
       state.connection!=connection_ || state.owner_epoch!=linked_ms_)return Invalid();
    if(bond_last_phase_!=unsigned(state.phase)){
        bond_last_phase_=unsigned(state.phase);
        Log("B1_STATE phase=%u store=%u native_status=%u encrypted=%u key_bytes=%u result=%08lx",
            unsigned(state.phase),unsigned(state.store),state.native_status,state.encrypted,state.key_bytes,
            static_cast<unsigned long>(state.result));
    }
    if(state.phase==central_bond::Phase::Failed || state.phase==central_bond::Phase::Cancelled)
        return state.result?state.result:Invalid();
    ready=state.phase==central_bond::Phase::Ready && state.store==central_bond::Store::Saved &&
        state.encrypted==1 && state.key_bytes==16 && !state.native_status && !state.result;
    if(ready)Log("B1_SECURITY_READY: native completion verified; private bond saved/readback verified. Reboot reconnect still requires device test.");
    return 0;
}
#endif
Result LibnxSession::PollIdentity(std::uint64_t now) {
    if (!client_.Tick(now)) return Invalid(); // Expire before consuming completion.
    if (input_diagnostic_ && input_budget_.Expired(linked_ms_,now)) {
        Log(input_budget_.Sustained() ? "INPUT_STOP_BUDGET: setup90s or Ready30min budget expired/clock regressed; no reconnect or security retry" :
            "INPUT_DIAGNOSTIC fixed90s connected-session limit reached; no reconnect or automatic security retry");
        return Invalid();
    }
    PollEnrollment(now);
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
#ifdef ARMODULE_CENTRAL_BOND_B1
        bool bond_ready{};
        const auto bond_rc=PollCentralBond(bond_ready);
        if(R_FAILED(bond_rc))return bond_rc;
        if(!bond_ready)return 0;
#endif
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
        if (!input_budget_.AdmitReady(linked_ms_,armTicksToNs(armGetSystemTick()) / 1000000)) return Invalid();
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
            rc = gatt_.ConfigureMtu(connection_, 0x200);
            Log("API=%sConfigureBleMtu Result=%08lx",gatt_.System()?"btm":"btdev", static_cast<unsigned long>(rc));
            if (R_FAILED(rc)) return rc;
            mtu_requested_ = true;
        }
        u16 mtu = 0; rc = gatt_.GetMtu(connection_, &mtu);
        Log("API=%sGetBleMtu Result=%08lx mtu=%u",gatt_.System()?"btm":"btdev", static_cast<unsigned long>(rc), mtu);
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
    // Admit immediately after the final successful CCC completion, before the
    // caller attaches a pad. Late completions cannot extend expired setup.
    if (Ready() && !input_budget_.AdmitReady(linked_ms_,armTicksToNs(armGetSystemTick()) / 1000000)) return Invalid();
#ifdef ARMODULE_HID_INFO_H1
    PollHidInformation(armTicksToNs(armGetSystemTick()) / 1000000);
#endif
    return 0;
}
#ifdef ARMODULE_HID_INFO_H1
void LibnxSession::PollHidInformation(std::uint64_t now) {
    if (hid_information_pending_ && (now < hid_information_started_ms_ || now-hid_information_started_ms_ >= 3000)) {
        hid_information_pending_ = false;
        Log("HID_INFO_H1_TIMEOUT: no value accepted; navigation unchanged, no retry");
    }
    if (!Ready() || !fingerprint_logged_ || !security_read_logged_ || !inputs_loaded_ ||
        !native_requests_ || !native_open_ || pending_operation_ || hid_information_attempted_) return;
    if (!hid_information_ready_ms_) { hid_information_ready_ms_=now; return; }
    if (now < hid_information_ready_ms_ || now-hid_information_ready_ms_ < 5000 || Buttons()) return;
    hid_information_attempted_ = true;
    if (!hid_information_.handle) return;
    // Revalidate exact live target immediately before the one read. No scan,
    // pairing, security escalation, Control Point or protocol-mode request.
    const auto bound = CheckCurrentTarget();
    if (R_FAILED(bound)) { Log("HID_INFO_H1_SKIPPED: live binding failed"); return; }
    const auto service = Id(client_.HidService()), characteristic = Id(hid_information_);
    hid_information_connection_ = connection_;
    hid_information_generation_ = client_.Generation();
    hid_information_started_ms_ = armTicksToNs(armGetSystemTick())/1000000;
    hid_information_pending_ = true;
    const auto rc = native_.ReadHidInformation(connection_,&service,&characteristic);
    Log("HID_INFO_H1_REQUEST: result=%08lx conn=%u generation=%u handle=%04x instance=%u auth_request=0; diagnostic_only=1",
        static_cast<unsigned long>(rc),connection_,hid_information_generation_,hid_information_.handle,hid_information_.instance);
    if (R_FAILED(rc)) hid_information_pending_ = false;
}
bool LibnxSession::CompleteHidInformation(const BtdrvBleEventInfo &event) {
    const auto &op = event.client_notify;
    if (op.type != 0 || ShortUuid(op.serv_uuid) != 0x1812 || ShortUuid(op.char_uuid) != 0x2a4a) return false;
    const auto now = armTicksToNs(armGetSystemTick())/1000000;
    if (!hid_information_pending_ || !Ready() || op.conn_id != hid_information_connection_ ||
        connection_ != hid_information_connection_ || client_.Generation() != hid_information_generation_ ||
        now < hid_information_started_ms_ || now-hid_information_started_ms_ >= 3000) {
        Log("HID_INFO_H1_IGNORED: unsolicited, late or retired read response; no evidence accepted");
        return true;
    }
    hid_information_pending_ = false;
    HidInformation info{};
    if (op.result || !DecodeHidInformation(op.data,op.size,info)) {
        Log("HID_INFO_H1_NO_VALUE: result=%u size=%u; navigation unchanged, no retry",op.result,op.size);
        return true;
    }
    char raw[9]{}; FormatHex(raw,sizeof raw,op.data,4);
    Log("HID_INFO_H1_VALUE: raw=%s bcd_hid=%04x country=%u flags=%02x RemoteWake=%u NormallyConnectable=%u other_flags=%02x; host_wake_proven=0",
        raw,info.bcd_hid,info.country,info.flags,info.RemoteWake(),info.NormallyConnectable(),info.OtherFlags());
    return true;
}
#endif
Result LibnxSession::ReleaseSubscriptions(bool trace_disconnect) {
    Result first_error{};
    for (std::size_t i = 0; i < subscription_count_; ++i) {
        const auto &r = subscriptions_[i]; const auto service = Id(r.service), characteristic = Id(r.characteristic);
        const auto binding = identity_test_ ? CheckCurrentTarget() : Result{};
        if (trace_disconnect) Log("DISCONNECT_STEP stage=subscription_binding index=%lu Result=%08lx",
            static_cast<unsigned long>(i),static_cast<unsigned long>(binding));
        if (R_FAILED(binding)) {
            Log("cleanup report deregistration skipped: configured address/handle no longer bound; no operation on a reassigned handle");
            first_error = first_error ? first_error : Invalid();
            break;
        }
        const auto rc = native_requests_ ? native_.Notify(r.connection,r.service.primary,&service,&characteristic,false)
            : btLeClientDeregisterNotification(r.connection,r.service.primary,&service,&characteristic);
        if (trace_disconnect) Log("DISCONNECT_STEP stage=subscription_deregister index=%lu Result=%08lx native=%u",
            static_cast<unsigned long>(i),static_cast<unsigned long>(rc),native_requests_);
        if (R_FAILED(rc)) {
            Log("cleanup deregister own report failed: Result=%08lx",static_cast<unsigned long>(rc));
            if (!first_error) first_error = rc;
        }
    }
    subscription_count_ = 0;
    return first_error;
}
Result LibnxSession::FinishDisconnect(Result result) {
    disconnect_result_ = result;
    disconnect_state_ = R_FAILED(result) ? DisconnectState::Failed : DisconnectState::Confirmed;
    Log("OWNED_DISCONNECT %s Result=%08lx; bond records and pending marker untouched",
        R_FAILED(result) ? "NOT_CONFIRMED" : "CONFIRMED_ABSENT",static_cast<unsigned long>(result));
    if (R_SUCCEEDED(result)) have_connection_ = connection_requested_ = false;
    return result;
}
Result LibnxSession::ReadDisconnectPresence(bool &present, bool require_both) {
    present = false;
    unsigned matching_views{};
    // A successful empty user view alone has previously hidden a system link.
    // Require both views through the existing clients, never reopen or adopt.
    if (!gatt_.SystemOpen()) {
        Log("DISCONNECT_STEP stage=system_client_guard Result=%08lx",static_cast<unsigned long>(Invalid()));
        return Invalid();
    }
    for (const bool system : {true, false}) {
        std::array<BtdrvBleConnectionInfo,16> entries{}; u8 count{};
        const auto rc = system ? btmBleGetConnectionState(entries.data(),entries.size(),&count)
            : btmuBleGetConnectionState(entries.data(),entries.size(),&count);
        const auto view=system?0u:1u;
        const char *label=system?"system_presence":"user_presence";
        if (R_FAILED(rc)) {
            Log("DISCONNECT_STEP stage=%s Result=%08lx count_untrusted=%u",label,static_cast<unsigned long>(rc),count);
            return rc;
        }
        if (count >= entries.size()) {
            Log("DISCONNECT_STEP stage=%s Result=%08lx guard=capacity count=%u",label,static_cast<unsigned long>(Invalid()),count);
            return Invalid();
        }
        unsigned matches{};
        for (std::size_t i=0;i<count;++i) {
            const bool address = !std::memcmp(&entries[i].addr,&target_,sizeof target_);
            const bool handle = entries[i].connection_handle == connection_;
            if (address != handle) {
                Log("DISCONNECT_STEP stage=%s Result=%08lx guard=rebound_or_recycled",label,static_cast<unsigned long>(Invalid()));
                return Invalid(); // Rebound address or recycled handle.
            }
            if (address) ++matches;
        }
        if (matches > 1) {
            Log("DISCONNECT_STEP stage=%s Result=%08lx guard=duplicate",label,static_cast<unsigned long>(Invalid()));
            return Invalid();
        }
        const auto signature=std::uint32_t(count)|(std::uint32_t(matches)<<8);
        if (disconnect_view_trace_[view]!=signature) {
            Log("DISCONNECT_STEP stage=%s Result=00000000 count=%u owned_matches=%u",label,count,matches);
            disconnect_view_trace_[view]=signature;
        }
        present = present || matches == 1;
        matching_views += matches == 1;
    }
    if (require_both && matching_views != 2) return Invalid();
    return 0;
}
Result LibnxSession::BeginOwnedDisconnect(std::uint64_t now) {
    if (disconnect_state_ != DisconnectState::NotStarted) return disconnect_result_;
    // This deliberately does not relax the existing one-shot/rearm policy.
    // Unknown pending connects still need recovery, not a MAC-only disconnect.
    ResetInput();
    if (!initialized_ || !link_only_ || !identity_test_ || !now) {
        Log("DISCONNECT_STEP stage=session_guard Result=%08lx initialized=%u link_only=%u identity=%u",
            static_cast<unsigned long>(Invalid()),initialized_,link_only_,identity_test_);
        return FinishDisconnect(Invalid());
    }
    if(enrollment_.state==unsigned(EnrollmentState::Accepted))EnrollmentFinish(EnrollmentState::Cancelled,Invalid());
    auto rc = StopOwnScan();
    Log("DISCONNECT_STEP stage=stop_scan Result=%08lx",static_cast<unsigned long>(rc));
    if (R_FAILED(rc)) return FinishDisconnect(rc);
#ifdef ARMODULE_RETRY_A5
    if(early_request_completed_ && !have_connection_ && !bond_started_) {
        bool absent{};rc=ReadTargetAbsent(absent);
        if(rc || !absent)return FinishDisconnect(rc?rc:Invalid());
        Log("RETRY_A5_TERMINAL_ABSENT: failed request completed; no disconnect command needed");
        return FinishDisconnect(0);
    }
#endif
    if (!connection_requested_ && !have_connection_) return FinishDisconnect(0);
    if (!connection_requested_ || !have_connection_ || !connection_ || connection_==0xffffffff) {
        Log("DISCONNECT_STEP stage=ownership_guard Result=%08lx requested=%u observed=%u",
            static_cast<unsigned long>(Invalid()),connection_requested_,have_connection_);
        return FinishDisconnect(Invalid());
    }
    bool present{};
    rc = ReadDisconnectPresence(present);
    if (R_FAILED(rc)) return FinishDisconnect(rc);
    if (!present) { subscription_count_ = 0; return FinishDisconnect(0); }
    rc = ReleaseSubscriptions(true);
    if (R_FAILED(rc)) return FinishDisconnect(rc);
    // Recheck BOTH views after deregistration. Never adopt a system-only
    // connection or act on a disappeared/reassigned target. Our original
    // request and pre-existing-target refusal remain required above.
    rc = ReadDisconnectPresence(present, true);
    Log("DISCONNECT_STEP stage=final_binding Result=%08lx",static_cast<unsigned long>(rc));
    if (R_FAILED(rc)) return FinishDisconnect(rc);
    disconnect_started_ms_ = armTicksToNs(armGetSystemTick()) / 1000000;
    if (disconnect_started_ms_ < now || disconnect_started_ms_ - now > 2000) {
        Log("DISCONNECT_STEP stage=dispatch_clock_guard Result=%08lx",static_cast<unsigned long>(Invalid()));
        return FinishDisconnect(Invalid());
    }
    disconnect_polled_ms_ = disconnect_started_ms_;
    // This is a background system client, not an applet. Use its already-open
    // standard BTM service directly, not a rejected user call plus a fallback.
    // Do not borrow an applet identity or open/initialize another radio client.
    rc = btmBleDisconnect(connection_);
    Log("DISCONNECT_STEP stage=btm_disconnect Result=%08lx handle=%lu",static_cast<unsigned long>(rc),static_cast<unsigned long>(connection_));
    if (R_FAILED(rc)) return FinishDisconnect(rc);
    disconnect_state_ = DisconnectState::Waiting;
    Log("OWNED_DISCONNECT requested handle=%lu; completion pending, no retry or bond removal",
        static_cast<unsigned long>(connection_));
    return 0;
}
Result LibnxSession::PollOwnedDisconnect(std::uint64_t now) {
    if (disconnect_state_ == DisconnectState::NotStarted) return Invalid();
    if (disconnect_state_ != DisconnectState::Waiting) return disconnect_result_;
    if (now < disconnect_polled_ms_ || now < disconnect_started_ms_) return FinishDisconnect(Invalid());
    constexpr std::uint64_t timeout_ms=5000, interval_ms=100;
    if (now-disconnect_started_ms_ >= timeout_ms) {
        Log("DISCONNECT_STEP stage=completion_timeout Result=%08lx",static_cast<unsigned long>(MAKERESULT(Module_Kernel,KernelError_TimedOut)));
        return FinishDisconnect(MAKERESULT(Module_Kernel,KernelError_TimedOut));
    }
    if (now-disconnect_polled_ms_ < interval_ms) return 0;
    disconnect_polled_ms_=now;
    bool present{};
    const auto rc=ReadDisconnectPresence(present);
    if (R_FAILED(rc)) return FinishDisconnect(rc);
    const auto after=armTicksToNs(armGetSystemTick()) / 1000000;
    if (after < now || after-disconnect_started_ms_ >= timeout_ms)
        return FinishDisconnect(MAKERESULT(Module_Kernel,KernelError_TimedOut));
    return present ? 0 : FinishDisconnect(0);
}
void LibnxSession::Close(bool sleeping) {
#ifndef ARMODULE_WAKE_A3
    (void)sleeping;
#endif
#ifdef ARMODULE_CENTRAL_BOND_B1
    if(serviceIsActive(&bond_service_)){
#ifdef ARMODULE_NATIVE_HCI_C2
        if(connection_requested_ && !sleeping){
            PollHciTrace(armTicksToNs(armGetSystemTick())/1000000,true);
            Log("C2_HCI_SUMMARY records=%u read_failed=%u; zero records is inconclusive, not proof of no command",hci_logged_,unsigned(hci_read_failed_));
        }
#endif
        if(!sleeping && (bond_started_
#ifdef ARMODULE_NATIVE_PEER_C1
           || peer_nonce_
#endif
        )){const u64 placeholder_pid=0;
            (void)serviceDispatchIn(&bond_service_,central_bond::CancelCommand,placeholder_pid,.in_send_pid=true);}
        serviceClose(&bond_service_);
    }
#endif
    if(enrollment_.state==unsigned(EnrollmentState::Accepted))EnrollmentFinish(EnrollmentState::Cancelled,Invalid());
    if (!initialized_) return;
    const auto cleanup = [&](const char *operation, Result rc) {
        if (R_FAILED(rc)) Log("cleanup %s failed: Result=%08lx", operation, static_cast<unsigned long>(rc));
    };
    if(!sleeping){
        cleanup("stop own filtered scan", StopOwnScan());
        cleanup("release subscriptions", ReleaseSubscriptions());
    } else { subscription_count_=0;Log("WAKE_A3_SLEEP: retire local subscriptions without stale-handle radio operations; absence must be checked after wake"); }
    if (link_only_) {
        if (disconnect_state_ == DisconnectState::Waiting) FinishDisconnect(Invalid());
        if (!sleeping && link_attempted_ && disconnect_state_ != DisconnectState::Confirmed)
            Log("LINK_ONLY close: disconnect not confirmed; no MAC-only fallback; pending marker retained");
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
    gatt_.Close();
    if (native_open_) { native_.Close(); } native_open_=false;
    ResetInput();
    initialized_ = event_open_ = discovery_open_ = connection_requested_ = have_connection_ = information_path_ = hid_path_ = false;
    scan_event_open_ = connection_event_open_ = pairing_event_open_ = authenticated_reads_ = false;
    btdev_initialized_ = false;
    // Do not erase a failed-stop ownership marker before logging it.
    if (scanning_) Log("WARNING: owned scan stop failed during exit; another scan must not be started until system recovery");
}
#ifdef ARMODULE_WAKE_A3
bool LibnxSession::RetireForSleep(){
    if(sleep_retired_ || !Ready() || !bond_saved_ || !bond_started_ || !have_connection_ || !connection_requested_ ||
       scanning_ || !connection_ || retired_count_>=retired_connections_.size())return false;
    retired_connections_[retired_count_++]=connection_;sleep_retired_=true;
    Close(true);return true;
}
#ifdef ARMODULE_SETUP_SLEEP_R3
bool LibnxSession::RetirePendingForSleep(){
    // Unfinished saved-peer attempt only: native peer prepared, no READY session.
    // Close(true) performs no radio operation and never sends Cancel, which would
    // stop the companion's wake session. Absence is checked after full wake.
    if(sleep_retired_ || Ready() || !bond_saved_ || !peer_nonce_ || !peer_generation_ ||
       !known_reconnect_ready_ms_ || scanning_ || retired_count_>=retired_connections_.size())return false;
    if(have_connection_ && connection_)retired_connections_[retired_count_++]=connection_;
    pending_peer_nonce_=peer_nonce_;pending_retired_=sleep_retired_=true;
    Close(true);return true;
}
#endif
Result LibnxSession::ResumeAfterSleep(std::uint64_t now,bool &ready){
    ready=false;
    if(!sleep_retired_ || initialized_ || native_open_ || gatt_.SystemOpen() || !now)return Invalid();
    if(!wake_begin_)wake_begin_=now;
    if(now<wake_begin_ || now-wake_begin_>=10000)return Invalid();
    Service service{};auto rc=smGetService(&service,"mc");if(rc)return rc;
    central_bond::WakeStatus w{};
    rc=serviceDispatch(&service,central_bond::WakeStatusCommand,
        .buffer_attrs={SfBufferAttr_HipcPointer|SfBufferAttr_Out},.buffers={{&w,sizeof w}});
#ifdef ARMODULE_SETUP_SLEEP_R3
    // After WakeCommand the companion has consumed the pending attempt and
    // reports the plain kind 0 while it reloads the saved record.
    const std::uint32_t kind=pending_retired_ && !wake_sent_?central_bond::WakeKindPending:0u;
    const bool identity=pending_retired_?
        (w.nonce==pending_peer_nonce_ && !w.connection && !w.owner_epoch):
        (w.nonce==bond_nonce_ && w.connection==connection_ && w.owner_epoch==linked_ms_);
#else
    const std::uint32_t kind=0;
    const bool identity=w.nonce==bond_nonce_ && w.connection==connection_ && w.owner_epoch==linked_ms_;
#endif
    if(rc || w.magic!=0x33575241 || w.version!=1 || w.reserved!=kind || w.ready>1){serviceClose(&service);return rc?rc:Invalid();}
    if(!wake_sent_){
        if(!w.ready){serviceClose(&service);return 0;}
        if(!w.remaining || !w.serial || !identity){serviceClose(&service);return Invalid();}
        rc=btmuInitialize();if(rc){serviceClose(&service);return rc;}
        rc=btmInitialize();
        if(rc){btmuExit();serviceClose(&service);return rc;}
        bool absent=true;
        for(bool system:{true,false}){
            std::array<BtdrvBleConnectionInfo,16> entries{};u8 count{};
            rc=system?btmBleGetConnectionState(entries.data(),entries.size(),&count):btmuBleGetConnectionState(entries.data(),entries.size(),&count);
            if(rc || count>=entries.size()){if(!rc)rc=Invalid();break;}
            for(unsigned i=0;i<count;++i)if(!std::memcmp(&entries[i].addr,&target_,sizeof target_))absent=false;
        }
        btmExit();btmuExit();
        if(rc){serviceClose(&service);return rc;}
        if(!absent){absence_since_=0;serviceClose(&service);return 0;}
        if(!absence_since_){absence_since_=now;serviceClose(&service);return 0;}
        if(now<absence_since_){serviceClose(&service);return Invalid();}
        if(now-absence_since_<250){serviceClose(&service);return 0;}
        struct{u64 placeholder_pid;central_bond::WakeRequest request;} req{0,{w.serial,w.nonce,w.connection,kind,w.owner_epoch}};
        rc=serviceDispatchIn(&service,central_bond::WakeCommand,req,.in_send_pid=true);
        if(rc){serviceClose(&service);return rc;}
        wake_sent_=true;
        Log(kind?"SETUP_R3 unfinished attempt admitted after two absent system/user views; waiting for saved-record reload":
                 "WAKE_A3 epoch admitted after two absent system/user views; waiting for saved-record reload");
    }
    central_bond::Status s{};
    rc=serviceDispatch(&service,central_bond::StatusCommand,
        .buffer_attrs={SfBufferAttr_HipcPointer|SfBufferAttr_Out},.buffers={{&s,sizeof s}});
    serviceClose(&service);
    if(rc || s.magic!=central_bond::Magic || s.version!=1 || s.result)return rc?rc:Invalid();
    if(s.store==central_bond::Store::Unknown)return 0;
    if(s.store!=central_bond::Store::Saved || s.generation!=peer_generation_ || s.phase!=central_bond::Phase::Idle)return Invalid();
    const auto ids=retired_connections_;const auto count=retired_count_;const auto log=logger_;
#ifdef ARMODULE_HID_INFO_H1
    const auto information_attempted=hid_information_attempted_;
#endif
    // All SDK-owned handles were closed; these objects have no native function
    // callback references. Companion encryption contexts are separately retained.
    this->~LibnxSession();new(this)LibnxSession(log,true,true,true,true,true,true);
#ifdef ARMODULE_HID_INFO_H1
    hid_information_attempted_=information_attempted;
#endif
    retired_connections_=ids;retired_count_=count;ready=true;
    Log("WAKE_A3 fresh local session ready; saved bond unchanged; no pairing");return 0;
}
#endif
}
