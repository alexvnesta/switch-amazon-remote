// Independently implemented public libnx bt/btmu integration. GPL-2.0-only.
#pragma once
#include <switch.h>
#include "hogp_client.hpp"
#include "remote_decoder.hpp"
#include "target_sighting.hpp"
#include "managed_gatt.hpp"
#include "native_gatt_requests.hpp"
#include "session_budget.hpp"
#include "health_log_gate.hpp"
#include "enrollment.hpp"
#include "enrollment_context.hpp"
#ifdef ARMODULE_HID_INFO_H1
#include "hid_information.hpp"
#ifndef ARMODULE_LATENCY_A6
#error HID Information diagnostic requires the reviewed A6 transport
#endif
#endif
#if defined(ARMODULE_LATENCY_A6) && !defined(ARMODULE_RETRY_A5)
#error Compact diagnostics require the reviewed A5 transport
#endif
#ifdef ARMODULE_RETRY_A5
#include "early_link_retry.hpp"
#ifndef ARMODULE_STARTUP_A4
#error Early-link retry requires the reviewed A4 startup and C2 evidence path
#endif
#endif
#if defined(ARMODULE_KNOWN_RECONNECT_CHECK) && defined(ARMODULE_CENTRAL_BOND_B1)
#error Saved-bond operation must use observed discovery, not the address-only experiment
#endif
#if defined(ARMODULE_SAVED_DISCOVERY_B3) && !defined(ARMODULE_CENTRAL_BOND_B1)
#error Saved discovery requires central bond metadata
#endif
#if defined(ARMODULE_NATIVE_PEER_C1) && (!defined(ARMODULE_CENTRAL_BOND_B1) || defined(ARMODULE_SAVED_DISCOVERY_B3) || defined(ARMODULE_KNOWN_RECONNECT_CHECK))
#error C1 is a separate saved-peer preparation path, not the old address-only or discovery experiment
#endif
#if defined(ARMODULE_NATIVE_HCI_C2) && !defined(ARMODULE_NATIVE_PEER_C1)
#error C2 diagnostics require the C1 saved-peer context
#endif
#ifdef ARMODULE_NATIVE_OWNER
#include "../../../tools/amazon-remote-pairing-audit/native_bond_owner.hpp"
#endif

