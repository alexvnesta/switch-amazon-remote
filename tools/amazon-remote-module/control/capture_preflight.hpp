// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../../../mc_mitm/source/amazon_remote/capture_diagnostics.hpp"
#include "setup_view.hpp"
namespace armodule {
struct CapturePreflightReport {
    std::uint32_t magic{0x41525032}, version{1}, result{}, reserved{};
    std::uint64_t instance{}, now_ms{};
    amazon_remote::CaptureDiagnosticsWire wire{};
    std::uint32_t checksum{}, reserved2{};
};
static_assert(sizeof(CapturePreflightReport) == 88 && offsetof(CapturePreflightReport, checksum) == 80);
inline std::uint32_t CapturePreflightChecksum(const CapturePreflightReport &r) {
    std::uint32_t h = 2166136261u;
    const auto *p = reinterpret_cast<const unsigned char *>(&r);
    for (unsigned i = 0; i < 80; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}
}
namespace arsetupview {
inline View SelectCapturePreflight(const Input &input, const View &base,
                                  const amazon_remote::CaptureDiagnosticsWire &wire,
                                  std::uint32_t result, std::uint64_t now) {
    using namespace amazon_remote;
    // Never replace active-link or Stop/Disconnect instructions. The frontend
    // only tightens launch/Arm admission and improves known capture failures.
    const bool before_start = !input.fresh && !input.process_present && !input.launch_consumed;
    if (!before_start && !(input.fresh && input.state == 0) && !input.capture_closed) return base;
    if (result || !ValidCaptureDiagnostics(wire, now))
        return {"WINDOW STATUS UNAVAILABLE", "+ Exit and open FTPD. Do not retry.", "Observer version or status could not be verified.", Tone::Error};
    if (!(wire.flags & CaptureSnapshotStable) || (wire.flags & CaptureClosePending))
        return {"CHECKING WINDOW", "Wait. Do not start remote pairing.", "Window changed during the check. No request sent.", Tone::Waiting};
    if (CaptureAvailable(wire, now)) return base;
    if (wire.state == 5)
        return {"WAITING FOR AWAKE", "Wait. Do not put the remote in pairing mode.", "Unused window paused by power transition. No attempt sent.", Tone::Waiting};
    if (wire.state == 4)
        return {"CAPTURE DISABLED", "+ Exit and open FTPD.", "Observer diagnostic configuration is unavailable.", Tone::Error};
    if (wire.state == 2)
        return {"CAPTURE TIME EXPIRED", "+ Exit and open FTPD. Do not retry.", "The armed 60-second capture period has ended.", Tone::Error};
    if (wire.state == 1)
        return {"CAPTURE ALREADY IN USE", "+ Exit and open FTPD. Do not retry.", "A capture was already armed. No new request sent.", Tone::Error};
    if (wire.first_close_reason == unsigned(CaptureCloseReason::SleepRequest))
        return {"WINDOW CLOSED: SLEEP REQUEST", "+ Exit and open FTPD. Do not retry.", "Sleep was the first recorded close request.", Tone::Error};
    if (wire.first_close_reason == unsigned(CaptureCloseReason::ShutdownRequest))
        return {"WINDOW CLOSED: SHUTDOWN REQUEST", "+ Exit and open FTPD. Do not retry.", "Shutdown was the first recorded close request.", Tone::Error};
    if (wire.first_close_reason == unsigned(CaptureCloseReason::ExplicitEnd))
        return {"WINDOW CLOSED: EXPLICIT STOP", "+ Exit and open FTPD. Do not retry.", "An explicit capture-stop request was recorded.", Tone::Error};
    if (wire.first_close_reason == unsigned(CaptureCloseReason::PowerError))
        return {"POWER STATE ERROR", "+ Exit and open FTPD. Do not retry.", "Power request or acknowledgement could not be verified.", Tone::Error};
    return {"WINDOW CLOSED: REASON UNKNOWN", "+ Exit and open FTPD. Do not retry.", "No closing request was recorded. This does not prove sleep.", Tone::Error};
}
}
