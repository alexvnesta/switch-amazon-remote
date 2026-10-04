// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../../mc_mitm/source/amazon_remote/remote_decoder.hpp"
#include <cstdint>

namespace armodule {
// Public libnx HidNpadButton/HiddbgNpadButton bit positions, checked in adapter.
// Media/voice/app buttons have no default side effect. No synthetic repeats,
// macros, vendor writes, fake microphone or volume-control service calls.
constexpr std::uint64_t MapButtons(std::uint32_t remote) {
    using namespace amazon_remote;
    std::uint64_t out = 0;
    if (remote & Confirm) out |= 1ull << 0; // A
    if (remote & Back) out |= 1ull << 1;    // B
    if (remote & Menu) out |= 1ull << 10;   // Plus
    if (remote & Left) out |= 1ull << 12;
    if (remote & Up) out |= 1ull << 13;
    if (remote & Right) out |= 1ull << 14;
    if (remote & Down) out |= 1ull << 15;
    if (remote & Home) out |= 1ull << 18;
    return out;
}

// Safety bound for this diagnostic candidate: a continuous mapped hold lasting
// ten seconds stops the test rather than risk an indefinitely stuck button.
// Changing one held button to another does not extend the continuous hold.
class InputHoldGuard {
public:
    bool Accept(std::uint32_t buttons, std::uint64_t now) {
        if (!now || (last_ && now < last_)) return false;
        last_ = now;
        if (!MapButtons(buttons)) { held_since_ = 0; return true; }
        if (!held_since_) held_since_ = now;
        return now-held_since_ < 10000;
    }
private:
    std::uint64_t held_since_{}, last_{};
};

// Backend owns exactly one service session, work buffer and virtual handle.
// Never reads/applies/reassigns the global controller list. Failures are latched:
// no retry loop, and every acquired stage is released in reverse order.
template<class Backend> class OwnedController {
public:
    explicit OwnedController(Backend &backend) : backend_(backend) {}
    ~OwnedController() { Stop(); }
    OwnedController(const OwnedController &) = delete;
    OwnedController &operator=(const OwnedController &) = delete;
    std::uint32_t Start() {
        if (failed_ || open_) return 1;
        auto rc = backend_.Open(); if (rc) return Fail(rc);
        open_ = true;
        rc = backend_.AttachSession(); if (rc) return Fail(rc);
        session_ = true;
        rc = backend_.AttachDevice(); if (rc) return Fail(rc);
        device_ = true;
        rc = Write(0); if (rc) return Fail(rc);
        last_ = 0; return 0;
    }
    std::uint32_t Update(std::uint32_t remote) {
        if (!device_ || failed_) return 1;
        const auto buttons = MapButtons(remote);
        if (buttons == last_) return 0;
        const auto rc = Write(buttons);
        if (rc) return Fail(rc);
        last_ = buttons; return 0;
    }
    std::uint32_t Stop() {
        std::uint32_t first = 0;
        const auto check = [&](std::uint32_t rc) { if (rc && !first) first = rc; };
        if (device_) { check(Write(0)); check(backend_.DetachDevice()); device_ = false; }
        if (session_) { check(backend_.ReleaseSession()); session_ = false; }
        if (open_) { backend_.Close(); open_ = false; }
        last_ = 0;
        if (first) { failed_ = true; if (!cleanup_error_) cleanup_error_ = first; }
        return cleanup_error_;
    }
    bool Attached() const { return device_; }
    // Last successful Set reply, NOT proof of visible navigation or of the
    // actual pad state after a failed cleanup. Keep a nonzero ACK on that failure.
    std::uint64_t LastAcknowledgedButtons() const { return acknowledged_; }
    std::uint64_t SuccessfulWrites() const { return writes_; }
    std::uint32_t CleanupError() const { return cleanup_error_; }
private:
    std::uint32_t Write(std::uint64_t buttons) {
        const auto rc = backend_.Set(buttons);
        if (!rc) { acknowledged_ = buttons; ++writes_; }
        return rc;
    }
    std::uint32_t Fail(std::uint32_t rc) { failed_ = true; Stop(); return rc; }
    Backend &backend_;
    bool open_{}, session_{}, device_{}, failed_{};
    std::uint64_t last_{}, acknowledged_{}, writes_{};
    std::uint32_t cleanup_error_{};
};
// Operator-only neutral attach/detach, independent of Bluetooth and configuration.
// A successful result proves service replies only, never system navigation.
template<class Controller> std::uint32_t NeutralControllerCheck(Controller &controller) {
    const auto rc = controller.Start();
    const auto cleanup = controller.Stop();
    return rc ? rc : cleanup;
}
}
