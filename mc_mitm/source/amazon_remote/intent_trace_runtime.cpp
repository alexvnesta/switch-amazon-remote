// SPDX-License-Identifier: GPL-2.0-only
#include <stratosphere.hpp>
#include <switch.h>
#include "intent_trace_runtime.hpp"
namespace ams::mc::ble_intent_trace {
namespace {
::amazon_remote::IntentTrace g_trace;
::amazon_remote::BleHeaderTrace g_headers;
bool g_initialized{}; // Startup/Main only. Never read by the producer.
u64 g_deadline_boot_ms{}; // Immutable once Initialize returns, before producers launch.
constexpr char ModePath[] = "sdmc:/config/amazon-remote/intent-trace-mode.txt";
static_assert(ncm::SystemProgramId::Hid.value == ::amazon_remote::HidProgram);
static_assert(ncm::SystemProgramId::Btm.value == ::amazon_remote::BtmProgram);
static_assert(sizeof(BtdrvBleAdvertiseFilter) == 62);
static_assert(sizeof(BtdrvGattAttributeUuid) == 20);
static_assert(offsetof(BtdrvBleAdvertiseFilter, index) == 0);
static_assert(offsetof(BtdrvBleAdvertiseFilter, adv) == 1);
static_assert(offsetof(BtdrvBleAdvertiseFilter, mask) == 32);
static_assert(offsetof(BtdrvBleAdvertiseFilter, mask_size) == 61);
using ScanView = decltype(BtdrvBleEventInfo{}.scan_result);
static_assert(BtdrvBleEventType_ScanResult == 6);
static_assert(offsetof(ScanView, result) == 0);
static_assert(offsetof(ScanView, status) == 4);
static_assert(offsetof(ScanView, device_type) == 5);
static_assert(offsetof(ScanView, ble_addr_type) == 6);
static_assert(offsetof(ScanView, address) == 7); // The evidence copy stops BEFORE any address.
bool CheckWindow(u64 boot_ms) {
    if (!::amazon_remote::IntentWindowOpen(boot_ms, g_deadline_boot_ms)) {
        g_trace.SetEnabled(false);
        g_headers.SetEnabled(false);
        return false;
    }
    return true;
}
}
void Initialize() {
    if (g_initialized) return;
    g_initialized = true;
    const auto start_boot_ms = static_cast<u64>(os::GetSystemTick().ToTimeSpan().GetMilliSeconds());
    g_deadline_boot_ms = ::amazon_remote::IntentDeadline(start_boot_ms);
    fs::FileHandle file{};
    if (R_FAILED(fs::OpenFile(&file, ModePath, fs::OpenMode_Read))) return;
    ON_SCOPE_EXIT { fs::CloseFile(file); };
    s64 size = 0; char mode[9]{}; size_t actual = 0;
    if (R_FAILED(fs::GetFileSize(&size, file)) || size < 7 || size > 9) return;
    if (R_FAILED(fs::ReadFile(&actual, file, 0, mode, static_cast<size_t>(size))) || actual != static_cast<size_t>(size)) return;
    const bool enabled = ::amazon_remote::IsIntentMode(mode, actual);
    g_trace.SetEnabled(enabled);
    g_headers.SetEnabled(enabled);
}
void Disable() { g_trace.SetEnabled(false); g_headers.SetEnabled(false); }
bool Enabled() { return g_trace.Enabled(); }
void Observe(std::uint32_t command, std::uint64_t program, std::uint64_t process,
    const void *payload, std::size_t size) {
    if (!g_trace.Enabled()) return;
    const auto boot_ms = os::GetSystemTick().ToTimeSpan().GetMilliSeconds();
    if (!CheckWindow(static_cast<u64>(boot_ms))) return;
    (void)g_trace.Observe(command, program, process, static_cast<std::uint64_t>(boot_ms), payload, size);
}
bool Pop(::amazon_remote::IntentRecord &out) { return g_trace.Pop(out); }
void SnapshotInto(::amazon_remote::IntentCounters &out) { g_trace.SnapshotInto(out); }
void ObserveManagedHeader(std::uint32_t type, const void *event, std::size_t size, bool configured_address_hint) {
    if (!g_headers.Enabled() || type != 6) return;
    const auto boot_ms = os::GetSystemTick().ToTimeSpan().GetMilliSeconds();
    if (!CheckWindow(static_cast<u64>(boot_ms))) return;
    (void)g_headers.Observe(type, event, size, static_cast<std::uint64_t>(boot_ms), configured_address_hint);
}
bool PopManagedHeader(::amazon_remote::BleHeaderRecord &out) { return g_headers.Pop(out); }
void HeaderSnapshotInto(::amazon_remote::BleHeaderCounters &out) { g_headers.SnapshotInto(out); }
}
