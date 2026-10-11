// SPDX-License-Identifier: GPL-2.0-only
// Writes bounded operator commands. M2 also queries fixed diagnostic metadata.
// No direct connection, pairing, key access or HDLS operations here.
#include <switch.h>
#ifdef ARMODULE_CENTRAL_BOND_B1
#include "../../../mc_mitm/source/amazon_remote/central_bond_wire.hpp"
#if defined(ARMODULE_SAVED_DISCOVERY_B3) || defined(ARMODULE_NATIVE_PEER_C1)
#include "saved_discovery_view.hpp"
#endif
#endif
#ifdef ARMODULE_SECURITY_CAPTURE
#include "security_capture_sdk.hpp"
#endif
#ifdef ARMODULE_CONTROLLER_SLOTS
#include "operator_input.hpp"
#endif
#include "../protocol.hpp"
#ifdef ARMODULE_STARTUP_TRACE
#include "../arm_startup_diagnostic.hpp"
#endif
#include "../io.hpp"
#include "../startup.hpp"
#ifdef ARMODULE_CAPTURE_PREFLIGHT
#include "capture_preflight.hpp"
#include "../../../mc_mitm/source/amazon_remote/power_lifecycle.hpp"
#endif
#ifdef ARMODULE_NAV15
#include "../../../mc_mitm/source/amazon_remote/enrollment.hpp"
#ifdef ARMODULE_NAV16
#include "../../../mc_mitm/source/amazon_remote/enrollment_context.hpp"
#endif
#endif
#if defined(ARMODULE_NAV9) || defined(ARMODULE_NAV10) || defined(ARMODULE_NAV11) || defined(ARMODULE_NAV12)
#include "../navigation_sdk.hpp"
#endif
#ifdef ARMODULE_NAV12
#include "../idle_health.hpp"
#include "../status_reader.hpp"
#endif
#include <cstdio>
#include <cstring>
#include <limits>
#ifdef ARMODULE_NAV17
#include "setup_view.hpp"
#include "../../amazon-remote-pairing-audit/native_bond_guard.hpp"
#endif
namespace {
std::uint64_t Now() { return armTicksToNs(armGetSystemTick()) / 1000000; }
const char *Name(std::uint32_t state) {
    constexpr const char *names[] = {"IDLE", "SCANNING", "LINKING", "READY", "STOPPED", "SUSPENDED", "DISCONNECTING", "DISCONNECTED"};
    return state < 8 ? names[state] : "INVALID";
}
}
int main(int, char **) {
    consoleInit(nullptr);
#ifdef ARMODULE_CONTROLLER_SLOTS
    static_assert(arsetupinput::JoyConStyles == (HidNpadStyleTag_NpadHandheld |
        HidNpadStyleTag_NpadJoyDual | HidNpadStyleTag_NpadJoyLeft | HidNpadStyleTag_NpadJoyRight));
    padConfigureInput(arsetupinput::SupportedPlayers, HidNpadStyleSet_NpadStandard);
    static_assert(HidNpadIdType_No1 == 0 && HidNpadIdType_No8 == 7);
    std::array<PadState, arsetupinput::InputSources> pads{};
    for (unsigned i = 0; i < arsetupinput::SupportedPlayers; ++i)
        padInitializeWithMask(&pads[i], 1ull << i);
    padInitialize(&pads[arsetupinput::SupportedPlayers], HidNpadIdType_Handheld);
    arsetupinput::OperatorInput operator_input;
#else
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
#endif
    std::uint64_t last_send = 0, last_draw = 0;
    std::uint64_t last_query = 0;
#ifdef ARMODULE_CENTRAL_BOND_B1
    amazon_remote::central_bond::Status bond_status{};
    Result bond_query=MAKERESULT(Module_Libnx,LibnxError_NotInitialized);
#endif
#ifdef ARMODULE_SECURITY_CAPTURE
    arsecuritycapture::Backend security_backend;
    arsecuritycapture::Capture security_capture(arsecuritycapture::Absent());
#endif
#ifdef ARMODULE_CAPTURE_PREFLIGHT
    amazon_remote::CaptureDiagnosticsWire capture_wire{};
    Result capture_result = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    armodule::CapturePreflightReport capture_saved{};
    unsigned capture_writes{}; bool capture_report_saved{};
    amazon_remote::PowerHistoryWire power_saved{};
    unsigned power_writes{};
    const auto refresh_capture = [&](std::uint64_t instance) {
        capture_wire = {}; Service observer{};
        capture_result = smGetService(&observer, "mc");
        if (R_SUCCEEDED(capture_result)) {
            capture_result = serviceDispatch(&observer, 65006,
                .buffer_attrs = {SfBufferAttr_HipcPointer | SfBufferAttr_Out},
                .buffers = {{&capture_wire, sizeof capture_wire}});
            amazon_remote::PowerHistoryWire power{};
            const auto power_result=serviceDispatch(&observer,65006,
                .buffer_attrs={SfBufferAttr_HipcPointer | SfBufferAttr_Out},
                .buffers={{&power,sizeof power}});
            if(!power_result && amazon_remote::ValidPowerHistory(power,Now()) && power_writes<32 &&
               (!power_writes || power.sequence!=power_saved.sequence)) {
                ++power_writes;
                if(armodule::Publish(ARMODULE_NAV15_CONFIG "/power-state.tmp",
                    ARMODULE_NAV15_CONFIG "/power-state.bin",power))power_saved=power;
            }
            serviceClose(&observer);
        }
        const auto now = Now();
        if (!capture_result && !amazon_remote::ValidCaptureDiagnostics(capture_wire, now))
            capture_result = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        const bool changed = !capture_writes || capture_saved.instance != instance ||
            capture_saved.result != capture_result || capture_saved.wire.state != capture_wire.state ||
            capture_saved.wire.flags != capture_wire.flags ||
            capture_saved.wire.deadline_boot_ms != capture_wire.deadline_boot_ms ||
            capture_saved.wire.first_close_reason != capture_wire.first_close_reason ||
            capture_saved.wire.first_close_ms != capture_wire.first_close_ms;
        if (changed) {
            capture_report_saved = false;
            if (capture_writes < 20) {
                ++capture_writes;
                armodule::CapturePreflightReport report{};
                report.result = capture_result; report.instance = instance; report.now_ms = now;
                report.wire = capture_wire; report.checksum = armodule::CapturePreflightChecksum(report);
                capture_report_saved = armodule::Publish(ARMODULE_NAV15_CONFIG "/capture-preflight.tmp",
                    ARMODULE_NAV15_CONFIG "/capture-preflight.bin", report);
                capture_saved = report;
            }
        }
    };
#endif
    armodule::StartupControlReport diagnostic{}, saved{};
    unsigned report_writes = 0; bool report_saved = false;
#ifdef ARMODULE_NAV15
    bool enrollment_sent{};
#endif
#ifdef ARMODULE_NAV17
    const char *last_input="No setup button pressed.";
#endif
#ifdef ARMODULE_NAV12
    armodule::StatusReadSchedule status_reads;armodule::IdleHealth idle_health;
    armodule::Status cached_status{};bool cached_valid{};Result status_read_result{};
#endif
    bool released = false; const char *message = "No command sent. Module must be deliberately enabled first.";
#if defined(ARMODULE_NAV9) || defined(ARMODULE_NAV10) || defined(ARMODULE_NAV11) || defined(ARMODULE_NAV12)
    armodule::ReleasedLaunchGate launch_gate;
    armodule::NativeAttempt launch{};
    message = "Release all buttons. A once, release ALL buttons for one second to launch IDLE.";
#endif
    while (appletMainLoop()) {
#ifdef ARMODULE_CONTROLLER_SLOTS
        arsetupinput::Frame frame{};
        for (unsigned i = 0; i < pads.size(); ++i) {
            padUpdate(&pads[i]);
            frame[i] = {padGetStyleSet(&pads[i]),
                unsigned(pads[i].active_id_mask) | (unsigned(pads[i].active_handheld) << 8),
                padGetButtons(&pads[i])};
        }
        const auto operator_sample = operator_input.Update(frame);
        const bool operator_available = operator_sample.available;
        const auto down = operator_sample.down;
        const auto held = operator_sample.held;
        if (!operator_available) {
            released = false;
            if (!launch_gate.Consumed()) launch_gate = {};
        }
#else
        padUpdate(&pad);
        const bool operator_available = true;
        const auto down = padGetButtonsDown(&pad);
        const auto held = padGetButtons(&pad);
#endif
        if (down & HidNpadButton_Plus) break;
#ifdef ARMODULE_SECURITY_CAPTURE
        if(down==HidNpadButton_A && held==HidNpadButton_A && !security_capture.Prepared())
            (void)security_capture.Prepare(security_backend);
        security_capture.Poll(security_backend);
#endif
        if (operator_available && !(held & (HidNpadButton_A | HidNpadButton_ZL | HidNpadButton_X | HidNpadButton_Y))) released = true;
        armodule::Status status{};
#ifdef ARMODULE_NAV12
        if (status_reads.Due(Now())) {
            auto *fs=fsdevGetDeviceFileSystem("sdmc");
            status_read_result=fs?armodule::ReadStatusFrame(*fs,status,Now()):MAKERESULT(Module_Libnx,LibnxError_NotInitialized);
            if (!status_read_result) { cached_status=status;cached_valid=true; }
            else if (status_read_result!=0xe02) cached_valid=false;
        }
        status=cached_status;
        bool fresh=cached_valid && armodule::FreshStatus(status,Now());
#else
        bool fresh = armodule::ReadExact(armodule::StatusPath, status) && armodule::FreshStatus(status, Now());
#endif
        if (!last_query || Now()-last_query >= 1000) {
#ifdef ARMODULE_CENTRAL_BOND_B1
            Service service{};bond_query=smGetService(&service,"mc");
            if(R_SUCCEEDED(bond_query)){
                bond_query=serviceDispatch(&service,amazon_remote::central_bond::StatusCommand,
                    .buffer_attrs={SfBufferAttr_HipcPointer|SfBufferAttr_Out},.buffers={{&bond_status,sizeof bond_status}});
                serviceClose(&service);
                if(!bond_query && (bond_status.magic!=amazon_remote::central_bond::Magic || bond_status.version!=1))
                    bond_query=MAKERESULT(Module_Libnx,LibnxError_BadInput);
            }
#endif
            diagnostic = {}; diagnostic.now_ms = Now();
            auto result = pmdmntInitialize();
            if (R_SUCCEEDED(result)) { result = pmdmntGetProcessId(&diagnostic.process_id, armodule::ModuleProgramId); pmdmntExit(); }
            diagnostic.process_query_result = result;
            diagnostic.startup_valid = armodule::ReadExact(armodule::StartupPath, diagnostic.startup) && armodule::ValidStartup(diagnostic.startup);
            if (!diagnostic.startup_valid) diagnostic.startup = {};
            // Bounded, module-owned evidence for FTP retrieval; no launch API.
            const bool changed = !report_writes || diagnostic.process_query_result != saved.process_query_result ||
                diagnostic.process_id != saved.process_id || diagnostic.startup_valid != saved.startup_valid ||
                std::memcmp(&diagnostic.startup, &saved.startup, sizeof diagnostic.startup);
            if (changed && report_writes < 20) {
                ++report_writes;
                report_saved = armodule::Publish(armodule::ControlTemp, armodule::ControlPath, diagnostic);
                saved = diagnostic;
            }
            last_query = diagnostic.now_ms;
        }
#if defined(ARMODULE_NAV9) || defined(ARMODULE_NAV10) || defined(ARMODULE_NAV11) || defined(ARMODULE_NAV12)
        // Separate namespace plus a live PM query and matching early checkpoint.
        fresh = fresh && !diagnostic.process_query_result && diagnostic.process_id &&
            diagnostic.startup_valid && !diagnostic.startup.result &&
            status.instance==diagnostic.startup.instance && diagnostic.startup.now_ms<=status.now_ms;
#ifdef ARMODULE_NAV12
        if (fresh) idle_health.Observe(status,Now());else idle_health.Reset();
#endif
#ifdef ARMODULE_CAPTURE_PREFLIGHT
        // Read-only MC metadata. Never arm, stop or perform Bluetooth IPC here.
        static std::uint64_t capture_query_ms{};
        if (!capture_query_ms || Now() - capture_query_ms >= 1000 || (down & HidNpadButton_A)) {
            refresh_capture(fresh ? status.instance : 0); capture_query_ms = Now();
        }
        const bool capture_available = !capture_result && capture_report_saved &&
            amazon_remote::CaptureAvailable(capture_wire, Now());
        if (!capture_available && !launch_gate.Consumed()) launch_gate = {};
#endif
        if (!fresh && operator_available &&
#ifdef ARMODULE_SECURITY_CAPTURE
            security_capture.CanStart() &&
#endif
            launch_gate.Poll(Now(), down==HidNpadButton_A && held==HidNpadButton_A,
                                      held==0)
#ifdef ARMODULE_CAPTURE_PREFLIGHT
            && capture_available
#endif
            ) {
            message=armodule::RequestNavigationLaunch(launch);
        }
#endif
        const auto requested = down & (HidNpadButton_A | HidNpadButton_ZL | HidNpadButton_X
#ifndef ARMODULE_RECONNECT_CHECK
            | HidNpadButton_Y
#endif
        );
        if (requested && (released || (requested & HidNpadButton_X))) {
            // X is the stop command and wins over other simultaneous buttons.
            auto kind = (requested & HidNpadButton_X) ? armodule::CommandKind::Stop :
                (requested & HidNpadButton_A) ? armodule::CommandKind::Arm :
                (requested & HidNpadButton_Y) ? armodule::CommandKind::CheckController : armodule::CommandKind::Link;
#ifdef ARMODULE_NAV15
            if(kind==armodule::CommandKind::CheckController && status.state==unsigned(armodule::State::Ready))
                kind=armodule::CommandKind::Enroll;
#endif
            const bool allowed = fresh &&
#ifdef ARMODULE_SECURITY_CAPTURE
                (kind==armodule::CommandKind::Stop || security_capture.CanStart()) &&
#endif
#ifdef ARMODULE_NAV12
                status.last_sequence>=last_send &&
#endif
                (!armodule::StartupDiagnosticOnly || kind == armodule::CommandKind::Stop || kind == armodule::CommandKind::CheckController) &&
                (kind == armodule::CommandKind::Stop ||
                 (kind == armodule::CommandKind::Arm && status.state == unsigned(armodule::State::Idle)
#ifdef ARMODULE_CAPTURE_PREFLIGHT
                    && capture_available
#endif
#ifdef ARMODULE_NAV12
                    && idle_health.Ready(Now())
#endif
                 ) ||
                 (kind == armodule::CommandKind::CheckController && status.state == unsigned(armodule::State::Idle) &&
                  status.controller_check == unsigned(armodule::ControllerCheck::NotRun)) ||
#ifdef ARMODULE_NAV15
                 (kind==armodule::CommandKind::Enroll && !enrollment_sent &&
                  status.state==unsigned(armodule::State::Ready) && status.output_attached && !status.buttons &&
                  requested==HidNpadButton_Y && held==HidNpadButton_Y) ||
#endif
                 (kind == armodule::CommandKind::Link && status.state == unsigned(armodule::State::Scanning) && status.link_ready));
            if (!allowed) {
                message = "Ignored: stale/missing module, wrong state or link not READY. Not queued.";
#ifdef ARMODULE_NAV17
                last_input=!fresh?"NOT SENT: status not ready. Wait before pressing again.":
                    status.last_sequence<last_send?"NOT SENT: previous step is still being acknowledged.":
                    kind==armodule::CommandKind::Enroll && status.buttons?"NOT SENT: release all remote buttons first.":
                    kind==armodule::CommandKind::Enroll && enrollment_sent?"Check already requested; do not press Y again.":
                    "NOT SENT: use the button shown under NEXT.";
#endif
            }
            else {
                const auto previous = status.last_sequence > last_send ? status.last_sequence : last_send;
                if (previous == std::numeric_limits<std::uint64_t>::max()) message = "Command counter exhausted. No write.";
                else {
                    armodule::Command command{}; command.kind = unsigned(kind); command.instance = status.instance;
                    command.issued_ms = Now(); command.sequence = previous+1;
#ifdef ARMODULE_SECURITY_CAPTURE
                    const bool is_link=kind==armodule::CommandKind::Link;
                    const bool bridge_ready=!is_link ||
                        security_capture.BeforeLink(security_backend,command.instance,command.sequence);
                    // Timestamp the existing command only after the bounded
                    // diagnostic check. P3 still rechecks sighting freshness.
                    command.issued_ms=Now();
#endif
                    if (
#ifdef ARMODULE_SECURITY_CAPTURE
                        bridge_ready &&
#endif
#ifdef ARMODULE_NAV12
                        armodule::NavigationAbsent(armodule::CommandPath) && armodule::NavigationAbsent(armodule::CommandTemp) &&
#endif
                        armodule::Publish(armodule::CommandTemp, armodule::CommandPath, command)) {
                        last_send = command.sequence; message = "Command published. Check state/log for actual acknowledgement.";
#ifdef ARMODULE_SECURITY_CAPTURE
                        if(is_link)security_capture.Published(security_backend,true);
#endif
#ifdef ARMODULE_NAV17
                        last_input=kind==armodule::CommandKind::Enroll?"Y received. Waiting for CHECK SAVED.":
                            kind==armodule::CommandKind::Stop?"Stop requested.":
                            kind==armodule::CommandKind::Arm?
#ifdef ARMODULE_KNOWN_RECONNECT_CHECK
                                "Connection preparation requested.":
#else
                                "Search requested.":
#endif
                                "Connection requested.";
#endif
#ifdef ARMODULE_NAV15
                        if(kind==armodule::CommandKind::Enroll) {
                            enrollment_sent=true;
#ifdef ARMODULE_NAV16
                            message="One getter-only context command sent. NO pairing request; check saved record.";
#else
                            message="One explicit enrollment command sent. Check record; acceptance is NOT persisted-bond proof.";
#endif
                        }
#endif
                    } else {
#ifdef ARMODULE_SECURITY_CAPTURE
                        if(is_link && bridge_ready)security_capture.Published(security_backend,false);
#endif
                        message = "Command publication failed. Existing temp file is preserved.";
#ifdef ARMODULE_NAV17
                        last_input="NOT SENT: SD command write failed. Stop and open FTPD.";
#endif
                    }
                }
            }
        }
        if (requested) released = false; // Require a fresh release before another physical command.
        if (!last_draw || Now()-last_draw >= 250 || requested) {
            consoleClear();
#ifdef ARMODULE_NAV17
            amazon_remote::EnrollmentContextRecord context{};
            const bool context_saved=fresh && armodule::ReadExact(armodule::EnrollmentContextPath,context) &&
                amazon_remote::ValidEnrollmentContext(context) && context.instance==status.instance;
            const auto view_now=Now();
            const bool context_usable=context_saved &&
                arnativebond::ReadyBinding(status,diagnostic.startup,context,view_now,context.expected_handle);
            arsetupview::Input input{fresh, !diagnostic.process_query_result && bool(diagnostic.process_id),
                launch_gate.Consumed(), idle_health.Ready(Now()), enrollment_sent, context_saved, context_usable,
                status.state, status.result, status.pad_cleanup_result, bool(status.link_ready),
                bool(status.output_attached), !status.buttons && !status.acknowledged_buttons};
#ifdef ARMODULE_MANUFACTURER_SCAN_CHECK
#ifdef ARMODULE_STARTUP_TRACE
            armodule::ArmDiagnostic arm_diagnostic{};
            input.capture_closed=fresh && armodule::ReadExact(armodule::ArmDiagnosticPath,arm_diagnostic) &&
                armodule::CaptureClosed(arm_diagnostic,status);
#ifdef ARMODULE_DISCONNECT_CHECK
            auto view=arsetupview::SelectDisconnectCheck(input);
#ifdef ARMODULE_CAPTURE_PREFLIGHT
            view=arsetupview::SelectCapturePreflight(input,view,capture_wire,capture_result,Now());
            if (!capture_report_saved && ((!input.fresh && !input.process_present && !input.launch_consumed) ||
                (input.fresh && input.state==0)))
                view={"DIAGNOSTIC SAVE FAILED", "+ Exit and open FTPD. Do not retry.",
                    "No start request sent. Existing files are preserved.", arsetupview::Tone::Error};
#endif
#else
            const auto view=arsetupview::SelectStartupTrace(input);
#endif
#else
            const auto view=arsetupview::SelectManufacturerScan(input);
#endif
#elif defined(ARMODULE_SCAN_ASSISTED_CHECK)
            const auto view=arsetupview::SelectScanAssisted(input);
#elif defined(ARMODULE_KNOWN_RECONNECT_CHECK)
            const auto view=arsetupview::SelectKnownReconnect(input);
#elif defined(ARMODULE_RECONNECT_CHECK)
            const auto view=arsetupview::SelectReconnect(input);
#else
            const auto view=arsetupview::Select(input);
#endif
#ifdef ARMODULE_SECURITY_CAPTURE
            if(security_capture.Running())
                view={"CAPTURING SECURITY EVENTS", "Wait here for up to 15 seconds. Do not exit yet.",
                    "X can stop the connection. No secret keys are recorded.", arsetupview::Tone::Waiting};
            else if(security_capture.Finished() || security_capture.Blocked()) {
                const bool active=fresh && (status.state==1 || status.state==2 || status.state==3);
                const auto outcome=security_capture.Get().outcome;
                const bool complete=security_capture.Saved() && (outcome==1 || outcome==2);
                if(!fresh || status.state!=6)
                    view={security_capture.Blocked()?"REPORT ALREADY EXISTS":
                        !security_capture.Saved()?"CAPTURE NEEDS REVIEW":
                        complete && fresh && status.state==7?"DISCONNECTED / REPORT SAVED":
                        outcome==1?"SECURITY EVENTS SAVED":outcome==2?"NO SECURITY EVENTS SEEN":"CAPTURE NEEDS REVIEW",
                        active?"Joy-Con X once. Wait for DISCONNECTED.":"+ Exit, then open FTPD. Do not repeat.",
                        complete?"Report saved and readback verified. Saved pairing is NOT proven.":
                            "Existing evidence preserved. Stop and return to FTPD.",
                        complete?arsetupview::Tone::Good:arsetupview::Tone::Error};
                if(fresh && status.result && status.state!=6)
                    view={"SESSION ERROR",active?"Joy-Con X once. Wait for the stop result.":"+ Exit, then open FTPD. Do not repeat.",
                        security_capture.Saved()?"Metadata report saved. The connection needs review.":
                            "Capture also needs review. Existing evidence is preserved.",arsetupview::Tone::Error};
            }
#endif
#ifdef ARMODULE_CENTRAL_BOND_B1
#ifdef ARMODULE_NATIVE_PEER_C1
            view=arsetupview::SelectSavedDiscovery(view,input,bond_query,bond_status,true);
#elif defined(ARMODULE_SAVED_DISCOVERY_B3)
            view=arsetupview::SelectSavedDiscovery(view,input,bond_query,bond_status);
#else
            namespace cb=amazon_remote::central_bond;
            if(bond_query || bond_status.store==cb::Store::Error)
                view={"BOND STORAGE NOT READY","+ Exit and open FTPD.","No new pairing request. Existing storage is preserved.",arsetupview::Tone::Error};
            else if(!std::strcmp(view.status,"READY TO SEARCH") && bond_status.store==cb::Store::Saved)
                view={"SAVED BOND READY","Joy-Con A once to prepare reconnection.","Do not hold remote Home or enter pairing mode.",arsetupview::Tone::Waiting};
            else if(fresh && status.state==1 && bond_status.store==cb::Store::Saved)
                view={"READY TO RECONNECT","Press a normal remote button, then Joy-Con ZL once.","Do not hold Home or put the remote in pairing mode.",arsetupview::Tone::Waiting};
            else if(fresh && status.state==1 && bond_status.store==cb::Store::Empty)
                view={status.link_ready?"REMOTE FOUND":"SEARCHING FOR FIRST PAIRING",
                    status.link_ready?"Joy-Con ZL once. Then wait here.":"Hold remote Home until flashing. Wait for REMOTE FOUND.",
                    "This test saves one private bond for this remote.",arsetupview::Tone::Waiting};
            else if(fresh && status.state==2)
                view={bond_status.phase==cb::Phase::Saving?"SAVING PRIVATE BOND":"CONNECTING / SECURING LINK",
                    "Wait here. Do not press ZL again.","Input starts only after encryption and storage checks.",arsetupview::Tone::Waiting};
            else if(fresh && status.state==3 && bond_status.phase==cb::Phase::Ready)
                view={"CONNECTED / BOND SAVED","Try remote directions, Select and Back.",
                    "Then Joy-Con X once. Wait for DISCONNECTED; + exits.",arsetupview::Tone::Good};
            if(bond_status.phase==cb::Phase::Failed && fresh && status.state!=6 && status.state!=7)
                view={"BOND TEST FAILED","Joy-Con X if needed, then + and FTPD.","No automatic re-pair or saved-key replacement was attempted.",arsetupview::Tone::Error};
#endif
#endif
            const char *color=view.tone==arsetupview::Tone::Good ? "\x1b[32;1m" :
                view.tone==arsetupview::Tone::Error ? "\x1b[31;1m" : "\x1b[33;1m";
            std::printf("\n  AMAZON REMOTE / SETUP\n\n  %s%s\x1b[0m\n\n  NEXT: %s\n\n  %s\n\n",
                color,view.status,view.action,view.note);
#ifdef ARMODULE_RECONNECT_CHECK
#ifdef ARMODULE_SCAN_ASSISTED_CHECK
#ifdef ARMODULE_MANUFACTURER_SCAN_CHECK
#ifdef ARMODULE_STARTUP_TRACE
#ifdef ARMODULE_DISCONNECT_CHECK
#ifdef ARMODULE_CONTROLLER_SLOTS
#ifdef ARMODULE_CAPTURE_PREFLIGHT
#ifdef ARMODULE_CONNECT_DISPATCH
#ifdef ARMODULE_OPERATOR_SLOTS
#ifdef ARMODULE_SECURITY_CAPTURE
#ifdef ARMODULE_BOND_PROPERTIES
            std::printf("  BOND PROPERTIES M3 - no keys or saved-pairing changes\n\n");
#else
            std::printf("  SECURITY EVENTS M2 - existing P3 connection / P4 Joy-Cons\n\n");
#endif
#else
#ifdef ARMODULE_CENTRAL_BOND_B1
#ifdef ARMODULE_NATIVE_PEER_C1
#ifdef ARMODULE_NATIVE_HCI_C2
            std::printf("  HCI STATUS C2 - controller response diagnostic\n\n");
#else
            std::printf("  SAVED PEER C1 - native identity checked before connection\n\n");
#endif
#elif defined(ARMODULE_SAVED_DISCOVERY_B3)
            std::printf("  SAVED DISCOVERY B3 - fresh sighting / saved bond only\n\n");
#else
            std::printf("  CENTRAL BOND B1 - private saved pairing / reconnect trial\n\n");
#endif
#else
            std::printf("  JOY-CON INPUT P4 - any player / P3 Bluetooth unchanged\n\n");
#endif
#endif
#else
            std::printf("  CONNECT TIMING P3 - one ZL decision / bounded dispatch\n\n");
#endif
#else
            std::printf("  WINDOW CHECK P2 - eight slots / D3 Bluetooth unchanged\n\n");
#endif
#else
            std::printf("  CONTROLLER SLOTS P1 - eight slots / D3 Bluetooth unchanged\n\n");
#endif
            if (!operator_available)
                std::printf("  \x1b[33;1mUse player-1 Joy-Cons and release all buttons.\x1b[0m\n\n");
#else
            std::printf("  CLEANUP CHECK D3 - connect, navigate, then X to disconnect\n\n");
#endif
#else
            std::printf("  STARTUP CHECK S3 - S2 scan / ordinary mode\n\n");
#endif
#else
            std::printf("  MANUFACTURER SCAN S2 - ordinary mode / ZL once\n\n");
#endif
#else
            std::printf("  SCAN-ASSISTED CHECK S1 - one fresh sighting / ZL once\n\n");
#endif
#elif defined(ARMODULE_KNOWN_RECONNECT_CHECK)
            std::printf("  KNOWN-ADDRESS RECONNECT K1 - no scan / explicit bonding\n\n");
#elif defined(ARMODULE_DISCOVERY_TRACE)
            std::printf("  RECONNECT CHECK R2 - discovery diagnostics only\n\n");
#else
            std::printf("  RECONNECT CHECK R1 - no explicit pairing request\n\n");
#endif
#endif
            if(arsetupview::StopAcknowledged(input,status.last_sequence,last_send))
                last_input="Stop command finished. See the result above; + exits.";
            if(context_usable) std::printf("  Connection check expires in %llus.\n\n",
                static_cast<unsigned long long>((60000-(view_now-context.snapshot.end_ms))/1000));
            std::printf("  Last input: %s\n\n  X: Stop setup     +: Exit     Hold R: Details\n",last_input);
#ifdef ARMODULE_CONTROLLER_SLOTS
            if (!operator_available)
                std::printf("\n  \x1b[33;1m%s\x1b[0m\n",
                    operator_sample.sources ? "RELEASE ALL JOY-CON BUTTONS, then try again."
                                            : "NO JOY-CON INPUT. Connect a Joy-Con; HOME can exit.");
#endif
            if(held&HidNpadButton_R) {
                std::printf("\n  State=%s Fresh=%u Read=%08lx PM=%08lx\n  Result=%08lx Pad=%u ACK=%llu Context=%u/%u\n  %s\n",
                    Name(status.state),fresh,static_cast<unsigned long>(status_read_result),
                    static_cast<unsigned long>(diagnostic.process_query_result),static_cast<unsigned long>(status.result),
                    status.output_attached,static_cast<unsigned long long>(status.last_sequence),context_saved,context_usable,message);
                std::printf("  Diagnostic checkpoint saved=%u\n",report_saved);
#ifdef ARMODULE_CONTROLLER_SLOTS
                std::printf("  Slots=8 Operator=%u JoyConStyles=%08lx Sources=%03x\n",
                    operator_available,static_cast<unsigned long>(operator_sample.styles),
                    operator_sample.sources);
#endif
#ifdef ARMODULE_CAPTURE_PREFLIGHT
                std::printf("  Capture query=%08lx State=%lu Flags=%lu FirstClose=%lu at=%llums Saved=%u\n",
                    static_cast<unsigned long>(capture_result), static_cast<unsigned long>(capture_wire.state),
                    static_cast<unsigned long>(capture_wire.flags), static_cast<unsigned long>(capture_wire.first_close_reason),
                    static_cast<unsigned long long>(capture_wire.first_close_ms), capture_report_saved);
#endif
            }
#else
#if defined(ARMODULE_NAV9) || defined(ARMODULE_NAV10) || defined(ARMODULE_NAV11) || defined(ARMODULE_NAV12)
#ifdef ARMODULE_NAV17
            std::printf("Amazon Remote Navigation17: full UUID / client identity census\n\n");
#elif defined(ARMODULE_NAV16)
            std::printf("Amazon Remote Navigation16: SAME_SESSION getter-only context\n\n");
#elif defined(ARMODULE_NAV15_SYSTEM)
            std::printf("Amazon Remote Navigation15S: system-BTM enrollment test\n\n");
#elif defined(ARMODULE_NAV15)
            std::printf("Amazon Remote Navigation15: explicit enrollment test\n\n");
#elif defined(ARMODULE_NAV14)
            std::printf("Amazon Remote Navigation 14 - connection evidence\n\n");
#elif defined(ARMODULE_NAV13)
            std::printf("Amazon Remote Navigation 13 - owned controller lifetime\n\n");
            std::printf("After a pad recovery, release remote buttons before pressing again.\n");
#elif defined(ARMODULE_NAV12)
            std::printf("Amazon Remote Navigation 12 - stability preflight / native requests\n\n");
#elif defined(ARMODULE_NAV11)
            std::printf("Amazon Remote Navigation 11 - owned first status write\n\n");
#elif defined(ARMODULE_NAV10)
            std::printf("Amazon Remote Navigation 10 - system BTM / status checksum\n\n");
#else
            std::printf("Amazon Remote Navigation 9 - supervised test\n\n");
#endif
            std::printf("No status: A once, release ALL buttons1s to launch IDLE\nIDLE: release, A arms60s capture; flash remote\nSCANNING and Link_READY=1: ZL once connects\nX: stop/release pad   +: exit UI (module remains)\n\n");
#else
            std::printf("Amazon Remote Startup Check - alpha.2 diag1\n\n");
            std::printf("Y: one NEUTRAL pad attach/detach check (IDLE, no Bluetooth)\nA/ZL: DISABLED for startup diagnosis\nX: stop/release virtual controller   +: exit UI\n\n");
#endif
            if (!diagnostic.process_query_result && diagnostic.process_id)
                std::printf("PM: module process PRESENT, PID=%llu (not a health check)\n", static_cast<unsigned long long>(diagnostic.process_id));
            else std::printf("PM: no process confirmed, Result=%08lx\n", static_cast<unsigned long>(diagnostic.process_query_result));
            if (diagnostic.startup_valid) std::printf("Saved startup: %s Result=%08lx\nCheckpoint belongs to live status instance: %s\n",
                armodule::StartupName(diagnostic.startup.stage), static_cast<unsigned long>(diagnostic.startup.result),
                fresh && status.instance == diagnostic.startup.instance ? "YES" : "UNCONFIRMED (may be an earlier boot)");
            else std::printf("No valid early SD checkpoint. Loader/SM/FS remain unresolved.\n");
            std::printf("FTP diagnostic snapshot: %s\n\n", report_saved ? "SAVED" : "NOT SAVED");
#ifdef ARMODULE_NAV12
            std::printf("IDLE health: %us/15s %s   Status read Result=%08lx\n",idle_health.Seconds(),idle_health.Ready(Now())?"PASSED":"WAIT",static_cast<unsigned long>(status_read_result));
            std::printf("Do NOT arm/flash remote until IDLE health PASSED. Then A -> flash -> Link_READY=1 -> ZL.\n\n");
#endif
            if (fresh) {
                constexpr const char *checks[] = {"NOT RUN", "PASSED (service replies only)", "FAILED"};
                std::printf("State=%s Result=%08lx Link_READY=%u Pad_attached=%u\nRemote_buttons=%08lx Acknowledged_sequence=%llu\n",
                Name(status.state), static_cast<unsigned long>(status.result), status.link_ready, status.output_attached,
                static_cast<unsigned long>(status.buttons), static_cast<unsigned long long>(status.last_sequence));
                std::printf("Neutral_check=%s Pad_cleanup_result=%08lx\nLast_ACK_mask=%016llx Successful_Set_replies=%llu\n",
                    checks[status.controller_check], static_cast<unsigned long>(status.pad_cleanup_result),
                    static_cast<unsigned long long>(status.acknowledged_buttons), static_cast<unsigned long long>(status.successful_writes));
#ifdef ARMODULE_NAV15
#ifdef ARMODULE_NAV16
                amazon_remote::EnrollmentContextRecord context{};
                if(armodule::ReadExact(armodule::EnrollmentContextPath,context) &&
                   amazon_remote::ValidEnrollmentContext(context) && context.instance==status.instance) {
                    const auto &s=context.snapshot;
                    std::printf("Context SAVED: user=%u/%u handle=%u system=%u/%u handle=%u\nUser_Result=%08lx System_Result=%08lx Pairing_flag_Result=%08lx flag=%u\nNO enrollment request sent. This is not bond/ownership proof.\n",
                        s.user_connections.list.count,s.user_connections.list.matches,s.user_connections.target_handle,
                        s.system_connections.list.count,s.system_connections.list.matches,s.system_connections.target_handle,
                        static_cast<unsigned long>(s.user_connections.list.result),static_cast<unsigned long>(s.system_connections.list.result),
                        static_cast<unsigned long>(s.pairing_query),s.pairing_started);
                } else std::printf("No getter-only context saved for THIS module instance.\n");
#else
                amazon_remote::EnrollmentRecord enrollment{};
                if(armodule::ReadExact(armodule::EnrollmentPath,enrollment) &&
                   amazon_remote::ValidEnrollment(enrollment) && enrollment.instance==status.instance) {
                    constexpr const char *names[]={"NONE","REFUSED","PREPARED","REQUEST ACCEPTED","LISTED IN QUERY SCOPE","ALREADY LISTED","FAILED","TIMED OUT","CANCELLED"};
                    std::printf("Enrollment=%s Result=%08lx Request_issued=%u\nBefore: Result=%08lx count=%u target=%u\nAfter: Result=%08lx count=%u target=%u\n",
                        names[enrollment.state],static_cast<unsigned long>(enrollment.result),enrollment.request_issued,
                        static_cast<unsigned long>(enrollment.before_result),enrollment.before_count,enrollment.before_matches,
                        static_cast<unsigned long>(enrollment.after_result),enrollment.after_count,enrollment.after_matches);
                } else std::printf("No enrollment record for THIS live module instance.\n");
#endif
#endif
            }
            else std::printf("No current module status. No Bluetooth action permitted.\n");
            std::printf("\n%s\n\n", message);
#if defined(ARMODULE_NAV9) || defined(ARMODULE_NAV10) || defined(ARMODULE_NAV11) || defined(ARMODULE_NAV12)
#ifdef ARMODULE_NAV15
#ifdef ARMODULE_NAV16
            std::printf("READY/Pad1 + remote buttons released: Y ONCE saves SAME_SESSION context.\nGetter-only; NO pairing request, retry or global pairing/filter/radio change.\nAfter Context SAVED, X stops; + exits; return to FTPD.\nSetup90s, READY30min max. No reconnect/autoboot/wake support claimed.\n");
#else
            std::printf("READY/Pad1 + remote buttons released: Y ONCE requests BLE enrollment.\nThis may save the configured remote; AD-derived profile is experimental.\nNo automatic pair/retry. Listed is NOT durable/encrypted-bond proof.\nSetup90s, READY30min max. + exits; test HOME; reopen SAME app, X stops.\nNo reconnect/autoboot/wake/microphone support is claimed.\n");
#endif
#else
            std::printf("One attempt,90s connected limit. No reconnect/autoboot.\nAfter READY: + exits; try D-pad/OK/Back on HOME.\nNo wake or microphone support is claimed.\n");
#endif
#else
            std::printf("Startup diagnosis only. No Bluetooth/pairing commands.\nNo wake or microphone support is claimed.\n");
#endif
#endif // ARMODULE_NAV17 compact view; older tools retain their screens.
            last_draw = Now();
        }
        consoleUpdate(nullptr); svcSleepThread(20000000);
    }
#ifdef ARMODULE_SECURITY_CAPTURE
    security_capture.Exit(security_backend);
#endif
    consoleExit(nullptr); return 0;
}
