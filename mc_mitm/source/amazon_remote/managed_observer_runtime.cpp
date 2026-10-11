// SPDX-License-Identifier: GPL-2.0-only
#include <stratosphere.hpp>
#include <switch.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "managed_observer.hpp"
#include "managed_observer_runtime.hpp"
#include "power_lifecycle.hpp"
#include "filter_correction_runtime.hpp"
#include "lease_scan_trace_runtime.hpp"
#include "target_sighting.hpp"
#include "capture_lease.hpp"
#include "capture_diagnostics.hpp"
#include "connection_evidence.hpp"
#include "core_security_evidence.hpp"
#include "native_metadata.hpp"
#include "bond_properties.hpp"
#ifdef ARMODULE_CENTRAL_BOND_B1
#include "central_bond_runtime.hpp"
#endif
#include "receiver_diagnostic.hpp"
#include "intent_trace_runtime.hpp"
#include "sleep_resume_trace_runtime.hpp"
#ifdef ARMODULE_SLEEP_STORAGE_GUARD_G1
#include "storage_gate.hpp"
#endif
#include "../bluetooth_mitm/bluetooth/bluetooth_ble.hpp"

namespace ams::mc::amazon_observer {
namespace {
// Bind the portable byte parser to the pinned SDK. These are layout guards,
// NOT proof that an unobserved Horizon version delivers the same callback ABI.
using ScanView = decltype(BtdrvBleEventInfo{}.scan_result);
using ConnectionView = decltype(BtdrvBleEventInfo{}.client_connection);
using FilterView = decltype(BtdrvBleEventInfo{}.scan_filter);
static_assert(BtdrvBleEventType_ScanFilter == 7 && sizeof(FilterView)==8);
static_assert(offsetof(FilterView,result)==0 && offsetof(FilterView,action)==4);
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
using CorePinView = decltype(BtdrvEventInfo{}.pairing_pin_code_request);
using CoreSspView = decltype(BtdrvEventInfo{}.ssp_request.v12);
using CoreLinkView = decltype(BtdrvEventInfo{}.connection.v12);
static_assert(sizeof(BtdrvEventInfo) == 0x400);
static_assert(BtdrvEventType_PairingPinCodeRequest == 2 && BtdrvEventType_SspRequest == 3 && BtdrvEventType_Connection == 4);
static_assert(sizeof(CorePinView) == 0x102 && offsetof(CorePinView, addr) == 0);
static_assert(sizeof(CoreSspView) == 0x108 && offsetof(CoreSspView, addr) == 0 && offsetof(CoreSspView, flag) == 0x102);
static_assert(sizeof(CoreLinkView) == 0x108 && offsetof(CoreLinkView, type) == 0 && offsetof(CoreLinkView, addr) == 4);

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
::amazon_remote::CaptureCloseTrace g_capture_close;
::amazon_remote::PowerLifecycle g_power;
#ifdef ARMODULE_WAKE_A3
os::SdkMutex g_wake_mutex;
#endif
#ifdef ARMODULE_SLEEP_STORAGE_GUARD_G1
::amazon_remote::StorageGate g_storage_gate; // Main thread only: PSC handling and both timers.
#endif
::amazon_remote::ConnectionEvidenceSnapshot g_connections;
::amazon_remote::CoreSecuritySnapshot g_core_security; // Core thread's sole writer.
::amazon_remote::native_metadata::Snapshot g_native_metadata;
::amazon_remote::bond_properties::Snapshot g_bond_properties;
::amazon_remote::ReceiverDiagnostic g_receiver;
::amazon_remote::ReceiverRecordWriter g_receiver_writer; // Main thread only.
bool g_receiver_suspended{}; // Main thread: no new storage work after Suspend.
std::atomic<std::uint64_t> g_receiver_begin{}; // Published once after successful Arm.
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
// Record-header <=1023, each raw-AD line <128, intent <512, BLE prefix <448;
// two counter lines <1024 each, with explicit newline room. No group can fill
// this batch before its fixed tick budget ends, even with ten ADs per target.
static_assert(sizeof(g_batch) >= DrainBudget * (1024 + 10*128 + 11) +
    TraceDrainBudget * (512 + 1 + 448 + 1) + 2 * (1024 + 1));
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
    sleep_resume_trace::Initialize(); // Independent opt-in; no target address is needed for prefix-only metadata.
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
#ifdef ARMODULE_CENTRAL_BOND_B1
    central_bond_runtime::Initialize(target);
#endif
    if (!g_sighting.Configure(target)) { g_configured = false; return; }
    if (!g_connections.Configure(target)) { g_configured = false; return; }
    if (!g_core_security.Configure(target)) { g_configured = false; return; }
    if (!g_native_metadata.Configure(target)) { g_configured = false; return; }
    if (!g_bond_properties.Configure(target)) { g_configured = false; return; }
    // Snapshot's sole controlling thread is Startup/Main. IPC only controls
    // the atomic lease; it must never mutate snapshot generations.
    g_sighting.SetEnabled(g_diagnostic_device_zero);
    g_start_tick = os::GetSystemTick();
    // Capture HOS startup intents before LaunchModules creates the producer.
    // Its wrapper enforces an absolute deadline even if initialization blocks.
    ble_intent_trace::Initialize();
    amazon_filter_correction::Initialize();
    lease_scan_trace::Initialize();
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
    AddLine("observer=passive-public-mc-v8 begin=1 window_ms=%lld clock_anchored_before_modules=1 sdk_layout_checked=1 callback_abi_not_device_verified=1 scan_changed=0 gatt_owned=0 identity_verified=0 intent_early_capture=%u diagnostic_device_zero_opt_in=%u; startup_logging_separate_from_one_shot_capture_lease=1 capture_requires_explicit_arm=1 target_connection_evidence_capacity=8 error_callbacks_retained=1 core_security_metadata_only=1 bond_completion_unproven=1", static_cast<long long>(WindowMilliseconds), ble_intent_trace::Enabled(), g_diagnostic_device_zero);
    if (!FlushBatch()) return;
    if (UptimeMilliseconds() >= WindowMilliseconds) {
        DisableLog();
        return;
    }
    g_observer.SetEnabled(true);
}

#ifdef ARMODULE_WAKE_A3
void PollHandshake() {
#ifdef ARMODULE_SLEEP_STORAGE_GUARD_G1
    if(!g_storage_gate.Open())return; // saved-record load/save is storage work
#endif
    // Runtime guards skip I/O while suspended/unconfigured and after the one
    // initial load unless peer preparation or security work is outstanding.
    central_bond_runtime::Poll();
}
#endif

void Poll() {
    sleep_resume_trace::Poll(); // Its own opt-in persists only after acknowledged wake.
#ifdef ARMODULE_SLEEP_STORAGE_GUARD_G1
    // No diagnostic writer, log flush or bond save starts during a power transition.
    if(!g_storage_gate.Open())return;
#endif
#if defined(ARMODULE_CENTRAL_BOND_B1) && !defined(ARMODULE_WAKE_A3)
    central_bond_runtime::Poll();
#endif
    amazon_filter_correction::Poll();
    lease_scan_trace::Poll();
    // Retained receiver counters are saved once on the existing maintenance
    // thread AFTER the lease ends, independently of startup log capacity.
    // No radio call, new thread/client/getter, log rotation, or capture rearm.
    const auto receiver_begin=g_receiver_begin.load(std::memory_order_acquire);
    if(receiver_begin && !g_receiver_suspended && !g_receiver_writer.Attempted()) {
        const auto now=armTicksToNs(armGetSystemTick())/1000000;
        const auto view=g_capture.ReadStatus(now);
        char path[128]{};
        std::snprintf(path,sizeof path,"sdmc:/config/amazon-remote/receiver-capture-%llu.bin",
            static_cast<unsigned long long>(receiver_begin));
        struct Storage {
            const char *path;
            bool Create(std::size_t size){return R_SUCCEEDED(fs::CreateFile(path,size));}
            bool Write(const ::amazon_remote::ReceiverRecord &r) {
                fs::FileHandle file{};
                if(R_FAILED(fs::OpenFile(&file,path,fs::OpenMode_Write)))return false;
                ON_SCOPE_EXIT {fs::CloseFile(file);};
                return R_SUCCEEDED(fs::WriteFile(file,0,&r,sizeof r,fs::WriteOption::Flush));
            }
            bool Read(::amazon_remote::ReceiverRecord &r) {
                fs::FileHandle file{};
                if(R_FAILED(fs::OpenFile(&file,path,fs::OpenMode_Read)))return false;
                ON_SCOPE_EXIT {fs::CloseFile(file);};
                s64 size{};std::size_t actual{};
                return R_SUCCEEDED(fs::GetFileSize(&size,file)) && size==sizeof r &&
                    R_SUCCEEDED(fs::ReadFile(&actual,file,0,&r,sizeof r)) && actual==sizeof r;
            }
        } storage{path};
        (void)g_receiver_writer.Save(receiver_begin,now,view.state,g_receiver.Read(),storage);
    }
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

namespace {
void CloseForPower(::amazon_remote::CaptureCloseReason reason) {
    // Close admission before touching secondary runtimes. No new Arm can win.
    g_capture.End();
#ifdef ARMODULE_CENTRAL_BOND_B1
    central_bond_runtime::Suspend();
#endif
    g_capture_close.Record(reason, armTicksToNs(armGetSystemTick()) / 1000000);
    amazon_filter_correction::Suspend();
    lease_scan_trace::Suspend();
    g_receiver_suspended=true;
    // No filesystem calls or waits before the existing PSC acknowledgement.
    // No automatic rearm on wake; only an explicitly opted-in fresh boot starts.
    DisableLog();
}
struct PowerAdapter {
#ifdef ARMODULE_WAKE_A3
    bool PauseSession() {
        if(!central_bond_runtime::PauseSessionForPower())return false;
        g_capture.End();
        g_capture_close.Record(::amazon_remote::CaptureCloseReason::SleepRequest,armTicksToNs(armGetSystemTick())/1000000);
        DisableLog();return true;
    }
    bool ResumeSession(){return central_bond_runtime::ResumeSessionAfterPower();}
#endif
    bool PauseUnused() {
        if(!g_capture.PauseUnusedForPower())return false;
#ifdef ARMODULE_CENTRAL_BOND_B1
        if(!central_bond_runtime::PauseUnusedForPower())return false;
#endif
        // No capture ever began: scan/filter/receiver runtimes have nothing to
        // resume. Their existing Live checks deny work while the lease is paused.
        DisableLog();
        return true;
    }
    bool ResumeUnused() {
#ifdef ARMODULE_CENTRAL_BOND_B1
        if(!central_bond_runtime::ResumeUnusedAfterPower())return false;
#endif
        return g_capture.ResumeUnusedAfterPower();
    }
    void Close(::amazon_remote::CaptureCloseReason reason){CloseForPower(reason);}
};
}
void Suspend(bool shutdown) {
#ifdef ARMODULE_WAKE_A3
    std::scoped_lock lock(g_wake_mutex);
#endif
    CloseForPower(shutdown ? ::amazon_remote::CaptureCloseReason::ShutdownRequest :
        ::amazon_remote::CaptureCloseReason::SleepRequest);
}
void PowerRequest(std::uint32_t state,std::uint32_t flags,std::uint32_t result) {
#ifdef ARMODULE_SLEEP_STORAGE_GUARD_G1
    g_storage_gate.Request(state,result); // close before any handler or later IPC End
#endif
    sleep_resume_trace::PowerRequest(state,flags,result);
#ifdef ARMODULE_WAKE_A3
    std::scoped_lock lock(g_wake_mutex);
#endif
    PowerAdapter adapter;
    g_power.Request(state,flags,result,armTicksToNs(armGetSystemTick())/1000000,adapter);
}
void PowerAcknowledge(std::uint32_t result) {
    sleep_resume_trace::PowerAcknowledge(result);
#ifdef ARMODULE_WAKE_A3
    std::scoped_lock lock(g_wake_mutex);
#endif
    PowerAdapter adapter;g_power.Acknowledge(result,adapter);
#ifdef ARMODULE_SLEEP_STORAGE_GUARD_G1
    g_storage_gate.Acknowledge(result);
#endif
}
#ifdef ARMODULE_WAKE_A3
bool ResumeSavedSession(std::uint64_t caller,const ::amazon_remote::central_bond::WakeRequest &request){
    std::scoped_lock lock(g_wake_mutex);
    if(g_capture.Status(armTicksToNs(armGetSystemTick())/1000000)!=::amazon_remote::CaptureLeaseState::Ended ||
       !central_bond_runtime::ResumeSavedSession(caller,request))return false;
    return g_capture.PermitAfterSleep();
}
#endif

void Observe(std::uint32_t type, const void *event, std::size_t size) {
    sleep_resume_trace::ObserveBle(type,event,size);
#ifdef ARMODULE_CENTRAL_BOND_B1
    central_bond_runtime::ObserveBle(type,event,size);
#endif
    // Existing one-copy event tap; never initialize/read/consume another event.
    lease_scan_trace::ObserveManaged(type,event,size);
    // Exact-target, bounded passive capture only while the explicit lease is
    // active. Independent of startup tracing/logging; no Bluetooth calls.
    const auto now = armTicksToNs(armGetSystemTick()) / 1000000;
    if (g_diagnostic_device_zero && g_capture.Active(now)) {
        g_receiver.Observe(type,static_cast<const std::uint8_t *>(event),size,g_target,true);
        g_sighting.Observe(type, static_cast<const std::uint8_t *>(event), size, now);
        g_connections.Observe(type, static_cast<const std::uint8_t *>(event), size, now);
    }
    if (ble_intent_trace::Enabled() && ::amazon_remote::IsBleHeaderEvent(type)) {
        const bool hint = ::amazon_remote::BleHeaderConfiguredAddressHint(type,event,size,g_target);
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
bool ReadCaptureDiagnostics(void *out, std::size_t size) {
    // Same metadata-only getter, exact-size overload. No radio or key access.
    if(out && size==sizeof(::amazon_remote::PowerHistoryWire)) {
        ::amazon_remote::PowerHistoryWire wire{};
        if(!g_power.Read(wire,armTicksToNs(armGetSystemTick())/1000000))return false;
        std::memcpy(out,&wire,sizeof wire);return true;
    }
    if (!out || size != sizeof(::amazon_remote::CaptureDiagnosticsWire)) return false;
    const auto now = armTicksToNs(armGetSystemTick()) / 1000000;
    const auto before = g_capture.ReadStatus(now);
    const auto close = g_capture_close.Read();
    const auto after = g_capture.ReadStatus(now);
    const auto wire = ::amazon_remote::DescribeCapture(before, after, close, g_configured && g_diagnostic_device_zero);
    std::memcpy(out, &wire, sizeof wire);
    return true;
}
bool ReadConnectionEvidence(void *out, std::size_t size) {
    if (!g_configured || !g_diagnostic_device_zero || !out || size!=sizeof(::amazon_remote::ConnectionEvidenceWire)) return false;
    ::amazon_remote::ConnectionEvidenceWire wire{};
    if (!g_connections.Read(wire)) return false;
    std::memcpy(out,&wire,sizeof wire);
    return true;
}
void ObserveCore(std::uint32_t type, const void *event, std::size_t size) {
    // Independent of startup logging/log capacity. No new event consumer,
    // filesystem/IPC/heap/wait, confirmation or security response is added.
    const auto now = armTicksToNs(armGetSystemTick()) / 1000000;
    (void)g_core_security.Observe(type, static_cast<const std::uint8_t *>(event), size,
        now, g_configured && g_diagnostic_device_zero && g_capture.Active(now),
        hos::GetVersion() >= hos::Version_12_0_0);
}
bool ReadCoreSecurityEvidence(void *out, std::size_t size) {
    if (!g_configured || !g_diagnostic_device_zero || !out ||
        size != sizeof(::amazon_remote::CoreSecurityWire)) return false;
    ::amazon_remote::CoreSecurityWire wire{};
    if (!g_core_security.Read(wire)) return false;
    std::memcpy(out, &wire, sizeof wire);
    return true;
}
bool BeginNativeMetadataProbe(std::uint32_t nonce) {
    if(!g_configured || !g_diagnostic_device_zero || hos::GetVersion()!=hos::Version_22_5_0)return false;
    const auto now=armTicksToNs(armGetSystemTick())/1000000;
    return g_native_metadata.Begin(nonce,now) && g_bond_properties.Begin(nonce,now);
}
void ObserveNativeMetadata(const void *event,std::size_t size) {
    if(!g_configured || hos::GetVersion()!=hos::Version_22_5_0)return;
    const auto now=armTicksToNs(armGetSystemTick())/1000000;
    (void)g_native_metadata.Observe(event,size,now);
    (void)g_bond_properties.Observe(event,size,now);
}
bool ReadNativeMetadata(void *out,std::size_t size) {
    if(!out || size!=sizeof(::amazon_remote::native_metadata::Wire))return false;
    ::amazon_remote::native_metadata::Wire wire{};
    if(!g_native_metadata.Read(wire))return false;
    std::memcpy(out,&wire,sizeof wire);return true;
}
bool ReadBondProperties(void *out,std::size_t size) {
    if(!out || size!=sizeof(::amazon_remote::bond_properties::Wire))return false;
    ::amazon_remote::bond_properties::Wire wire{};
    if(!g_bond_properties.Read(wire))return false;
    std::memcpy(out,&wire,sizeof wire);return true;
}
bool ArmTargetCapture() {
#ifdef ARMODULE_WAKE_A3
    std::scoped_lock lock(g_wake_mutex);
#endif
    if(!g_configured || !g_diagnostic_device_zero)return false;
    const auto now=armTicksToNs(armGetSystemTick())/1000000;
    if(!g_capture.Arm(now))return false;
    g_receiver_begin.store(now,std::memory_order_release);
    lease_scan_trace::Arm(now);
    return true;
}
void EndTargetCapture() {
#ifdef ARMODULE_WAKE_A3
    std::scoped_lock lock(g_wake_mutex);
#endif
    g_capture_close.Record(::amazon_remote::CaptureCloseReason::ExplicitEnd, armTicksToNs(armGetSystemTick()) / 1000000);
    g_capture.End();
}
}
