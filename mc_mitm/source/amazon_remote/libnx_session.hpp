// Independently implemented public libnx bt/btmu integration. GPL-2.0-only.
#pragma once
#include <switch.h>
#include "hogp_client.hpp"
#include "remote_decoder.hpp"
#include "target_sighting.hpp"

namespace amazon_remote {
using Logger = void (*)(const char *message);
// One-shot getter-only census. Opens/closes btmu, does not initialize bt,
// register data paths, start/stop a scan, connect, pair or emit controller input.
Result ReadOnlyScanCacheCensus(BtdrvAddress target, Logger logger);
// Single-thread owner. Uses btdev's user path; never consumes btdrv managed events.
class LibnxSession {
public:
    explicit LibnxSession(Logger logger, bool fingerprint_only = false, bool input_diagnostic = false);
    Result Initialize(BtdrvAddress target);
    Result InitializeLinkOnly(BtdrvAddress target);
    Result InitializeIdentityTest(BtdrvAddress target);
    Result StartLinkOnlyScan(std::uint64_t now_ms);
    Result ConfirmObservedLink(const SightingWire &sighting, std::uint64_t now_ms, std::uint64_t capture_deadline_ms = 0);
    bool ScanActive() const { return scanning_; }
    bool LinkAttempted() const { return link_attempted_; }
    bool LinkConfirmed() const { return link_only_ && have_connection_; }
    std::uint64_t ScanStarted() const { return scan_started_ms_; }
    // Connects only the explicitly configured physical remote. No ambient auto-pairing.
    Result Connect(bool filtered_discovery = false, bool authenticated_reads = false);
    // Nonblocking polling; caller supplies a monotonic time in milliseconds.
    Result Poll(std::uint64_t now_ms);
    void Close();
    bool Ready() const { return client_.GetPhase() == Phase::Ready; }
    std::uint32_t Buttons() const { return decoder_.Buttons(); }
    Phase GetPhase() const { return client_.GetPhase(); }
    Error GetError() const { return client_.GetError(); }
    bool FingerprintObserved() const { return fingerprint_logged_; }
    bool SecurityReadObserved() const { return security_read_logged_; }
private:
    Result LoadCache(std::uint32_t connection);
    Result LoadFingerprintCache(std::uint32_t connection);
    Result ReadInputInventory(const char *label, bool adopt);
    Result Execute(const Request &request);
    Result OnOperation();
    Result RequestConnection(const SightingWire *sighting = nullptr);
    Result StopOwnScan();
    Result PollScan(std::uint64_t now_ms);
    Result PollLinkOnly(std::uint64_t now_ms);
    Result PollIdentity(std::uint64_t now_ms);
    Result CheckCurrentTarget();
    void LogScanCacheSummary(const char *name, Result result, std::uint8_t count,
        const void *data, std::size_t size);
    void Log(const char *format, ...);
    void ResetInput();
    HogpClient client_;
    RemoteDecoder decoder_;
    Logger logger_;
    BtdrvAddress target_{};
    Event operation_event_{};
    Event discovery_event_{};
    Event scan_event_{}, connection_event_{}, pairing_event_{};
    bool initialized_{}, event_open_{}, discovery_open_{}, discovery_signalled_{}, connection_requested_{}, have_connection_{}, cache_loaded_{};
    bool hid_path_{}, information_path_{}, pending_operation_{}, mtu_requested_{};
    bool scan_event_open_{}, connection_event_open_{}, pairing_event_open_{}, scanning_{}, authenticated_reads_{};
    bool link_only_{}, link_attempted_{};
    bool identity_test_{}, btdev_initialized_{}, fingerprint_logged_{};
    const bool input_diagnostic_;
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
};
}
