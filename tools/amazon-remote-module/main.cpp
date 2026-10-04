// SPDX-License-Identifier: GPL-2.0-only
// Inactive, bounded experimental sysmodule. Not a production auto-pair daemon.
#include <switch.h>
#include "../../mc_mitm/source/amazon_remote/libnx_session.hpp"
#include "../../mc_mitm/source/amazon_remote/diagnostic_log.hpp"
#include "../../mc_mitm/source/amazon_remote/capture_lease.hpp"
#include "../../mc_mitm/source/amazon_remote/ble_preflight.hpp"
#include "hdls_backend.hpp"
#include "io.hpp"
#include "protocol.hpp"
#include "config.hpp"
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>

extern "C" {
u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;
void __libnx_initheap() {
    alignas(16) static char heap[0x80000];
    extern void *fake_heap_start, *fake_heap_end;
    fake_heap_start = heap; fake_heap_end = heap + sizeof heap;
}
}
namespace {
Result initialization{}; bool sm_open{}, fs_open{}, mounted{};
amazon_remote::RotatingLog log_file;
bool log_healthy{};
static_assert(PscPmState_Awake == 0 && PscPmState_ReadyAwaken == 1 && PscPmState_ReadySleep == 2);
static_assert(PscPmState_ReadySleepCritical == 3 && PscPmState_ReadyAwakenCritical == 4 && PscPmState_ReadyShutdown == 5);
std::uint64_t start_ms{};
std::uint64_t Now() { return armTicksToNs(armGetSystemTick()) / 1000000; }
Result Invalid() { return MAKERESULT(Module_Libnx, LibnxError_BadInput); }
void Log(const char *message) {
    char line[amazon_remote::RotatingLog::LineLimit]{};
    amazon_remote::FormatLogLine(line, sizeof line, start_ms, Now(), message);
    if (!log_file.Write(line)) log_healthy = false;
}
void ResultLog(const char *operation, Result rc) {
    char line[192]{};
    std::snprintf(line, sizeof line, "%s Result=%08lx", operation, static_cast<unsigned long>(rc)); Log(line);
}
bool Enabled() {
    constexpr char expected[] = "enable-experimental-module=1\n";
    char data[sizeof expected-1]{};
    return armodule::ReadExact("sdmc:/config/amazon-remote-module/enable.txt", data) &&
        !std::memcmp(data, expected, sizeof data);
}
bool Target(BtdrvAddress &target) {
    char text[18]{};
    if (!armodule::ReadExact("sdmc:/config/amazon-remote/address.txt", text)) return false;
    std::array<std::uint8_t,6> address{};
    if (!armodule::ParseTarget(text, sizeof text, address)) return false;
    std::memcpy(target.address, address.data(), address.size()); return true;
}
bool Pending() {
    struct stat marker{}; errno = 0;
    return ::stat("sdmc:/config/amazon-remote/link-test-pending.txt", &marker) == 0 || errno != ENOENT;
}
Result Preflight() {
    auto rc = setsysInitialize(); if (R_FAILED(rc)) return rc;
    const auto flag = [&](const char *section, const char *key) {
        u64 copied = 0; u8 value = 0;
        const auto result = setsysGetSettingsItemValue(section, key, &value, sizeof value, &copied);
        return amazon_remote::DecodeFlag(R_SUCCEEDED(result), copied, value);
    };
    amazon_remote::BlePreflight view{};
    view.ble_disabled = flag("hid_debug", "ble_disabled");
    view.config_skip_boot = flag("bluetooth_config", "skip_boot");
    view.debug_skip_boot = flag("bluetooth_debug", "skip_boot");
    setsysExit();
    rc = btmsysInitialize(); if (R_FAILED(rc)) return rc;
    bool on = false; rc = btmsysGetRadioOnOff(&on); btmsysExit();
    if (R_FAILED(rc)) return rc;
    view.radio_on = on ? amazon_remote::FlagState::True : amazon_remote::FlagState::False;
    return view.ExplicitlyBlocked() ? Invalid() : 0; // Unknown debug flags are not reinterpreted as enabled.
}
Result ReadLease(Service &mc, amazon_remote::CaptureLeaseWire &lease) {
    lease = {};
    const auto rc = serviceDispatch(&mc, 65002,
        .buffer_attrs = {SfBufferAttr_HipcPointer | SfBufferAttr_Out},
        .buffers = {{&lease, sizeof lease}});
    if (R_FAILED(rc)) return rc;
    if (lease.magic != amazon_remote::CaptureLeaseMagic || lease.version != amazon_remote::CaptureLeaseVersion || lease.reserved ||
        lease.state > unsigned(amazon_remote::CaptureLeaseState::Unavailable) || !lease.now_boot_ms) return Invalid();
    return 0;
}
bool LiveLease(const amazon_remote::CaptureLeaseWire &lease, std::uint64_t now) {
    return lease.state == unsigned(amazon_remote::CaptureLeaseState::Active) && now >= lease.now_boot_ms &&
        lease.deadline_boot_ms > now && lease.deadline_boot_ms-now <= 60000;
}
}
extern "C" void __appInit() {
    initialization = smInitialize(); sm_open = R_SUCCEEDED(initialization); if (!sm_open) return;
    initialization = setsysInitialize(); if (R_FAILED(initialization)) return;
    SetSysFirmwareVersion version{}; initialization = setsysGetFirmwareVersion(&version); setsysExit();
    if (R_FAILED(initialization)) return;
    hosversionSet(MAKEHOSVERSION(version.major, version.minor, version.micro));
    initialization = fsInitialize(); fs_open = R_SUCCEEDED(initialization); if (!fs_open) return;
    initialization = fsdevMountSdmc(); mounted = R_SUCCEEDED(initialization);
}
extern "C" void __appExit() {
    log_file.Close();
    if (mounted) fsdevUnmountAll();
    if (fs_open) fsExit();
    if (sm_open) smExit();
}
int main(int, char **) {
    if (R_FAILED(initialization) || !Enabled()) return 0; // Never abort boot for this optional module.
    start_ms = Now(); const auto instance = armGetSystemTick();
    log_healthy = log_file.Open("sdmc:/config/amazon-remote-module/module.log");
    if (!log_healthy) return 0;
    Log("Amazon Remote Module 0.2.0-candidate: explicit operator arm/link; 90sec input bound; no reconnect/wake/voice");
    // Current public BTM-layout workaround has device evidence only on 22.5.0.
    if (hosversionGet() != MAKEHOSVERSION(22,5,0)) { Log("Unsupported firmware: no Bluetooth or controller setup"); return 0; }
    auto rc = pscmInitialize(); ResultLog("pscmInitialize", rc); if (R_FAILED(rc)) return 0;
    PscPmModule pm{}; const u32 dependencies[] = {PscPmModuleId_Fs, PscPmModuleId_Hid, PscPmModuleId_Btm};
    rc = pscmGetPmModule(&pm, static_cast<PscPmModuleId>(0xbe), dependencies, 3, true);
    ResultLog("psc register custom id0xbe (device validation pending)", rc);
    if (R_FAILED(rc)) { pscmExit(); return 0; }
    amazon_remote::LibnxSession remote(Log, true, true);
    armodule::HdlsBackend backend;
    armodule::OwnedController controller(backend);
    armodule::SessionPolicy policy;
    armodule::InputHoldGuard hold_guard;
    Service mc{}; bool capture_owned = false, awake = true, shutdown = false;
    BtdrvAddress target{}; amazon_remote::CaptureLeaseWire lease{};
    armodule::Status status{}; status.instance = instance;
    std::uint64_t last_status = 0, last_clock = start_ms;
    const auto cleanup = [&] {
        ResultLog("neutralize/detach owned pad", controller.Stop());
        remote.Close();
        if (capture_owned && serviceIsActive(&mc)) ResultLog("end own capture", serviceDispatch(&mc, 65003));
        capture_owned = false; serviceClose(&mc); lease = {};
        status.link_ready = status.buttons = status.output_attached = 0;
    };
    const auto stop = [&](Result result) { status.result = result; cleanup(); policy.Stop(); };
    const auto sighting = [&](amazon_remote::SightingWire &wire) {
        if (policy.Get() != armodule::State::Scanning || !serviceIsActive(&mc)) return false;
        auto result = ReadLease(mc, lease);
        if (R_FAILED(result) || !LiveLease(lease, Now())) return false;
        result = serviceDispatch(&mc, 65000,
            .buffer_attrs = {SfBufferAttr_HipcPointer | SfBufferAttr_Out}, .buffers = {{&wire, sizeof wire}});
        amazon_remote::SightingAddress address{}; std::memcpy(address.data(), target.address, address.size());
        return R_SUCCEEDED(result) && LiveLease(lease, Now()) &&
            amazon_remote::AssessSighting(wire, address, remote.ScanStarted(), Now()) == amazon_remote::SightingAssessment::FreshCandidate;
    };
    while (!shutdown) {
        // Power event is checked before SD/BT/output. No applet API in sysmodule.
        rc = eventWait(&pm.event, 0);
        if (R_SUCCEEDED(rc)) {
            PscPmState state{}; u32 flags{}; rc = pscPmModuleGetRequest(&pm, &state, &flags);
            if (R_FAILED(rc)) { stop(rc); break; }
            const auto decision = armodule::ClassifyPower(unsigned(state));
            if (!decision.valid) { if (awake) stop(Invalid()); shutdown = true; }
            else if (decision.suspend) {
                if (awake) { cleanup(); Log("Suspend/shutdown: neutralize/detach attempted; no automatic reconnect"); log_file.Close(); }
                policy.Suspend(); awake = false; shutdown = decision.shutdown;
            } else if (decision.resume && !awake) {
                awake = true; policy.Resume();
                log_healthy = log_file.Open("sdmc:/config/amazon-remote-module/module.log");
            }
            rc = pscPmModuleAcknowledge(&pm, state);
            if (R_FAILED(rc)) { if (awake) stop(rc); break; }
        } else if (rc != MAKERESULT(Module_Kernel, KernelError_TimedOut)) { if (awake) stop(rc); break; }
        if (shutdown) break;
        if (!awake) { svcSleepThread(100000000); continue; }
        const auto now = Now();
        if (now < last_clock) { stop(Invalid()); break; } last_clock = now;
        armodule::Command command{};
        if (armodule::ReadExact(armodule::CommandPath, command) && armodule::ValidCommand(command, instance, status.last_sequence, now)) {
            status.last_sequence = command.sequence;
            if (std::remove(armodule::CommandPath) != 0) { stop(Invalid()); }
            else if (command.kind == unsigned(armodule::CommandKind::Stop)) stop(0);
            else if (command.kind == unsigned(armodule::CommandKind::Arm) && policy.Get() == armodule::State::Idle) {
                if (!policy.Arm() || !log_healthy || Pending() || !Target(target)) rc = Invalid();
                else rc = Preflight();
                if (R_SUCCEEDED(rc)) rc = smGetService(&mc, "mc");
                if (R_SUCCEEDED(rc)) rc = ReadLease(mc, lease);
                if (R_SUCCEEDED(rc) && lease.state != unsigned(amazon_remote::CaptureLeaseState::Available)) rc = Invalid();
                if (R_SUCCEEDED(rc)) { rc = serviceDispatch(&mc, 65001); capture_owned = R_SUCCEEDED(rc); }
                if (R_SUCCEEDED(rc)) rc = ReadLease(mc, lease);
                if (R_SUCCEEDED(rc) && !LiveLease(lease, Now())) rc = Invalid();
                if (R_SUCCEEDED(rc)) rc = remote.InitializeIdentityTest(target);
                ResultLog("ARUID0 user-path initialization (not yet device-proven)", rc);
                if (R_SUCCEEDED(rc) && !log_healthy) rc = Invalid();
                if (R_SUCCEEDED(rc)) rc = remote.StartLinkOnlyScan(Now());
                if (R_FAILED(rc)) stop(rc);
            } else if (command.kind == unsigned(armodule::CommandKind::Link)) {
                amazon_remote::SightingWire wire{};
                if (sighting(wire) && policy.Link(true)) {
                    rc = remote.ConfirmObservedLink(wire, Now(), lease.deadline_boot_ms);
                    if (R_FAILED(rc) || !remote.LinkAttempted()) stop(R_FAILED(rc) ? rc : Invalid());
                } // Not-ready request is discarded, never queued.
            }
        }
        if (policy.Active()) {
            rc = remote.Poll(Now());
            if (R_FAILED(rc) || !log_healthy) stop(R_FAILED(rc) ? rc : Invalid());
            else if (remote.Ready()) {
                if (policy.Get() == armodule::State::Linking && policy.Ready()) {
                    rc = controller.Start(); ResultLog("attach owned HDLS pad (device validation pending)", rc);
                    if (R_FAILED(rc)) stop(rc);
                }
                if (policy.Get() == armodule::State::Ready) {
                    if (!hold_guard.Accept(remote.Buttons(), Now())) { Log("Continuous held-input/clock watchdog: stop and neutralize"); stop(Invalid()); }
                    else { rc = controller.Update(remote.Buttons()); if (R_FAILED(rc)) stop(rc); }
                }
            }
            if (policy.Get() == armodule::State::Scanning) {
                rc = ReadLease(mc, lease); if (R_FAILED(rc) || !LiveLease(lease, Now())) stop(R_FAILED(rc) ? rc : Invalid());
            }
        }
        if (!last_status || Now()-last_status >= 500) {
            amazon_remote::SightingWire wire{};
            status.link_ready = sighting(wire);
            status.state = unsigned(policy.Get()); status.now_ms = Now(); status.capture_deadline = lease.deadline_boot_ms;
            status.buttons = policy.Get() == armodule::State::Ready ? remote.Buttons() : 0;
            status.output_attached = controller.Attached();
            if (!armodule::Publish("sdmc:/config/amazon-remote-module/status.tmp", armodule::StatusPath, status)) { stop(Invalid()); break; }
            last_status = status.now_ms;
        }
        svcSleepThread(policy.Active() ? 20000000 : 100000000);
    }
    if (awake) cleanup();
    eventClose(&pm.event); pscPmModuleFinalize(&pm); pscPmModuleClose(&pm); pscmExit();
    return 0;
}