namespace amazon_remote {
using Logger = void (*)(const char *message);
enum class DisconnectState { NotStarted, Waiting, Confirmed, Failed };
// One-shot getter-only census. Opens/closes btmu, does not initialize bt,
// register data paths, start/stop a scan, connect, pair or emit controller input.
Result ReadOnlyScanCacheCensus(BtdrvAddress target, Logger logger);
// Single-thread owner. Uses btdev's user path; never consumes btdrv managed events.
class LibnxSession {
public:
    explicit LibnxSession(Logger logger, bool fingerprint_only = false, bool input_diagnostic = false, bool system_gatt = false, bool native_requests = false, bool sustained_input = false, bool system_enrollment = false);
    Result Initialize(BtdrvAddress target);
    Result InitializeLinkOnly(BtdrvAddress target);
    Result InitializeIdentityTest(BtdrvAddress target);
    Result StartLinkOnlyScan(std::uint64_t now_ms);
#if defined(ARMODULE_KNOWN_RECONNECT_CHECK) || defined(ARMODULE_NATIVE_PEER_C1)
    // Operator-only known-address experiment. Does not scan or prove bonding.
    Result PrepareKnownReconnect(std::uint64_t now_ms, std::uint64_t capture_deadline_ms=0);
    Result ConfirmKnownReconnect(std::uint64_t now_ms, std::uint64_t capture_deadline_ms);
    bool KnownReconnectPrepared() const { return known_reconnect_ready_ms_ && !link_attempted_; }
#endif
#ifdef ARMODULE_CENTRAL_BOND_B1
    bool HasSavedBond() const { return bond_saved_; }
#endif
    Result ConfirmObservedLink(const SightingWire &sighting, std::uint64_t now_ms, std::uint64_t capture_deadline_ms = 0);
    bool ScanActive() const { return scanning_; }
    bool LinkAttempted() const { return link_attempted_; }
    bool LinkConfirmed() const { return link_only_ && have_connection_; }
    std::uint64_t ScanStarted() const { return scan_started_ms_; }
    // Connects only the explicitly configured physical remote. No ambient auto-pairing.
    Result Connect(bool filtered_discovery = false, bool authenticated_reads = false);
    // Nonblocking polling; caller supplies a monotonic time in milliseconds.
    Result Poll(std::uint64_t now_ms);
    // Caller must neutralize/detach controller output first. Nonblocking: an
    // accepted disconnect is not completion. Keep clients open and poll until
    // Confirmed/Failed, then Close. No bond deletion or address-only adoption.
    Result BeginOwnedDisconnect(std::uint64_t now_ms);
    Result PollOwnedDisconnect(std::uint64_t now_ms);
    DisconnectState GetDisconnectState() const { return disconnect_state_; }
    Result DisconnectResult() const { return disconnect_result_; }
    void Close(bool sleeping=false);
#ifdef ARMODULE_WAKE_A3
    bool RetireForSleep();
#ifdef ARMODULE_SETUP_SLEEP_R3
    bool RetirePendingForSleep();
#endif
    Result ResumeAfterSleep(std::uint64_t now,bool &ready);
    bool CompletedLinkLost()const{return completed_link_lost_;}
#endif
    bool Ready() const { return client_.GetPhase() == Phase::Ready; }
    std::uint32_t Buttons() const { return decoder_.Buttons(); }
#ifdef ARMODULE_LATENCY_A6
    struct AcceptedInputTiming { std::uint64_t sequence{},received_ms{},decoded_ms{};std::uint32_t buttons{}; };
    const AcceptedInputTiming &InputTiming()const{return input_timing_;}
#endif
    Phase GetPhase() const { return client_.GetPhase(); }
    Error GetError() const { return client_.GetError(); }
    bool FingerprintObserved() const { return fingerprint_logged_; }
    bool SecurityReadObserved() const { return security_read_logged_; }
    // Explicit operator call only, never invoked by connection/Ready/polling.
    Result EnrollOnce(std::uint64_t instance,EnrollmentIntent persist_intent);
    const EnrollmentRecord &Enrollment() const {return enrollment_;}
    // One operator-only getter snapshot using the already-open user/system
    // clients. Does not enroll, connect, replace client ownership or radio state.
    Result ReadEnrollmentContext(std::uint64_t instance,EnrollmentContextRecord &out);
#ifdef ARMODULE_NATIVE_OWNER
    // Explicit fresh command only. Reuses clients; Trial owns once-only policy.
    arnativebond::OwnerResult NativeBondOnce(std::uint64_t sequence,const arnativebond::OwnerEnvironment &environment,arnativebond::NativeIntent persist);
#endif
private:
#ifdef ARMODULE_HID_INFO_H1
    void PollHidInformation(std::uint64_t now);
    bool CompleteHidInformation(const BtdrvBleEventInfo &event);
    Attribute hid_information_{};
    bool hid_information_attempted_{}, hid_information_pending_{};
    std::uint64_t hid_information_ready_ms_{}, hid_information_started_ms_{};
    std::uint32_t hid_information_connection_{}, hid_information_generation_{};
#endif
#ifdef ARMODULE_LATENCY_A6
    AcceptedInputTiming input_timing_{};
#endif
#ifdef ARMODULE_RETRY_A5
    Result ReadRetryEvidence(ConnectionEvidenceWire &,central_bond::HciTrace &);
    Result PollEarlyLinkRetry(std::uint64_t now,bool &settling);
    Result ReadTargetAbsent(bool &absent);
    EarlyLinkRetry early_retry_;
    bool early_retry_configured_{},early_settling_{},early_request_completed_{};
    std::uint64_t early_polled_{},early_logged_failure_{};
#endif
#ifdef ARMODULE_WAKE_A3
    bool completed_link_lost_{};
    std::array<std::uint32_t,8> retired_connections_{};
    unsigned retired_count_{};
    bool sleep_retired_{},wake_sent_{};
    std::uint64_t wake_begin_{},absence_since_{};
#ifdef ARMODULE_SETUP_SLEEP_R3
    std::uint32_t pending_peer_nonce_{};
    bool pending_retired_{};
#endif
#endif
#ifdef ARMODULE_CENTRAL_BOND_B1
    Result PollCentralBond(bool &ready);
    Service bond_service_{};
    bool bond_saved_{},bond_started_{};
    std::uint32_t bond_nonce_{},bond_last_phase_{0xffffffff};
#endif
#ifdef ARMODULE_NATIVE_PEER_C1
    Result PollPeerPreparation(std::uint64_t now,bool force=false);
    std::uint64_t peer_generation_{},peer_started_ms_{},peer_deadline_ms_{},peer_poll_ms_{};
    std::uint32_t peer_nonce_{};
#endif
#ifdef ARMODULE_NATIVE_HCI_C2
    void PollHciTrace(std::uint64_t now,bool force=false);
    std::uint64_t hci_polled_ms_{};
    std::uint32_t hci_logged_{};
    bool hci_read_failed_{};
#endif
#ifdef ARMODULE_NATIVE_OWNER
    arnativebond::Trial native_bond_trial_;
#endif
#ifdef ARMODULE_NAV17
    void LogServiceUuid(const BtdevGattService &service,std::size_t index,const char *phase);
    void ReadServiceUuidCensus(std::uint64_t instance,Result binding_before);
#endif
    Result ReadEnrollmentList(std::uint32_t &count,std::uint32_t &matches);
    void PollEnrollment(std::uint64_t now);
    void EnrollmentFinish(EnrollmentState state,Result result);
    EnrollmentRecord enrollment_{};
#ifndef ARMODULE_RECONNECT_CHECK
    bool enrollment_consumed_{};
#endif
    bool enrollment_context_consumed_{};
    const bool system_enrollment_;
    Result LoadCache(std::uint32_t connection);
    Result LoadFingerprintCache(std::uint32_t connection);
    Result ReadInputInventory(const char *label, bool adopt);
    Result Execute(const Request &request);
    Result OnOperation();
    Result RequestConnection(const SightingWire *sighting = nullptr, std::uint64_t accepted_ms = 0);
    Result PersistLinkMarker(std::uint64_t now_ms);
    Result StopOwnScan();
    Result PollScan(std::uint64_t now_ms);
    Result PollLinkOnly(std::uint64_t now_ms);
    Result PollIdentity(std::uint64_t now_ms);
    Result CheckCurrentTarget();
    Result ReadDisconnectPresence(bool &present, bool require_both=false);
    Result FinishDisconnect(Result result);
    Result ReleaseSubscriptions(bool trace_disconnect = false);
    void LogScanCacheSummary(const char *name, Result result, std::uint8_t count,
        const void *data, std::size_t size);
    void Log(const char *format, ...);
    void ResetInput();
    HogpClient client_;
    RemoteDecoder decoder_;
    Logger logger_;
    ManagedGatt gatt_;
    NativeGattRequests native_;
    bool native_open_{};
    const bool native_requests_;
    BtdrvAddress target_{};
    Event operation_event_{};
    Event discovery_event_{};
    Event scan_event_{}, connection_event_{}, pairing_event_{};
    bool initialized_{}, event_open_{}, discovery_open_{}, discovery_signalled_{}, connection_requested_{}, have_connection_{}, cache_loaded_{};
    bool hid_path_{}, information_path_{}, pending_operation_{}, mtu_requested_{};
    bool scan_event_open_{}, connection_event_open_{}, pairing_event_open_{}, scanning_{}, authenticated_reads_{};
    bool link_only_{}, link_attempted_{};
#if defined(ARMODULE_KNOWN_RECONNECT_CHECK) || defined(ARMODULE_NATIVE_PEER_C1)
    std::uint64_t known_reconnect_ready_ms_{};
#endif
    bool identity_test_{}, btdev_initialized_{}, fingerprint_logged_{};
    const bool input_diagnostic_;
    SessionBudget input_budget_;
    HealthLogGate connection_log_gate_;
    bool security_read_logged_{}, inputs_loaded_{}, input_ready_logged_{};
    std::uint64_t linked_ms_{}, last_identity_poll_ms_{};
    std::uint64_t capture_deadline_ms_{};
    std::uint64_t last_connection_poll_ms_{};
    std::uint64_t scan_started_ms_{}, last_scan_poll_ms_{};
    bool general_cache_seen_{}, smart_cache_seen_{}, general_target_dumped_{}, smart_target_dumped_{};
    std::uint8_t general_cache_count_{}, smart_cache_count_{}, general_cache_matches_{}, smart_cache_matches_{};
    Result general_cache_result_{}, smart_cache_result_{};
    std::uint32_t connection_{};
    std::uint64_t requested_ms_{};
    Request request_{};
    std::array<Request, 2> subscriptions_{};
    std::size_t subscription_count_{};
    DisconnectState disconnect_state_{DisconnectState::NotStarted};
    Result disconnect_result_{};
    std::uint64_t disconnect_started_ms_{}, disconnect_polled_ms_{};
    std::array<std::uint32_t,2> disconnect_view_trace_{{0xffffffffu,0xffffffffu}};
};
}
