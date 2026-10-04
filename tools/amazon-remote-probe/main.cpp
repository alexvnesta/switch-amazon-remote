// Independent Amazon remote BLE diagnostic and optional virtual-controller prototype.
// GPL-2.0-only. No private MissionControl code or binary patches.
#include "../../mc_mitm/source/amazon_remote/libnx_session.hpp"
#include "../../mc_mitm/source/amazon_remote/diagnostic_log.hpp"
#include "../../mc_mitm/source/amazon_remote/ble_preflight.hpp"
#include "../../mc_mitm/source/amazon_remote/capture_lease.hpp"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <cerrno>

namespace {
amazon_remote::RotatingLog g_log;
PrintConsole g_status_console{}, g_log_console{};
std::uint64_t g_session_start;
std::uint64_t Now() { return armTicksToNs(armGetSystemTick()) / 1000000; }
void Log(const char *message) {
    char line[amazon_remote::RotatingLog::LineLimit];
    amazon_remote::FormatLogLine(line, sizeof line, g_session_start, Now(), message);
    std::printf("%s\n", line);
    if (g_log.Active() && !g_log.Write(line)) std::printf("Persistent log stopped after storage/rotation error; console logging continues.\n");
}
void Status(const char *text, std::uint64_t remaining_ms = 0) {
    consoleSelect(&g_status_console); consoleClear();
    std::printf("Amazon Remote Probe 0.1.10 - INPUT DIAGNOSTIC\n%.79s\n", text);
    std::printf("Capture: %llu seconds (scan only). Security may pair; no output.\n", static_cast<unsigned long long>((remaining_ms+999)/1000));
    std::printf("A starts one test; ZL only when READY; + exits.");
    consoleSelect(&g_log_console);
}
Result ReadCapture(Service *mc, amazon_remote::CaptureLeaseWire &lease) {
    lease = {};
    if (!serviceIsActive(mc)) return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    const auto rc = serviceDispatch(mc, 65002,
        .buffer_attrs = { SfBufferAttr_HipcPointer | SfBufferAttr_Out },
        .buffers = { { &lease, sizeof lease } });
    if (R_FAILED(rc)) return rc;
    if (lease.magic != amazon_remote::CaptureLeaseMagic || lease.version != amazon_remote::CaptureLeaseVersion ||
        lease.reserved || lease.state > unsigned(amazon_remote::CaptureLeaseState::Unavailable) || !lease.now_boot_ms ||
        (lease.state == unsigned(amazon_remote::CaptureLeaseState::Active) &&
         (lease.deadline_boot_ms <= lease.now_boot_ms || lease.deadline_boot_ms-lease.now_boot_ms > 60000)))
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    return 0;
}
bool CaptureActive(const amazon_remote::CaptureLeaseWire &lease, std::uint64_t now) {
    return lease.state == unsigned(amazon_remote::CaptureLeaseState::Active) &&
        now >= lease.now_boot_ms && lease.deadline_boot_ms > now && lease.deadline_boot_ms-now <= 60000;
}
void EndCapture(Service *mc, bool &armed) {
    if (!armed) return;
    if (serviceIsActive(mc)) {
        const auto rc = serviceDispatch(mc, 65003);
        char line[128]{}; std::snprintf(line, sizeof line, "API=mc65003 end-only capture Result=%08lx; no rearm", static_cast<unsigned long>(rc)); Log(line);
    }
    armed = false;
}
amazon_remote::FlagState ReadDebugFlag(const char *section, const char *key) {
    u64 declared = 0, copied = 0;
    u8 value = 0;
    auto rc = setsysGetSettingsItemValueSize(section, key, &declared);
    char message[256]{};
    std::snprintf(message, sizeof message, "setting=%s!%s size_result=%08lx declared_size=%llu",
        section, key, static_cast<unsigned long>(rc), static_cast<unsigned long long>(declared)); Log(message);
    if (R_FAILED(rc) || declared != 1) { Log("Setting is UNKNOWN; no default or disabled-state inference."); return amazon_remote::FlagState::Unknown; }
    rc = setsysGetSettingsItemValue(section, key, &value, sizeof value, &copied);
    const auto state = amazon_remote::DecodeFlag(R_SUCCEEDED(rc), copied, value);
    std::snprintf(message, sizeof message, "setting=%s!%s read_result=%08lx copied=%llu value=%02x interpreted=%s",
        section, key, static_cast<unsigned long>(rc), static_cast<unsigned long long>(copied), value,
        state == amazon_remote::FlagState::Unknown ? "UNKNOWN" : state == amazon_remote::FlagState::True ? "true" : "false"); Log(message);
    return state;
}
amazon_remote::BlePreflight ReadBlePreflight() {
    amazon_remote::BlePreflight state{};
    auto rc = setsysInitialize(); char message[160]{};
    std::snprintf(message, sizeof message, "Read-only setsysInitialize Result=%08lx", static_cast<unsigned long>(rc)); Log(message);
    if (R_SUCCEEDED(rc)) {
        state.ble_disabled = ReadDebugFlag("hid_debug", "ble_disabled");
        state.config_skip_boot = ReadDebugFlag("bluetooth_config", "skip_boot");
        state.debug_skip_boot = ReadDebugFlag("bluetooth_debug", "skip_boot");
        bool enabled = false; rc = setsysGetBluetoothEnableFlag(&enabled);
        std::snprintf(message, sizeof message, "Bluetooth preference Result=%08lx enabled=%s (not proof of BLE radio state)",
            static_cast<unsigned long>(rc), R_FAILED(rc) ? "UNKNOWN" : enabled ? "true" : "false"); Log(message);
        setsysExit();
    }
    rc = btmsysInitialize();
    std::snprintf(message, sizeof message, "Read-only btmsysInitialize Result=%08lx", static_cast<unsigned long>(rc)); Log(message);
    if (R_SUCCEEDED(rc)) {
        bool on = false; rc = btmsysGetRadioOnOff(&on);
        if (R_SUCCEEDED(rc)) state.radio_on = on ? amazon_remote::FlagState::True : amazon_remote::FlagState::False;
        std::snprintf(message, sizeof message, "BTM radio query Result=%08lx radio_on=%s (radio-on does not independently prove BLE-on)",
            static_cast<unsigned long>(rc), R_FAILED(rc) ? "UNKNOWN" : on ? "true" : "false"); Log(message);
        btmsysExit();
    }
    if (state.ExplicitlyBlocked()) Log("BLE preflight BLOCKED by an explicit disable/skip-boot flag or radio-off. No settings changed; connect disabled.");
    else Log("BLE preflight: no confirmed disable gate. Unknown values stay unknown; BLE enablement/pairing are NOT proven.");
    Log("Captured candidate AR advertises HID16 and manufacturer710104...; standard BTM general expects fixed01, smart scan expects UUID128.");
    Log("No compatible generic BTM scan established. No fake wildcard, global driver initialization, filter clearing or unpairing will be attempted.");
    Log("Input diagnostic: fingerprint first, then NoMitm-requested security read and validated keyboard/consumer notifications only. May initiate pairing/encryption; controller output and vendor/voice writes disabled.");
    return state;
}
bool Target(BtdrvAddress &target) {
    FILE *f = std::fopen("sdmc:/config/amazon-remote/address.txt", "r");
    if (!f) return false;
    unsigned octets[6]{}; char extra = 0;
    const int read = std::fscanf(f, "%2x:%2x:%2x:%2x:%2x:%2x %c", &octets[0], &octets[1], &octets[2], &octets[3], &octets[4], &octets[5], &extra);
    std::fclose(f);
    if (read != 6) return false;
    bool nonzero = false;
    for (unsigned i = 0; i < 6; ++i) { if (octets[i] > 255) return false; target.address[i] = octets[i]; nonzero |= octets[i] != 0; }
    return nonzero;
}
struct FocusContext { amazon_remote::LibnxSession *remote; bool *connected; bool *initialized; bool *disarmed; Service *mc; bool *armed; };
void FocusHook(AppletHookType type, void *parameter) {
    auto &context = *static_cast<FocusContext *>(parameter);
    if (type == AppletHookType_OnExitRequest ||
        (type == AppletHookType_OnFocusState && appletGetFocusState() != AppletFocusState_InFocus)) {
        *context.disarmed = true; // Never rearm after focus loss in this process.
        context.remote->Close(); *context.connected = *context.initialized = false;
        EndCapture(context.mc, *context.armed);
        serviceClose(context.mc);
        Log("Focus/exit transition: scan stop attempted, user session and mc closed; test disarmed. Pending marker retained, reboot required after any attempted link.");
    }
}
}
int main(int, char **) {
    consoleInit(&g_status_console); consoleInit(&g_log_console);
    consoleSetWindow(&g_status_console, 0, 0, g_status_console.consoleWidth, 4);
    consoleSetWindow(&g_log_console, 0, 5, g_log_console.consoleWidth, g_log_console.consoleHeight-5);
    Status("IDLE - make remote flash, then press A. No boot-time race.");
    mkdir("sdmc:/config/amazon-remote", 0777);
    g_session_start = Now();
    if (!g_log.Open("sdmc:/config/amazon-remote/probe.log")) std::printf("Persistent log unavailable; console logging only.\n");
    Log("Amazon K7Q3M7 public-API prototype 0.1.10 INPUT_DIAGNOSTIC; session begin");
    Log("Capture starts only on physical A: one60s passive lease per boot. Startup trace deadline is independent. ZL while NOTREADY is ignored, not queued.");
    Log("Cache projection: unique primary DI/HID services and only PnP/report-map characteristics. Unrelated SDK records are not adopted or reclassified.");
    char startup[256]{}; const auto horizon = hosversionGet();
    std::snprintf(startup, sizeof startup, "Startup applet_type=%u ARUID=%016llx Horizon=%u.%u.%u focus_state=%u",
        unsigned(appletGetAppletType()), static_cast<unsigned long long>(appletGetAppletResourceUserId()),
        unsigned(HOSVER_MAJOR(horizon)), unsigned(HOSVER_MINOR(horizon)), unsigned(HOSVER_MICRO(horizon)), unsigned(appletGetFocusState()));
    Log(startup);
    Log("Log limit: 256KiB per file, two backups; timestamps are monotonic boot/session milliseconds, not wall time.");
    Log("Requires the crashing experimental MissionControl BLE module to be disabled first.");
    Log("A: smart scan. Physical ZL: ONE fresh-target link request and authorized security/input diagnostic. +: exit.");
    Log("B/Y/X DISABLED. Exact fingerprint required before one NoMitm security read, ReportReference reads and two standard CCC writes. No explicit bond API, PIN/SSP replies, vendor/voice writes or controller output.");
    Log("R idle: read-only cache census. Neither user cache bucket authorizes connection. Pending request cannot be cancelled publicly: reboot after EVERY link attempt.");
    Log("Use isolated custom-Bluetooth test only. Presence/MAC/ARUID handle are not authenticated identity or exclusive driver ownership. No MAC-only disconnect performed.");
    Log("Address config: sd:/config/amazon-remote/address.txt (one MAC address).");
    PadState pad; padConfigureInput(1, HidNpadStyleSet_NpadStandard); padInitializeDefault(&pad);
    amazon_remote::LibnxSession remote(Log, true, true);
    BtdrvAddress target{}; Result error = 0;
    const bool target_valid = Target(target);
    const auto preflight = ReadBlePreflight();
    struct stat marker{}; errno = 0;
    const bool latched = ::stat("sdmc:/config/amazon-remote/link-test-pending.txt", &marker) == 0 || errno != ENOENT;
    if (latched) Log("Persistent link-test-pending marker exists or status is ambiguous: A/ZL BLOCKED. Operator must confirm reboot, preserve then remove marker; no timer-based reset.");
    bool initialized = false, connected = false, disarmed = false, capture_armed = false;
    Service mc{}; amazon_remote::SightingWire sighting{}; std::uint64_t last_snapshot_poll = 0;
    amazon_remote::CaptureLeaseWire lease{}; std::uint64_t last_capture_poll = 0, last_status_draw = 0;
    auto capture_result = smGetService(&mc, "mc");
    if (R_SUCCEEDED(capture_result)) capture_result = ReadCapture(&mc, lease);
    char capture_line[160]{};
    std::snprintf(capture_line, sizeof capture_line, "Read-only capture status startup Result=%08lx state=%u; unsupported API never falls back to blind connect", static_cast<unsigned long>(capture_result), lease.state); Log(capture_line);
    auto last_sighting_assessment = amazon_remote::SightingAssessment::Invalid;
    bool ready_displayed = false, start_buttons_released = false;
    FocusContext focus{&remote, &connected, &initialized, &disarmed, &mc, &capture_armed}; AppletHookCookie focus_cookie{};
    appletHook(&focus_cookie, FocusHook, &focus);
    // Library applet mode cannot configure this guarantee: stay logging-only.
    const auto focus_result = appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleepNotify);
    amazon_remote::FormatApiResult(startup, sizeof startup, "appletSetFocusHandlingMode(SuspendHomeSleepNotify)", focus_result, unsigned(remote.GetPhase()), unsigned(remote.GetError()));
    Log(startup);
    const bool navigation_allowed = R_SUCCEEDED(focus_result) && appletGetAppletType() == AppletType_Application;
    if (!navigation_allowed) Log("Focus notification mode unavailable; read-only checks only. Full-memory mode required for ALL Bluetooth requests and output.");
    if (!target_valid) Log("No valid target address; connection disabled.");
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (!(padGetButtons(&pad) & (HidNpadButton_A | HidNpadButton_ZL))) start_buttons_released = true;
        const auto raw_down = padGetButtonsDown(&pad);
        const auto down = start_buttons_released ? raw_down : raw_down & ~(HidNpadButton_A | HidNpadButton_ZL);
        const auto press_ms = Now();
        // Only a READY banner from a preceding frame can authorize the physical
        // ZL edge. A+ZL or an early ZL is never queued until a future sighting.
        const bool ready_at_press = ready_displayed && connected && remote.ScanActive() && CaptureActive(lease, press_ms) &&
            last_sighting_assessment == amazon_remote::SightingAssessment::FreshCandidate &&
            press_ms >= sighting.captured_boot_ms && press_ms-sighting.captured_boot_ms <= 3000;
        if (down & HidNpadButton_Plus) break;
        const auto connect_buttons = down & HidNpadButton_A;
        const bool transport_allowed = navigation_allowed && appletGetFocusState() == AppletFocusState_InFocus;
        if (down & HidNpadButton_R) {
            if (!transport_allowed || !target_valid || initialized || connected || error || connect_buttons)
                Log("Cache census blocked: idle safe-focus full-memory app with valid target required; no new Bluetooth call.");
            else {
                const auto census_result = amazon_remote::ReadOnlyScanCacheCensus(target, Log);
                char message[128]{}; std::snprintf(message, sizeof message, "Read-only cache census Result=%08lx; existing transport modes unchanged", static_cast<unsigned long>(census_result)); Log(message);
            }
        }
        if (connect_buttons && !transport_allowed) Log("Request blocked: safe focus handling/full-memory foreground required; no scan or connection started.");
        if (down & (HidNpadButton_B | HidNpadButton_Y | HidNpadButton_X)) Log("B/Y/X disabled; no extra Bluetooth/output call.");
        if (connect_buttons && (latched || disarmed || remote.LinkAttempted())) Log("A blocked by persistent/process safety latch; no retry.");
        if (!connected && !latched && !disarmed && !remote.LinkAttempted() && target_valid && !preflight.ExplicitlyBlocked() && transport_allowed && !error && connect_buttons) {
            capture_result = ReadCapture(&mc, lease);
            error = capture_result;
            if (!error && lease.state != unsigned(amazon_remote::CaptureLeaseState::Available)) error = MAKERESULT(Module_Libnx, LibnxError_BadInput);
            if (!error) {
                error = serviceDispatch(&mc, 65001); capture_armed = R_SUCCEEDED(error);
                char line[128]{}; std::snprintf(line, sizeof line, "Physical A: API=mc65001 one-shot capture arm Result=%08lx", static_cast<unsigned long>(error)); Log(line);
            }
            if (!error) { error = ReadCapture(&mc, lease); if (!error && !CaptureActive(lease, Now())) error = MAKERESULT(Module_Libnx, LibnxError_BadInput); }
            if (!error) { error = remote.InitializeIdentityTest(target); initialized = R_SUCCEEDED(error); }
            if (!error) { error = remote.StartLinkOnlyScan(Now()); connected = R_SUCCEEDED(error); }
        }
        const auto now = Now();
        if (serviceIsActive(&mc) && (!last_capture_poll || now-last_capture_poll >= 250 || (down & HidNpadButton_ZL))) {
            last_capture_poll = now; capture_result = ReadCapture(&mc, lease);
            if (capture_armed && !remote.LinkAttempted() && (R_FAILED(capture_result) || !CaptureActive(lease, Now()))) {
                Log("Capture EXPIRED/ENDED/unavailable before connection request. No request; + exits; reboot before a new capture test.");
                error = R_FAILED(capture_result) ? capture_result : MAKERESULT(Module_Libnx, LibnxError_BadInput);
            }
        }
        if (connected && transport_allowed && remote.ScanActive() &&
            (!last_snapshot_poll || now-last_snapshot_poll >= 500 || (down & HidNpadButton_ZL))) {
            last_snapshot_poll = now;
            amazon_remote::SightingWire fresh{};
            const auto rc = serviceDispatch(&mc, 65000,
                .buffer_attrs = { SfBufferAttr_HipcPointer | SfBufferAttr_Out },
                .buffers = { { &fresh, sizeof fresh } });
            if (R_SUCCEEDED(rc) && CaptureActive(lease, Now())) sighting = fresh;
            else { sighting = {}; char message[128]{}; std::snprintf(message, sizeof message, "API=mc65000 sighting Result=%08lx; no candidate authorization", static_cast<unsigned long>(rc)); Log(message); }
            amazon_remote::SightingAddress configured{};
            std::memcpy(configured.data(), target.address, configured.size());
            const auto assessment = amazon_remote::AssessSighting(sighting, configured, remote.ScanStarted(), Now());
            if (assessment != last_sighting_assessment) {
                last_sighting_assessment = assessment;
                if (assessment != amazon_remote::SightingAssessment::FreshCandidate) ready_displayed = false;
                if (assessment == amazon_remote::SightingAssessment::FreshCandidate)
                    Log("Fresh configured-target advertisement present. Press physical ZL ONCE for fingerprint + security/input test; security may pair. Not authenticated identity or pairing proof.");
                else Log("No fresh matching advertisement available now; physical ZL will not request a connection.");
            }
        }
        if (down & HidNpadButton_ZL) {
            if (!connected || !transport_allowed || latched || disarmed || remote.LinkAttempted() || !remote.ScanActive()) Log("Physical ZL blocked: active scan, safe foreground and unused link attempt required.");
            else if (!ready_at_press || !CaptureActive(lease, Now())) Log("ZL NOTREADY: ignored, not queued. Wait for fixed READY banner, then press ZL once.");
            else error = remote.ConfirmObservedLink(sighting, Now(), lease.deadline_boot_ms);
        }
        if (connected && !error) error = remote.Poll(Now());
        if (!last_status_draw || Now()-last_status_draw >= 100) {
            last_status_draw = Now();
            const auto draw_ms = Now();
            const auto remaining = CaptureActive(lease, draw_ms) ? lease.deadline_boot_ms-draw_ms : 0;
            ready_displayed = false;
            if (latched) Status("BLOCKED - old connection test lock; confirmed reboot recovery needed.");
            else if (error || disarmed) Status(remote.FingerprintObserved() ? "STOPPED after fingerprint match - see log. + exits; no retry." : "STOPPED - see result in log below. + exits; no retry.");
            else if (remote.Ready()) Status("INPUT READY - press remote buttons one at a time. Logging only.", remaining);
            else if (remote.GetPhase() == amazon_remote::Phase::SecurityRead) Status("FINGERPRINT MATCHED - requesting security; pairing not yet proven.", remaining);
            else if (remote.GetPhase() == amazon_remote::Phase::SecurityReadVerified || remote.GetPhase() == amazon_remote::Phase::References || remote.GetPhase() == amazon_remote::Phase::Subscribe) Status("SECURITY READ RETURNED - validating input reports; bond unverified.", remaining);
            else if (remote.GetPhase() == amazon_remote::Phase::FingerprintVerified) Status("FINGERPRINT MATCHED - next: security read and input discovery.", remaining);
            else if (remote.LinkConfirmed()) Status("CONNECTED - reading device ID / HID map; no buttons yet.", remaining);
            else if (remote.LinkAttempted()) Status("ONE CONNECTION REQUEST SUBMITTED - waiting; do not retry.", remaining);
            else if (R_FAILED(capture_result)) Status("UNAVAILABLE - module/API mismatch; see logs; no Bluetooth request.");
            else if (lease.state == unsigned(amazon_remote::CaptureLeaseState::Expired) || lease.state == unsigned(amazon_remote::CaptureLeaseState::Ended)) Status("EXPIRED / ENDED - reboot before another capture test.");
            else if (lease.state == unsigned(amazon_remote::CaptureLeaseState::Unavailable)) Status("UNAVAILABLE - diagnostic opt-in/config absent; no request.");
            else if (connected && CaptureActive(lease, draw_ms) && last_sighting_assessment == amazon_remote::SightingAssessment::FreshCandidate && draw_ms >= sighting.captured_boot_ms && draw_ms-sighting.captured_boot_ms <= 3000) {
                Status("READY - press ZL ONCE now.", remaining); ready_displayed = true;
            }
            else if (connected) Status("WAITING FOR FRESH REMOTE - ZL would be ignored.", remaining);
            else if (!start_buttons_released) Status("Release A and ZL first; then make remote flash and press A.");
            else Status("IDLE - make remote flash, then press A. No boot-time race.");
        }
        if (error) {
            Status(remote.FingerprintObserved() ? "STOPPED after fingerprint match - see log. + exits; no retry." : "STOPPED - see result in log below. + exits; no retry.");
            char message[160]; std::snprintf(message, sizeof message, "Stopped: Result=%08lx phase=%u error=%u; check console/persistent log. + exits.",
                static_cast<unsigned long>(error), unsigned(remote.GetPhase()), unsigned(remote.GetError())); Log(message);
            remote.Close(); EndCapture(&mc, capture_armed); serviceClose(&mc); connected = initialized = false; disarmed = true;
            // Retain the error and permit exit only; no automatic retry/pairing storm.
            while (appletMainLoop()) { padUpdate(&pad); if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break; consoleUpdate(nullptr); svcSleepThread(20000000); }
            break;
        }
        consoleUpdate(nullptr); svcSleepThread(20000000);
    }
    appletUnhook(&focus_cookie); remote.Close(); EndCapture(&mc, capture_armed); serviceClose(&mc);
    Log("Session end"); g_log.Close();
    consoleExit(nullptr); return 0;
}
