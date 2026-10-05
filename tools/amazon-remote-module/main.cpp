// SPDX-License-Identifier: GPL-2.0-only
// Inactive, bounded experimental sysmodule. Not a production auto-pair daemon.
#include <switch.h>
#ifdef ARMODULE_NAV14
#include "connection_capture.hpp"
#endif
#include "../../mc_mitm/source/amazon_remote/libnx_session.hpp"
#include "../../mc_mitm/source/amazon_remote/diagnostic_log.hpp"
#include "../../mc_mitm/source/amazon_remote/capture_lease.hpp"
#include "../../mc_mitm/source/amazon_remote/ble_preflight.hpp"
#include "hdls_backend.hpp"
#include "io.hpp"
#include "protocol.hpp"
#include "config.hpp"
#include "startup_storage.hpp"
#ifdef ARMODULE_NAV15
#include "enrollment_storage.hpp"
#endif
#if defined(ARMODULE_NAV10) || defined(ARMODULE_NAV11) || defined(ARMODULE_NAV12)
#include "status_storage.hpp"
#endif
#ifdef ARMODULE_NAV12
#include "idle_health.hpp"
#endif
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>

extern "C" {
u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;
void __libnx_initheap() {
    extern void *fake_heap_start, *fake_heap_end;
    alignas(4096) static char arena[0x80000];
    fake_heap_start=arena; fake_heap_end=arena+sizeof arena;
}
}
namespace {
Result initialization{}; bool sm_open{}, fs_open{}, mounted{};
constinit armodule::StartupRecord startup_record{};
FsFileSystem startup_fs{}; bool startup_fs_open{};
amazon_remote::RotatingLog log_file;
bool log_healthy{};
#ifdef ARMODULE_NAV13
bool log_stopped{};
#endif
static_assert(PscPmState_Awake == 0 && PscPmState_ReadyAwaken == 1 && PscPmState_ReadySleep == 2);
static_assert(PscPmState_ReadySleepCritical == 3 && PscPmState_ReadyAwakenCritical == 4 && PscPmState_ReadyShutdown == 5);
std::uint64_t start_ms{};
std::uint64_t Now() { return armTicksToNs(armGetSystemTick()) / 1000000; }
Result Invalid() { return MAKERESULT(Module_Libnx, LibnxError_BadInput); }
void Checkpoint(armodule::StartupStage stage, Result result) {
    startup_record.stage = unsigned(stage); startup_record.result = result;
    startup_record.now_ms = Now(); startup_record.checksum = armodule::StartupChecksum(startup_record);
    // Earlier SM/FS failures cannot be persisted without an SD service. The
    // companion's read-only PM query covers process presence, not that error.
    if (startup_fs_open) (void)armodule::WriteStartup(startup_fs, startup_record);
}
void Log(const char *message) {
#ifdef ARMODULE_NAV13
    if (log_stopped) return;
#endif
    char line[amazon_remote::RotatingLog::LineLimit]{};
    amazon_remote::FormatLogLine(line, sizeof line, start_ms, Now(), message);
    if (!log_file.Write(line)) log_healthy = false;
}
void ResultLog(const char *operation, Result rc) {
    char line[192]{};
    std::snprintf(line, sizeof line, "%s Result=%08lx", operation, static_cast<unsigned long>(rc)); Log(line);
}
#if defined(ARMODULE_NAV15) && !defined(ARMODULE_NAV16)
bool EnrollmentIntent(const amazon_remote::EnrollmentRecord &record) {
    return log_healthy && record.instance==startup_record.instance &&
        armodule::PersistEnrollmentIntent(armodule::EnrollmentIntentPath,record);
}
#endif
bool Enabled() {
    constexpr char expected[] = "enable-experimental-module=1\n";
    char data[sizeof expected-1]{};
    return armodule::ReadExact(armodule::EnablePath, data) &&
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
    startup_record.instance = armGetSystemTick();
    initialization = smInitialize(); sm_open = R_SUCCEEDED(initialization);
    Checkpoint(armodule::StartupStage::Sm, initialization); if (!sm_open) return;
    // Establish raw SD diagnostics before settings/devoptab initialization.
    initialization = fsInitialize(); fs_open = R_SUCCEEDED(initialization);
    Checkpoint(armodule::StartupStage::Fs, initialization); if (!fs_open) return;
    initialization = fsOpenSdCardFileSystem(&startup_fs); startup_fs_open = R_SUCCEEDED(initialization);
    Checkpoint(armodule::StartupStage::Sd, initialization); if (!startup_fs_open) return;
    initialization = fsdevMountSdmc(); mounted = R_SUCCEEDED(initialization);
    Checkpoint(armodule::StartupStage::Mount, initialization); if (!mounted) return;
    initialization = setsysInitialize();
    Checkpoint(armodule::StartupStage::Settings, initialization); if (R_FAILED(initialization)) return;
    SetSysFirmwareVersion version{}; initialization = setsysGetFirmwareVersion(&version); setsysExit();
    Checkpoint(armodule::StartupStage::Firmware, initialization);
    if (R_FAILED(initialization)) return;
    hosversionSet(MAKEHOSVERSION(version.major, version.minor, version.micro));
}
extern "C" void __appExit() {
    log_file.Close();
    if (startup_fs_open) fsFsClose(&startup_fs);
    if (mounted) fsdevUnmountAll();
    if (fs_open) fsExit();
    if (sm_open) smExit();
}
int main(int, char **) {
    // Preserve the failing early checkpoint on init failure instead of hiding it.
    if (R_FAILED(initialization)) return 0;
    Checkpoint(armodule::StartupStage::Main, 0);
    const bool enabled = Enabled(); Checkpoint(armodule::StartupStage::Enable, enabled ? 0 : Invalid());
    if (!enabled) return 0; // Never abort boot for this optional module.
    start_ms = Now(); const auto instance = startup_record.instance;
    log_healthy = log_file.Open(armodule::LogPath);
    Checkpoint(armodule::StartupStage::Log, log_healthy ? 0 : Invalid());
    if (!log_healthy) return 0;
    Log(armodule::StartupDiagnosticOnly ?
        "Amazon Remote startup-diag1: Bluetooth commands disabled; neutral Y check only" :
        "Amazon Remote navigation: IDLE until operator A/ZL; bounded session, no boot scan/reconnect/wake");
    // Current public BTM-layout workaround has device evidence only on 22.5.0.
    Checkpoint(armodule::StartupStage::FirmwareGuard, hosversionGet() == MAKEHOSVERSION(22,5,0) ? 0 : Invalid());
    if (hosversionGet() != MAKEHOSVERSION(22,5,0)) { Log("Unsupported firmware: no Bluetooth or controller setup"); return 0; }
    auto rc = pscmInitialize(); Checkpoint(armodule::StartupStage::Psc, rc);
    ResultLog("pscmInitialize", rc); if (R_FAILED(rc)) return 0;
    PscPmModule pm{}; const u32 dependencies[] = {PscPmModuleId_Fs, PscPmModuleId_Hid, PscPmModuleId_Btm};
    rc = pscmGetPmModule(&pm, static_cast<PscPmModuleId>(0xbe), dependencies, 3, true);
    Checkpoint(armodule::StartupStage::PscRegister, rc);
    ResultLog("psc register custom id0xbe (device validation pending)", rc);
    if (R_FAILED(rc)) { pscmExit(); return 0; }
#if defined(ARMODULE_NAV10) || defined(ARMODULE_NAV11) || defined(ARMODULE_NAV12)
#ifdef ARMODULE_NAV12
#ifdef ARMODULE_NAV15_SYSTEM
    amazon_remote::LibnxSession remote(Log,true,true,true,true,true,true);
#elif defined(ARMODULE_NAV15)
    amazon_remote::LibnxSession remote(Log,true,true,true,true,true);
#else
    amazon_remote::LibnxSession remote(Log,true,true,true,true);
#endif
    armodule::IdleHealth idle_health;armodule::StatusWriteSchedule status_schedule;bool idle_pass_logged{},status_checkpointed{};
#else
    amazon_remote::LibnxSession remote(Log,true,true,true);
#endif
    armodule::StatusWriter status_writer;std::uint64_t status_error_since{};
#else
    amazon_remote::LibnxSession remote(Log, true, true);
#endif
    armodule::HdlsBackend backend;
#ifdef ARMODULE_NAV13
#ifdef ARMODULE_NAV15
    backend.SetLogger(Log,true);
#ifdef ARMODULE_NAV17
    Log("NAV17: same navigation; Y getter-only full UUID census plus cached client ARUID; NO PairDevice command, no ownership changes");
#elif defined(ARMODULE_NAV16)
    Log("NAV16: same navigation; explicit Y getter-only context once at neutral READY; existing clients borrowed; NO PairDevice command");
#else
    Log("NAV15: setup90s, Ready30min; explicit Y enrollment once after verified neutral READY; AD-derived profile is experimental; no automatic pair/reconnect");
#ifdef ARMODULE_NAV15_SYSTEM
    Log("ENROLLMENT_ROUTE=system BTM cmd41 + paired-address cmd44; fixed route, no user-to-system fallback; no new privileges");
#endif
#endif
#else
    backend.SetLogger(Log);
#endif
#endif
    armodule::OwnedController controller(backend);
    armodule::SessionPolicy policy;
    armodule::InputHoldGuard hold_guard;
    Service mc{}; bool capture_owned = false, awake = true, shutdown = false;
    BtdrvAddress target{}; amazon_remote::CaptureLeaseWire lease{};
#ifdef ARMODULE_NAV14
    armodule::ConnectionCapture connection_capture;
    std::uint64_t connection_capture_ms{};
    bool connection_capture_checked{};
    const auto capture_connections = [&](bool force) {
        if (!capture_owned || !serviceIsActive(&mc) || !connection_capture_checked) return;
        const auto now=Now();
        if (!force && connection_capture_ms && now-connection_capture_ms<500) return;
        connection_capture_ms=now;
        amazon_remote::ConnectionEvidenceWire wire{};
        const auto result=serviceDispatch(&mc,65004,
            .buffer_attrs={SfBufferAttr_HipcPointer | SfBufferAttr_Out},.buffers={{&wire,sizeof wire}});
        if (R_FAILED(result) || !connection_capture.Accept(wire,Now(),Log)) {
            ResultLog("connection evidence access gap (not a connection result)",R_FAILED(result)?result:Invalid());
            connection_capture_checked=false; // No error retry/spam; connection behavior unchanged.
        } else if (force) {
            char line[192]{};
            std::snprintf(line,sizeof line,"TARGET_CONNECTION_EVIDENCE_FINAL captured=%lu overflow=%lu; explicit60slease_only=1 missing_callback_not_radio_absence_proof=1",
                static_cast<unsigned long>(connection_capture.Count()),static_cast<unsigned long>(connection_capture.Overflow()));
            Log(line);
        }
    };
#endif
    armodule::Status status{}; status.instance = instance;
#ifdef ARMODULE_NAV15
    amazon_remote::EnrollmentRecord last_enrollment{};
    bool enrollment_saved{}, enrollment_write_failed{};
    const auto publish_enrollment = [&] {
        const auto &record=remote.Enrollment();
        if(record.state==unsigned(amazon_remote::EnrollmentState::None))return true;
        if(enrollment_write_failed)return false;
        if(enrollment_saved && !std::memcmp(&record,&last_enrollment,sizeof record))return true;
        if(!amazon_remote::ValidEnrollment(record) ||
           !armodule::Publish(armodule::EnrollmentTemp,armodule::EnrollmentPath,record)) {
            enrollment_write_failed=true;Log("ENROLLMENT_RECORD_FAILED: stop; intent preserved; no retry");return false;
        }
        last_enrollment=record;enrollment_saved=true;return true;
    };
#endif
    std::uint64_t last_clock = start_ms;
#ifndef ARMODULE_NAV12
    std::uint64_t last_status = 0;
#endif
    const auto cleanup = [&] {
#ifdef ARMODULE_NAV14
        capture_connections(true); // Final snapshot before ending our lease/closing MC.
#endif
        const auto pad_result = controller.Stop();
        const auto final_trace=backend.Trace(instance,Now());
        if (final_trace.open_result!=armodule::HdlsNotAttempted)
            (void)armodule::Publish(armodule::HdlsTemp,armodule::HdlsPath,final_trace);
        status.pad_cleanup_result = controller.CleanupError();
        if (!status.result && pad_result) status.result = pad_result;
        ResultLog("neutralize/detach owned pad", pad_result);
        remote.Close();
#ifdef ARMODULE_NAV15
        if(!publish_enrollment() && !status.result)status.result=Invalid();
#endif
        if (capture_owned && serviceIsActive(&mc)) ResultLog("end own capture", serviceDispatch(&mc, 65003));
        capture_owned = false; serviceClose(&mc); lease = {};
        status.link_ready = status.buttons = status.output_attached = 0;
        return pad_result;
    };
    const auto stop = [&](Result result) {
        const auto pad_result = cleanup();
        // A later successful X/Stop must not erase a recorded failure.
        if (result || pad_result) status.result = result ? result : pad_result;
        policy.Stop();
#ifdef ARMODULE_NAV13
        // A stopped one-shot session will never reconnect. Release the text-log
        // handle AFTER all failure/cleanup events so FTP can capture this boot.
        log_file.Close();log_stopped=true;
#endif
    };
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
                log_healthy = log_file.Open(armodule::LogPath);
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
            else if (armodule::StartupDiagnosticOnly &&
                (command.kind == unsigned(armodule::CommandKind::Arm) || command.kind == unsigned(armodule::CommandKind::Link))) {
                Log("Diagnostic build: ARM/LINK ignored; no Bluetooth initialization");
            }
            else if (command.kind == unsigned(armodule::CommandKind::Stop)) stop(0);
#ifdef ARMODULE_NAV15
            else if(command.kind==unsigned(armodule::CommandKind::Enroll)) {
                // Live session repeats every identity/handle/fingerprint guard;
                // a stale UI record can never authorize pairing by itself.
                if(policy.Get()==armodule::State::Ready && controller.Attached() &&
                   !remote.Buttons() && log_healthy) {
#ifdef ARMODULE_NAV16
                    amazon_remote::EnrollmentContextRecord record{};
                    const auto context_rc=remote.ReadEnrollmentContext(instance,record);
                    const bool saved=!context_rc && log_healthy && armodule::PersistEnrollmentContext(armodule::EnrollmentContextPath,record);
                    ResultLog("operator getter-only SAME_SESSION context; no pair request",context_rc);
                    Log(saved?"ENROLLMENT_CONTEXT_SAVED: exclusive synced readback verified; no pair request":"ENROLLMENT_CONTEXT_NOT_SAVED: partials preserved; no retry or pair request");
                    if(!saved || !log_healthy)stop(context_rc?context_rc:Invalid());
#else
                    const auto enrollment_rc=remote.EnrollOnce(instance,EnrollmentIntent);
                    ResultLog("operator explicit BLE enrollment; Result0 is request acceptance only",enrollment_rc);
#endif
                } else Log("ENROLLMENT_REFUSED: live READY/owned pad/neutral input required; no request queued");
            }
#endif
            else if (command.kind == unsigned(armodule::CommandKind::CheckController) && policy.CheckController()) {
                // No remote.Initialize, MC capture, scan, link or target-file access.
                rc = log_healthy ? armodule::NeutralControllerCheck(controller) : Invalid();
                const auto hdls_trace=backend.Trace(instance,Now());
                (void)armodule::Publish(armodule::HdlsTemp,armodule::HdlsPath,hdls_trace);
                status.controller_check = unsigned(R_SUCCEEDED(rc) ? armodule::ControllerCheck::Passed : armodule::ControllerCheck::Failed);
                status.pad_cleanup_result = controller.CleanupError();
                ResultLog("operator neutral HDLS attach/detach; replies only, not navigation proof", rc);
                if (R_FAILED(rc) || !log_healthy) stop(R_FAILED(rc) ? rc : Invalid());
            }
#ifdef ARMODULE_NAV12
            else if (command.kind==unsigned(armodule::CommandKind::Arm) && !idle_health.Ready(Now())) {
                Log("ARM refused:15s stable IDLE heartbeat required; no Bluetooth initialized");
            }
#endif
            else if (command.kind == unsigned(armodule::CommandKind::Arm) && policy.Get() == armodule::State::Idle) {
                if (!policy.Arm() || !log_healthy || Pending() || !Target(target)) rc = Invalid();
                else rc = Preflight();
                if (R_SUCCEEDED(rc)) rc = smGetService(&mc, "mc");
#ifdef ARMODULE_NAV14
                if (R_SUCCEEDED(rc)) {
                    amazon_remote::ConnectionEvidenceWire wire{};
                    rc=serviceDispatch(&mc,65004,
                        .buffer_attrs={SfBufferAttr_HipcPointer | SfBufferAttr_Out},.buffers={{&wire,sizeof wire}});
                    if (R_SUCCEEDED(rc) && (!connection_capture.Accept(wire,Now(),Log) || wire.target_count || wire.overflow)) rc=Invalid();
                    connection_capture_checked=R_SUCCEEDED(rc);
                    ResultLog("observer connection evidence preflight; failure prevents Bluetooth initialization",rc);
                }
#endif
                if (R_SUCCEEDED(rc)) rc = ReadLease(mc, lease);
                if (R_SUCCEEDED(rc) && lease.state != unsigned(amazon_remote::CaptureLeaseState::Available)) rc = Invalid();
                if (R_SUCCEEDED(rc)) { rc = serviceDispatch(&mc, 65001); capture_owned = R_SUCCEEDED(rc); }
                if (R_SUCCEEDED(rc)) rc = ReadLease(mc, lease);
                if (R_SUCCEEDED(rc) && !LiveLease(lease, Now())) rc = Invalid();
                if (R_SUCCEEDED(rc)) rc = remote.InitializeIdentityTest(target);
                ResultLog("explicit bounded Bluetooth session initialization", rc);
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
#ifdef ARMODULE_NAV14
            capture_connections(false);
#endif
            if (R_FAILED(rc) || !log_healthy) stop(R_FAILED(rc) ? rc : Invalid());
            else if (remote.Ready()) {
                if (policy.Get() == armodule::State::Linking && policy.Ready()) {
                    rc = controller.Start(); ResultLog("attach owned HDLS pad (visible navigation validation pending)", rc);
                    const auto hdls_trace=backend.Trace(instance,Now());
                    if (!armodule::Publish(armodule::HdlsTemp,armodule::HdlsPath,hdls_trace) && !rc) rc=Invalid();
                    if (R_FAILED(rc)) stop(rc);
                }
                if (policy.Get() == armodule::State::Ready) {
                    if (!hold_guard.Accept(remote.Buttons(), Now())) { Log("Continuous held-input/clock watchdog: stop and neutralize"); stop(Invalid()); }
                    else {
                        const auto before = controller.SuccessfulWrites();
#ifdef ARMODULE_NAV13
                        const bool recovered=controller.Recovered();
                        rc = controller.UpdateMaintained(remote.Buttons(),Now());
                        if (!recovered && controller.Recovered())
                            Log("HDLS_RECOVERY: one confirmed-absent owned handle replaced; neutral first; mapped button release required; no Bluetooth reconnect");
                        if (R_FAILED(rc)) {
                            char line[192]{};
                            std::snprintf(line,sizeof line,"HDLS_DELIVERY_FAILED Result=%08lx remote=%08lx mapped=%016llx waiting_release=%u; no ambiguous-error reattach",
                                static_cast<unsigned long>(rc),static_cast<unsigned long>(remote.Buttons()),
                                static_cast<unsigned long long>(armodule::MapButtons(remote.Buttons())),controller.WaitingRelease());Log(line);
                        }
#else
                        rc = controller.Update(remote.Buttons());
#endif
                        if (R_FAILED(rc)) stop(rc);
                        else if (controller.SuccessfulWrites() != before) {
                            char line[160]{};
                            std::snprintf(line, sizeof line, "HDLS ACK remote=%08lx mapped=%016llx writes=%llu; visible navigation unverified",
                                static_cast<unsigned long>(remote.Buttons()),
                                static_cast<unsigned long long>(controller.LastAcknowledgedButtons()),
                                static_cast<unsigned long long>(controller.SuccessfulWrites()));
                            Log(line);
                            if (!log_healthy) stop(Invalid());
                        }
                    }
                }
            }
            if (policy.Get() == armodule::State::Scanning) {
                rc = ReadLease(mc, lease); if (R_FAILED(rc) || !LiveLease(lease, Now())) stop(R_FAILED(rc) ? rc : Invalid());
            }
        }
#ifdef ARMODULE_NAV15
        if(!publish_enrollment()){stop(Invalid());break;}
#endif
#ifdef ARMODULE_NAV12
        if (status_schedule.Due(Now())) {
#else
        if (!last_status || Now()-last_status >= 500) {
#endif
            amazon_remote::SightingWire wire{};
            status.link_ready = sighting(wire);
            status.state = unsigned(policy.Get()); status.now_ms = Now(); status.capture_deadline = lease.deadline_boot_ms;
            status.buttons = policy.Get() == armodule::State::Ready ? remote.Buttons() : 0;
            status.output_attached = controller.Attached();
            status.acknowledged_buttons = controller.LastAcknowledgedButtons();
            status.successful_writes = controller.SuccessfulWrites();
            status.pad_cleanup_result = controller.CleanupError();
#if defined(ARMODULE_NAV10) || defined(ARMODULE_NAV11) || defined(ARMODULE_NAV12)
            status.reserved=armodule::StatusChecksum(status);
            const auto status_rc=status_writer.WriteRecord(startup_fs,status);
            const bool published=R_SUCCEEDED(status_rc);
            if (!published) {
                if (!status_error_since) status_error_since=Now();
                char detail[160]{};std::snprintf(detail,sizeof detail,"Status write step=%u Result=%08lx permanent=%u",status_writer.step,
                    static_cast<unsigned long>(status_rc),status_writer.permanent);Log(detail);
            } else status_error_since=0;
#else
            const bool published = armodule::Publish(armodule::StatusTemp, armodule::StatusPath, status);
#endif
            // Checkpoint once on first status or failure, not every500ms forever.
#ifdef ARMODULE_NAV12
            if (published && (!status_checkpointed || startup_record.result)) {
                Checkpoint(armodule::StartupStage::Status,0);status_checkpointed=true;
            }
            if (published) idle_health.Observe(status,Now());
            else if (status_rc!=0xe02 || status_writer.permanent) idle_health.Reset();
            if (idle_health.Ready(Now()) && !idle_pass_logged) {
                Log("IDLE_HEALTH_PASSED:15s distinct fresh status publications; NO Bluetooth activity yet");idle_pass_logged=true;
            }
            if (status_schedule.Complete(status_rc,status_writer.permanent,Now())) {
                Checkpoint(armodule::StartupStage::Status,status_rc);stop(status_rc);break;
            }
#else
            if (!last_status || !published || startup_record.result) Checkpoint(armodule::StartupStage::Status, published ? 0 : Invalid());
#if defined(ARMODULE_NAV10) || defined(ARMODULE_NAV11) || defined(ARMODULE_NAV12)
            if (!published && armodule::StatusFailureExpired(status_error_since,Now(),status_writer.permanent)) { stop(status_rc);break; }
#else
            if (!published) { stop(Invalid()); break; }
#endif
#endif
#ifndef ARMODULE_NAV12
            last_status = status.now_ms;
#endif
        }
#ifdef ARMODULE_NAV12
        svcSleepThread((policy.Active() || status_schedule.Retrying()) ? 20000000 : 100000000);
#else
        svcSleepThread(policy.Active() ? 20000000 : 100000000);
#endif
    }
    if (awake) cleanup();
    eventClose(&pm.event); pscPmModuleFinalize(&pm); pscPmModuleClose(&pm); pscmExit();
    return 0;
}
