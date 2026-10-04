// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <type_traits>

namespace armodule {
inline constexpr std::uint32_t Magic = 0x41524d31, Version = 2;
inline constexpr char Directory[] = "sdmc:/config/amazon-remote-module";
inline constexpr char CommandPath[] = "sdmc:/config/amazon-remote-module/command.bin";
inline constexpr char StatusPath[] = "sdmc:/config/amazon-remote-module/status.bin";
enum class CommandKind : std::uint32_t { Arm = 1, Link = 2, Stop = 3, CheckController = 4 };
enum class ControllerCheck : std::uint32_t { NotRun, Passed, Failed };
enum class State : std::uint32_t { Idle, Scanning, Linking, Ready, Stopped, Suspended };
struct Command {
    std::uint32_t magic{Magic}, version{Version}, kind{}, reserved{};
    std::uint64_t instance{}, issued_ms{}, sequence{};
};
struct Status {
    std::uint32_t magic{Magic}, version{Version}, state{}, result{};
    std::uint64_t instance{}, now_ms{}, last_sequence{}, capture_deadline{};
    std::uint32_t link_ready{}, buttons{}, output_attached{}, reserved{};
    std::uint32_t controller_check{}, pad_cleanup_result{};
    std::uint64_t acknowledged_buttons{}, successful_writes{};
};
static_assert(sizeof(Command) == 40 && sizeof(Status) == 88);
static_assert(std::is_trivially_copyable_v<Command> && std::is_trivially_copyable_v<Status>);
inline bool ValidCommand(const Command &c, std::uint64_t instance,
        std::uint64_t last_sequence, std::uint64_t now) {
    return c.magic == Magic && c.version == Version && !c.reserved && instance && c.instance == instance &&
        c.kind >= unsigned(CommandKind::Arm) && c.kind <= unsigned(CommandKind::CheckController) &&
        c.sequence && c.sequence > last_sequence && c.issued_ms && now >= c.issued_ms && now-c.issued_ms <= 2000;
}
inline bool FreshStatus(const Status &s, std::uint64_t now) {
    return s.magic == Magic && s.version == Version && !s.reserved && s.instance && s.now_ms &&
        s.state <= unsigned(State::Suspended) && s.link_ready <= 1 && s.output_attached <= 1 &&
        s.controller_check <= unsigned(ControllerCheck::Failed) &&
        now >= s.now_ms && now-s.now_ms <= 2000;
}
// One controlling thread; permission is consumed even if transport setup fails.
// No boot-time connection and no automatic reconnect after suspend/error/stop.
class SessionPolicy {
public:
    bool CheckController() {
        if (state_ != State::Idle || consumed_ || check_consumed_) return false;
        check_consumed_ = true; return true;
    }
    bool Arm() {
        if (state_ != State::Idle || consumed_) return false;
        consumed_ = true; state_ = State::Scanning; return true;
    }
    bool Link(bool fresh) {
        if (state_ != State::Scanning || !fresh) return false;
        state_ = State::Linking; return true;
    }
    bool Ready() {
        if (state_ != State::Linking) return false;
        state_ = State::Ready; return true;
    }
    void Stop() { state_ = State::Stopped; consumed_ = true; }
    void Suspend() { state_ = State::Suspended; consumed_ = true; }
    void Resume() { if (state_ == State::Suspended) state_ = State::Stopped; }
    State Get() const { return state_; }
    bool Active() const { return state_ == State::Scanning || state_ == State::Linking || state_ == State::Ready; }
private:
    State state_{State::Idle}; bool consumed_{}, check_consumed_{};
};
struct PowerDecision { bool valid{}, suspend{}, resume{}, shutdown{}; };
// Public libnx PscPmState numbers. ReadyAwaken restores idle polling, never BLE
// permissions. Critical-awaken is acknowledged without any transport action.
constexpr PowerDecision ClassifyPower(std::uint32_t state) {
    switch (state) {
        case 0: case 1: return {true,false,true,false};
        case 2: case 3: return {true,true,false,false};
        case 4: return {true,false,false,false};
        case 5: return {true,true,false,true};
        default: return {};
    }
}
}
