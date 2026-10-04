// SPDX-License-Identifier: GPL-2.0-only
#include <stratosphere.hpp>
#include <switch.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "managed_observer.hpp"
#include "managed_observer_runtime.hpp"
#include "target_sighting.hpp"
#include "capture_lease.hpp"
#include "intent_trace_runtime.hpp"
#include "../bluetooth_mitm/bluetooth/bluetooth_ble.hpp"

namespace ams::mc::amazon_observer {
namespace {
// Bind the portable byte parser to the pinned SDK. These are layout guards,
// NOT proof that an unobserved Horizon version delivers the same callback ABI.
using ScanView = decltype(BtdrvBleEventInfo{}.scan_result);
using ConnectionView = decltype(BtdrvBleEventInfo{}.client_connection);
static_assert(sizeof(BtdrvBleEventInfo) == 0x400);
static_assert(sizeof(BtdrvBleAdvertisement) == 31);
static_assert(BtdrvBleEventType_ScanResult == ::amazon_remote::ManagedObserver::ScanEventType);
static_assert(BtdrvBleEventType_ClientConnection == ::amazon_remote::ManagedObserver::ConnectionEventType);
static_assert(sizeof(ScanView) == ::amazon_remote::ManagedObserver::ScanBytes);
static_assert(offsetof(ScanView, result) == 0 && offsetof(ScanView, status) == 4);
static_assert(offsetof(ScanView, device_type) == 5 && offsetof(ScanView, ble_addr_type) == 6);
static_assert(offsetof(ScanView, address) == 7 && offsetof(ScanView, ad_list) == 13);
static_assert(offsetof(ScanView, count) == 323 && offsetof(ScanView, rssi) == 324);
static_assert(sizeof(ConnectionView) == ::amazon_remote::ManagedObserver::ConnectionBytes);
static_assert(offsetof(ConnectionView, result) == 0 && offsetof(ConnectionView, status) == 4);
static_assert(offsetof(ConnectionView, client_if) == 5 && offsetof(ConnectionView, conn_id) == 8);
static_assert(offsetof(ConnectionView, address) == 12 && offsetof(ConnectionView, reason) == 18);
static_assert(sizeof(BtdrvAddress) == 6);

constexpr char ModePath[] = "sdmc:/config/amazon-remote/observer-mode.txt";
constexpr char AddressPath[] = "sdmc:/config/amazon-remote/address.txt";
constexpr char TargetCapturePath[] = "sdmc:/config/amazon-remote/target-capture-mode.txt";
constexpr char LogPath[] = "sdmc:/config/amazon-remote/managed-observer.log";
constexpr s64 MaxLogBytes = 64 * 1024;
constexpr s64 WindowMilliseconds = 120000;
constexpr unsigned DrainBudget = 2;
constexpr unsigned TraceDrainBudget = 4;

::amazon_remote::ManagedObserver g_observer;
::amazon_remote::TargetSightingSnapshot g_sighting;
::amazon_remote::OneShotCaptureLease g_capture;
// Only Startup/Main accesses the following state; BLE accesses g_observer only.
bool g_configured{}, g_started{}, g_session_active{}, g_output_failed{};
bool g_diagnostic_device_zero{};
::amazon_remote::ManagedAddress g_target; // Immutable before either producer starts.
s64 g_log_offset{};
os::Tick g_start_tick;
unsigned g_poll_count{};
// Static scratch avoids spending MissionControl's small Main-thread stack.
::amazon_remote::ManagedRecord g_record;
::amazon_remote::ManagedObserverCounters g_counts;
::amazon_remote::IntentRecord g_intent;
::amazon_remote::BleHeaderRecord g_header;
::amazon_remote::IntentCounters g_intent_counts;
::amazon_remote::BleHeaderCounters g_header_counts;
u32 g_managed_drained{}, g_intent_drained{}, g_header_drained{};
char g_line[1024];
char g_trace_line[512];
char g_batch[12288];
// Record-header <=1023, each raw-AD line <128, intent <512, BLE prefix <384;
// two counter lines <1024 each, with explicit newline room. No group can fill
// this batch before its fixed tick budget ends, even with ten ADs per target.
static_assert(sizeof(g_batch) >= DrainBudget * (1024 + 10*128 + 11) +
    TraceDrainBudget * (512 + 1 + 384 + 1) + 2 * (1024 + 1));
size_t g_batch_size{};

bool ReadBounded(const char *path, char *out, size_t capacity, size_t *length) {
    fs::FileHandle file{};
    if (R_FAILED(fs::OpenFile(&file, path, fs::OpenMode_Read))) return false;
    ON_SCOPE_EXIT { fs::CloseFile(file); };
    s64 size = 0;
    if (R_FAILED(fs::GetFileSize(&size, file)) || size <= 0 || static_cast<u64>(size) > capacity) return false;
    size_t actual = 0;
    if (R_FAILED(fs::ReadFile(&actual, file, 0, out, static_cast<size_t>(size))) || actual != static_cast<size_t>(size)) return false;
    *length = actual;
    return true;
}

s64 UptimeMilliseconds() {
    return (os::GetSystemTick() - g_start_tick).ToTimeSpan().GetMilliSeconds();
}

void DisableLog() {
    g_observer.SetEnabled(false);
    ble_intent_trace::Disable();
    g_session_active = false;
}

bool AddLine(const char *format, ...) {
    if (g_output_failed) return false;
    va_list args;
    va_start(args, format);
    const int size = std::vsnprintf(g_line, sizeof(g_line), format, args);
    va_end(args);
    if (size < 0 || static_cast<size_t>(size) >= sizeof(g_line) ||
        static_cast<size_t>(size) + 1 > sizeof(g_batch) - g_batch_size) {
        g_output_failed = true;
        return false;
    }
    std::memcpy(g_batch + g_batch_size, g_line, size);
    g_batch_size += size;
    g_batch[g_batch_size++] = '\n';
    return true;
}

bool FlushBatch() {
    // Never truncate/rotate somebody's existing log. At the cap, fail closed;
    // retrieve/archive the file before explicitly enabling another boot session.
    if (!g_session_active || g_output_failed || g_log_offset > MaxLogBytes ||
        g_batch_size > static_cast<size_t>(MaxLogBytes - g_log_offset)) {
        DisableLog();
        return false;
    }
    if (g_batch_size != 0) {
        fs::FileHandle file{};
        if (R_FAILED(fs::OpenFile(&file, LogPath, fs::OpenMode_Write | fs::OpenMode_AllowAppend))) {
            DisableLog();
            return false;
        }
        ON_SCOPE_EXIT { fs::CloseFile(file); };
        s64 actual_size = 0;
        // An external edit/removal is not silently overwritten or recreated.
        if (R_FAILED(fs::GetFileSize(&actual_size, file)) || actual_size != g_log_offset ||
            R_FAILED(fs::WriteFile(file, g_log_offset, g_batch, g_batch_size, fs::WriteOption::Flush))) {
            DisableLog();
            return false;
        }
        g_log_offset += g_batch_size;
        g_batch_size = 0;
    }
    // No persistent writer handle: FTP can retrieve between maintenance ticks.
    return true;
}

void AddRecord(const ::amazon_remote::ManagedRecord &record) {
    const auto &address = record.address;
    AddLine("drain_uptime_ms=%lld event=%u result=%08x status=%u target=%02x:%02x:%02x:%02x:%02x:%02x device_type=%u address_type=%u count=%u rssi=%d client_if=%u connection_id=%u reason=%u diagnostic_only=%u device_type_exception=%u ownership=unproven identity_unverified=1",
        static_cast<long long>(UptimeMilliseconds()), record.event_type, record.result, record.status,
        address[0], address[1], address[2], address[3], address[4], address[5],
        record.device_type, record.address_type, record.count, record.rssi,
        record.client_if, record.connection_id, record.reason,
        record.diagnostic_only, record.device_type_exception);
    for (unsigned i = 0; i < record.count; ++i) {
        // Always preserve fixed 29-byte SDK data; size is evidence, NOT a memcpy length.
        char hex[59];
        constexpr char digits[] = "0123456789abcdef";
        for (unsigned j = 0; j < record.ads[i].data.size(); ++j) {
            hex[2*j] = digits[record.ads[i].data[j] >> 4];
            hex[2*j+1] = digits[record.ads[i].data[j] & 15];
        }
        hex[58] = 0;
        AddLine("  ad[%u] sdk_size=%u type=%02x raw29=%s size_convention=unverified", i, record.ads[i].size, record.ads[i].type, hex);
    }
}

void AddCounters(const char *phase) {
    g_observer.SnapshotInto(g_counts);
    AddLine("uptime_ms=%lld phase=%s ble_reader_initialized=%u counts_approximate=1 seen=%u disabled=%u scan=%u scan_started=%u scan_complete=%u scan_device=%u client_connection=%u target_hits=%u other_address=%u rejected_counts=%u invalid_fields=%u invalid_input=%u short_input=%u queued=%u observer_drops=%u; no_scan_or_gatt_calls=1 pairing_not_attempted=1",
        static_cast<long long>(UptimeMilliseconds()), phase, ams::bluetooth::ble::IsInitialized(),
        g_counts.seen, g_counts.disabled, g_counts.type_seen[6], g_counts.scan_status_seen[255],
        g_counts.scan_status_seen[1], g_counts.scan_status_seen[2], g_counts.type_seen[4],
        g_counts.target_hits, g_counts.other_address, g_counts.rejected_counts, g_counts.invalid_fields,
        g_counts.invalid_input, g_counts.short_input, g_counts.queued, g_counts.drops);
    ble_intent_trace::SnapshotInto(g_intent_counts);
    ble_intent_trace::HeaderSnapshotInto(g_header_counts);
    AddLine("phase=%s rejection_predicates_overlap=1 discoveries_nonzero_result=%u status_not_newdevice=%u discoveries_device_type_bad=%u discoveries_address_type_bad=%u diagnostic_zero_captured=%u intent_seen=%u intent_queued=%u intent_drops=%u intent_rejected=%u header_seen=%u header_queued=%u header_drops=%u header_short=%u drained_managed=%u drained_intent=%u drained_header=%u; driver_results_not_traced=1 callbacks_uncorrelated=1 startup_drop_or_unlogged_queue_is_inconclusive=1",
        phase, g_counts.nonzero_result, g_counts.status_not_newdevice, g_counts.device_type_bad, g_counts.address_type_bad,
        g_counts.diagnostic_zero_captured,
        g_intent_counts.seen, g_intent_counts.queued, g_intent_counts.drops, g_intent_counts.rejected,
        g_header_counts.seen, g_header_counts.queued, g_header_counts.drops, g_header_counts.short_input,
        g_managed_drained, g_intent_drained, g_header_drained);
}
}

void Initialize() {
    char mode[9], address[19];
    size_t mode_size = 0, address_size = 0;
    if (!ReadBounded(ModePath, mode, sizeof(mode), &mode_size)) return;
    const bool passive = (mode_size == 7 && std::memcmp(mode, "passive", 7) == 0) ||
        (mode_size == 8 && std::memcmp(mode, "passive\n", 8) == 0) ||
        (mode_size == 9 && std::memcmp(mode, "passive\r\n", 9) == 0);
    if (!passive || !ReadBounded(AddressPath, address, sizeof(address), &address_size)) return;
    ::amazon_remote::ManagedAddress target{};
    if (!::amazon_remote::StrictParseAddress(address, address_size, target)) return;
    char capture_mode[32];
    size_t capture_size = 0;
    if (ReadBounded(TargetCapturePath, capture_mode, sizeof(capture_mode), &capture_size)) {
        constexpr char expected[] = "diagnostic-device-zero";
        size_t text_size = capture_size;
        if (text_size && capture_mode[text_size - 1] == '\n') {
            --text_size;
            if (text_size && capture_mode[text_size - 1] == '\r') --text_size;
        }
        if (text_size != sizeof(expected) - 1 || std::memcmp(capture_mode, expected, text_size) != 0) return;
        g_diagnostic_device_zero = true;
    }
    const auto capture = g_diagnostic_device_zero ?
        ::amazon_remote::TargetCaptureMode::DiagnosticAllowDeviceZero :
        ::amazon_remote::TargetCaptureMode::StrictSdk;
    g_configured = g_observer.ConfigureTarget(target, capture);
    if (!g_configured) return;
    g_target = target;
    if (!g_sighting.Configure(target)) { g_configured = false; return; }
    // Snapshot's sole controlling thread is Startup/Main. IPC only controls
    // the atomic lease; it must never mutate snapshot generations.
    g_sighting.SetEnabled(g_diagnostic_device_zero);
    g_start_tick = os::GetSystemTick();
    // Capture HOS startup intents before LaunchModules creates the producer.
    // Its wrapper enforces an absolute deadline even if initialization blocks.
    ble_intent_trace::Initialize();
}

void Start() {
    if (!g_configured || g_started) return;
    g_started = true;
    {
        fs::FileHandle file{};
        const int mode = fs::OpenMode_Write | fs::OpenMode_AllowAppend;
        if (R_FAILED(fs::OpenFile(&file, LogPath, mode))) {
            // CreateFile does not replace an existing file. No truncate fallback.
            if (R_FAILED(fs::CreateFile(LogPath, 0)) || R_FAILED(fs::OpenFile(&file, LogPath, mode))) {
                DisableLog();
                return;
            }
        }
        ON_SCOPE_EXIT { fs::CloseFile(file); };
        if (R_FAILED(fs::GetFileSize(&g_log_offset, file)) || g_log_offset < 0 || g_log_offset >= MaxLogBytes) {
            DisableLog();
            return;
        }
    }
    g_session_active = true;
    AddLine("observer=passive-public-mc-v5 begin=1 window_ms=%lld clock_anchored_before_modules=1 sdk_layout_checked=1 callback_abi_not_device_verified=1 scan_changed=0 gatt_owned=0 identity_verified=0 intent_early_capture=%u diagnostic_device_zero_opt_in=%u; startup_logging_separate_from_one_shot_capture_lease=1 capture_requires_explicit_arm=1", static_cast<long long>(WindowMilliseconds), ble_intent_trace::Enabled(), g_diagnostic_device_zero);
    if (!FlushBatch()) return;
    if (UptimeMilliseconds() >= WindowMilliseconds) {
        DisableLog();
        return;
    }
    g_observer.SetEnabled(true);
}

void Poll() {
    if (!g_session_active) return;
    if (UptimeMilliseconds() >= WindowMilliseconds) {
        g_observer.SetEnabled(false);
        ble_intent_trace::Disable();
        AddCounters("deadline");
        (void)FlushBatch();
        DisableLog();
        return;
    }
    for (unsigned i = 0; i < TraceDrainBudget && ble_intent_trace::Pop(g_intent); ++i) {
        ++g_intent_drained;
        if (!::amazon_remote::FormatIntent(g_trace_line, sizeof(g_trace_line), g_intent)) { g_output_failed = true; break; }
        AddLine("%s", g_trace_line);
    }
    for (unsigned i = 0; i < TraceDrainBudget && ble_intent_trace::PopManagedHeader(g_header); ++i) {
        ++g_header_drained;
        if (!::amazon_remote::FormatBleHeader(g_trace_line, sizeof(g_trace_line), g_header)) { g_output_failed = true; break; }
        AddLine("%s", g_trace_line);
    }
    for (unsigned i = 0; i < DrainBudget && g_observer.Pop(g_record); ++i) { ++g_managed_drained; AddRecord(g_record); }
    if ((++g_poll_count % 5) == 0) AddCounters("running");
    (void)FlushBatch();
}

void Suspend() {
    // No filesystem calls or waits before the existing PSC acknowledgement.
    // No automatic rearm on wake; only an explicitly opted-in fresh boot starts.
    DisableLog();
    g_capture.End(); // Terminal; no automatic rearm on resume.
}

void Observe(std::uint32_t type, const void *event, std::size_t size) {
    // Exact-target, bounded passive capture only while the explicit lease is
    // active. Independent of startup tracing/logging; no Bluetooth calls.
    const auto now = armTicksToNs(armGetSystemTick()) / 1000000;
    if (g_diagnostic_device_zero && g_capture.Active(now))
        g_sighting.Observe(type, static_cast<const std::uint8_t *>(event), size, now);
    if (ble_intent_trace::Enabled() && type == ::amazon_remote::ManagedObserver::ScanEventType) {
        const bool hint = ::amazon_remote::MatchesConfiguredAddressAtSdkOffset(event, size, g_target);
        ble_intent_trace::ObserveManagedHeader(type, event, size, hint);
    }
    if (!g_observer.Enabled()) return;
    if (UptimeMilliseconds() >= WindowMilliseconds) {
        g_observer.SetEnabled(false);
        ble_intent_trace::Disable();
        return;
    }
    (void)g_observer.Observe(type, static_cast<const std::uint8_t *>(event), size);
}

bool ReadTargetSighting(void *out, std::size_t size) {
    if (!out || size != sizeof(::amazon_remote::SightingWire)) return false;
    ::amazon_remote::SightingWire wire{};
    // Fixed retry bound; no locks, driver calls or filesystem operations.
    if (g_capture.Active(armTicksToNs(armGetSystemTick()) / 1000000))
        (void)g_sighting.Read(wire);
    if (!g_capture.Active(armTicksToNs(armGetSystemTick()) / 1000000))
        wire = {};
    std::memcpy(out, &wire, sizeof wire);
    return true;
}
bool ReadCaptureStatus(void *out, std::size_t size) {
    if (!out || size != sizeof(::amazon_remote::CaptureLeaseWire)) return false;
    const auto now = armTicksToNs(armGetSystemTick()) / 1000000;
    auto wire = g_capture.ReadStatus(now);
    if (!g_configured || !g_diagnostic_device_zero) {
        wire.state = static_cast<std::uint32_t>(::amazon_remote::CaptureLeaseState::Unavailable);
        wire.deadline_boot_ms = 0;
    }
    std::memcpy(out, &wire, sizeof wire);
    return true;
}
bool ArmTargetCapture() {
    return g_configured && g_diagnostic_device_zero &&
        g_capture.Arm(armTicksToNs(armGetSystemTick()) / 1000000);
}
void EndTargetCapture() { g_capture.End(); }
}
