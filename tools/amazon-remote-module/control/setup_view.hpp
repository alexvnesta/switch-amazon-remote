// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
namespace arsetupview {
enum class Tone { Waiting, Good, Error };
struct View { const char *status; const char *action; const char *note; Tone tone; };
struct Input {
    bool fresh{}, process_present{}, launch_consumed{}, idle_passed{};
    bool context_requested{}, context_saved{}, context_usable{};
    std::uint32_t state{}, result{}, cleanup{};
    bool link_ready{}, pad_attached{}, remote_neutral{};
    bool capture_closed{};
};
// Presentation only: these messages grant no command/API permission.
inline View Select(const Input &i) {
    if (!i.fresh) {
        if (i.process_present) return {"WAITING FOR STATUS", "Wait. Do not press a setup button.", "If this persists, + exits; open FTPD.", Tone::Waiting};
        if (i.launch_consumed) return {"SETUP NOT READY", "+ Exit, then open FTPD.", "Do not restart setup. Logs will explain why.", Tone::Error};
        return {"NOT STARTED", "Joy-Con A: Start setup.", "Then release ALL buttons for one second.", Tone::Waiting};
    }
    if (i.state == 6) return {"DISCONNECTING", "Wait up to five seconds for the result.", "Controller output is off. Do not press A or ZL.", Tone::Waiting};
    if (i.state == 7 && !(i.result || i.cleanup || i.pad_attached || i.link_ready))
        return {"DISCONNECTED", "+ Exit, then open FTPD.", "BLE link absent in both views. Saved bonding is not tested.", Tone::Good};
    if (i.state == 7)
        return {"STOPPED - CHECK DETAILS", "+ Exit, then open FTPD.", "Cleanup had an error or inconsistent status. Do not retry.", Tone::Error};
    if (i.state == 4 && (i.result || i.cleanup)) return {"STOPPED - DETAILS SAVED", "+ Exit, then open FTPD.", "Session ended with an error. Hold R for details; do not retry.", Tone::Error};
    if (i.result || i.cleanup) return {"SETUP ERROR", "+ Exit, then open FTPD.", "Do not retry. Hold R for technical details.", Tone::Error};
    if (i.state == 4) return {"STOPPED", "+ Exit, then open FTPD.", "Controller output has been released.", Tone::Good};
    if (i.state == 5) return {"SUSPENDED", "+ Exit, then open FTPD.", "No automatic reconnect. Do not retry.", Tone::Waiting};
    if (i.state == 0) {
        if (!i.idle_passed) return {"CHECKING STARTUP", "Wait about 15 seconds.", "Do not start remote pairing yet.", Tone::Waiting};
        return {"READY TO SEARCH", "Joy-Con A: Search for the remote.", "Then hold REMOTE Home until its light flashes.", Tone::Good};
    }
    if (i.state == 1) {
        if (i.link_ready) return {"REMOTE FOUND", "Joy-Con ZL: Connect ONCE.", "Keep the remote in flashing pairing mode.", Tone::Good};
        return {"SEARCHING", "Hold REMOTE Home until its light flashes.", "Wait for REMOTE FOUND. Do not press ZL yet.", Tone::Waiting};
    }
    if (i.state == 2) return {"CONNECTING", "Wait. No button press is needed.", "Do not press A or ZL again.", Tone::Waiting};
    // The producer's link_ready flag admits SCANNING -> LINKING only.
    // READY must not depend on a flag that the producer clears in READY.
    if (i.state != 3 || !i.pad_attached)
        return {"CONNECTION NOT READY", "Wait. Do not open the bond trial.", "X stops setup; + exits.", Tone::Waiting};
    if (i.context_saved) {
        if (!i.context_usable) return {"CHECK NOT USABLE", "Joy-Con X: Stop, then + and FTPD.", "Check expired, input held, or connection check failed.", Tone::Error};
        return {"CHECK SAVED", "+ Exit; open Amazon Remote Native Bond Trial NOW.", "Leave setup running. Do NOT press X at this step.", Tone::Good};
    }
    if (i.context_requested) return {"SAVING CONNECTION CHECK", "Wait for the green CHECK SAVED screen.", "Do not exit or press Y again.", Tone::Waiting};
    if (!i.remote_neutral) return {"RELEASE REMOTE BUTTONS", "Release every button on the Amazon remote.", "Wait before saving the connection check.", Tone::Waiting};
    return {"CONNECTED - CHECK NOT SAVED", "Joy-Con Y: Save the connection check.", "Do NOT open the bond trial until CHECK SAVED.", Tone::Waiting};
}
// Same permission guards, different experiment. Never direct the operator to
// pairing mode, Y enrollment/context, or the old native bond trial.
inline View SelectReconnect(const Input &i) {
    if (!i.fresh || i.result || i.cleanup || i.state>=4 ||
        (i.state==0 && !i.idle_passed) || i.state==2) return Select(i);
    if (i.state==0)
        return {"READY TO SEARCH", "Joy-Con A: Search for the remote.", "Next: briefly press a REMOTE button. Do NOT hold Home.", Tone::Good};
    if (i.state==1) {
        if (i.link_ready)
            return {"REMOTE FOUND", "Joy-Con ZL: Connect ONCE.", "Do not put the remote into flashing pairing mode.", Tone::Good};
        return {"SEARCHING", "Briefly press a REMOTE button to wake it.", "Do NOT hold Home. Wait for REMOTE FOUND before ZL.", Tone::Waiting};
    }
    if (i.state!=3 || !i.pad_attached)
        return {"CONNECTION NOT READY", "Wait. X stops; + exits to FTPD.", "No pairing trial or retry in this test.", Tone::Waiting};
    return {"CONNECTED - TRY NAVIGATION", "Joy-Con +: Exit; try remote D-pad, OK and Back.", "Then reopen Setup: X stops; + exits; open FTPD.", Tone::Good};
}
inline View SelectKnownReconnect(const Input &i) {
    if (!i.fresh || i.result || i.cleanup || i.state>=4 ||
        (i.state==0 && !i.idle_passed) || i.state==2) return Select(i);
    if (i.state==0)
        return {"READY TO PREPARE", "Joy-Con A: Prepare one connection test.", "No scan or connection is started by this step.", Tone::Good};
    if (i.state==1) {
        if (i.link_ready)
            return {"READY TO CONNECT", "Briefly press REMOTE Up, then Joy-Con ZL ONCE.", "Do NOT hold Home. Presence and stored bonding are unproven.", Tone::Good};
        return {"PREPARING CONNECTION", "Wait. No button press yet.", "No discovery or automatic connection in this test.", Tone::Waiting};
    }
    if (i.state!=3 || !i.pad_attached)
        return {"CONNECTION NOT READY", "Wait. X stops; + exits to FTPD.", "No pairing trial or retry in this test.", Tone::Waiting};
    return {"CONNECTED - TRY NAVIGATION", "Joy-Con +: Exit; try remote Up, OK and Back.", "Then reopen Setup: X stops; + exits; open FTPD.", Tone::Good};
}
// S1 reuses the existing filtered scan and fresh target-sighting admission.
// Seeing an advertisement does not establish identity, security or bonding.
inline View SelectScanAssisted(const Input &i) {
    if (!i.fresh || i.result || i.cleanup || i.state>=4 ||
        (i.state==0 && !i.idle_passed) || i.state==2) return Select(i);
    if (i.state==0)
        return {"READY TO SEARCH", "Joy-Con A: Start the scan.", "Then hold REMOTE Home until its light flashes.", Tone::Good};
    if (i.state==1) {
        if (i.link_ready)
            return {"REMOTE SEEN - READY TO CONNECT", "Joy-Con ZL: Connect ONCE.", "Fresh target advertisement; identity/bonding unproven.", Tone::Good};
        return {"SEARCHING", "Hold REMOTE Home until its light flashes.", "Wait for REMOTE SEEN. Do not press ZL yet.", Tone::Waiting};
    }
    if (i.state!=3 || !i.pad_attached)
        return {"CONNECTION NOT READY", "Wait. X stops; + exits to FTPD.", "No bond trial or retry in this test.", Tone::Waiting};
    return {"CONNECTED - TRY NAVIGATION", "Joy-Con +: Exit; try remote Up, OK and Back.", "Then reopen Setup: X stops; + exits; open FTPD.", Tone::Good};
}
// Same sighting/connection guards as S1; only scan selection and instructions
// differ. No paired-state, identity or persistence claim from discovery.
inline View SelectManufacturerScan(const Input &i) {
    const auto base=SelectScanAssisted(i);
    if(!i.fresh || i.result || i.cleanup || i.state>=4 ||
       (i.state==0 && !i.idle_passed) || i.state==2) return base;
    if(i.state==0)
        return {"READY TO SEARCH", "Joy-Con A: Start the scan.", "Then briefly press REMOTE Up. Do NOT hold Home.", Tone::Good};
    if(i.state==1 && !i.link_ready)
        return {"SEARCHING", "Briefly press REMOTE Up. Do NOT hold Home.", "No REMOTE SEEN after35s? X stops. Then + and FTPD.", Tone::Waiting};
    return base;
}
inline bool StopAcknowledged(const Input &i, std::uint64_t acknowledged, std::uint64_t sent) {
    return i.fresh && (i.state==4 || i.state==7) && sent && acknowledged>=sent;
}
inline View SelectStartupTrace(const Input &i) {
    if(i.fresh && i.state==4 && i.result && !i.cleanup && !i.pad_attached && !i.link_ready && i.capture_closed)
        return {"CAPTURE WINDOW CLOSED", "+ Exit; open FTPD. A fresh reboot is required.",
            "No scan or connection was started. Do not retry.", Tone::Error};
    return SelectManufacturerScan(i);
}
// D1 changes cleanup, not discovery/security. Use the device-proven flashing
// discovery workflow and never direct the operator into the old bond trial.
inline View SelectDisconnectCheck(const Input &i) {
    if(i.fresh && i.state==4 && i.result && !i.cleanup && !i.pad_attached && !i.link_ready && i.capture_closed)
        return SelectStartupTrace(i);
    return SelectScanAssisted(i);
}
}
