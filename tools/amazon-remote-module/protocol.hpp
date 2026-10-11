// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#if defined(ARMODULE_AUTOMATIC_A2) && !defined(ARMODULE_AUTOMATIC_A1)
#error A2 is a storage repair of the A1 automatic variant
#endif
#if defined(ARMODULE_AUTOMATIC_A1) && (!defined(ARMODULE_NATIVE_HCI_C2) || !defined(ARMODULE_RECONNECT_CHECK))
#error Automatic A1 requires the saved-only C2 companion contract
#endif
#if defined(ARMODULE_DISCONNECT_CHECK) && !defined(ARMODULE_STARTUP_TRACE)
#error Disconnect check requires the reviewed startup/scan path
#endif
#if defined(ARMODULE_DISCOVERY_TRACE) && !defined(ARMODULE_RECONNECT_CHECK)
#error Discovery trace requires reconnect-only policy
#endif
#if defined(ARMODULE_RECONNECT_CHECK) && (!defined(ARMODULE_NAV17) || defined(ARMODULE_NATIVE_OWNER))
#error Reconnect check requires Navigation17 without native bond request support
#endif
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include "navigation15_namespace.hpp"

namespace armodule {
inline constexpr std::uint32_t Magic = 0x41524d31;
#ifdef ARMODULE_NAV15
inline constexpr std::uint32_t Version = 3;
inline constexpr char Directory[] = ARMODULE_NAV15_CONFIG;
inline constexpr char CommandPath[] = ARMODULE_NAV15_CONFIG "/command.bin";
inline constexpr char StatusPath[] = ARMODULE_NAV15_CONFIG "/status.bin";
inline constexpr char CommandTemp[] = ARMODULE_NAV15_CONFIG "/command.tmp";
inline constexpr char StatusTemp[] = ARMODULE_NAV15_CONFIG "/status.tmp";
inline constexpr char EnablePath[] = ARMODULE_NAV15_CONFIG "/enable.txt";
inline constexpr char LogPath[] = ARMODULE_NAV15_CONFIG "/module.log";
inline constexpr char HdlsPath[] = ARMODULE_NAV15_CONFIG "/hdls-backend.bin";
inline constexpr char HdlsTemp[] = ARMODULE_NAV15_CONFIG "/hdls-backend.tmp";
inline constexpr char ControlPath[] = ARMODULE_NAV15_CONFIG "/control.bin";
inline constexpr char ControlTemp[] = ARMODULE_NAV15_CONFIG "/control.tmp";
inline constexpr char StatusFsPath[]=ARMODULE_NAV15_CONFIG_FS "/status.bin";
inline constexpr char EnrollmentIntentPath[]=ARMODULE_NAV15_CONFIG "/enrollment-intent.bin";
inline constexpr char EnrollmentPath[]=ARMODULE_NAV15_CONFIG "/enrollment.bin";
inline constexpr char EnrollmentTemp[]=ARMODULE_NAV15_CONFIG "/enrollment.tmp";
#ifdef ARMODULE_NAV16
inline constexpr char EnrollmentContextPath[]=ARMODULE_NAV15_CONFIG "/enrollment-context.bin";
#endif
#elif defined(ARMODULE_NAV14)
inline constexpr std::uint32_t Version = 3;
inline constexpr char Directory[] = "sdmc:/config/amazon-remote-nav14";
inline constexpr char CommandPath[] = "sdmc:/config/amazon-remote-nav14/command.bin";
inline constexpr char StatusPath[] = "sdmc:/config/amazon-remote-nav14/status.bin";
inline constexpr char CommandTemp[] = "sdmc:/config/amazon-remote-nav14/command.tmp";
inline constexpr char StatusTemp[] = "sdmc:/config/amazon-remote-nav14/status.tmp";
inline constexpr char EnablePath[] = "sdmc:/config/amazon-remote-nav14/enable.txt";
inline constexpr char LogPath[] = "sdmc:/config/amazon-remote-nav14/module.log";
inline constexpr char HdlsPath[] = "sdmc:/config/amazon-remote-nav14/hdls-backend.bin";
inline constexpr char HdlsTemp[] = "sdmc:/config/amazon-remote-nav14/hdls-backend.tmp";
inline constexpr char ControlPath[] = "sdmc:/config/amazon-remote-nav14/control.bin";
inline constexpr char ControlTemp[] = "sdmc:/config/amazon-remote-nav14/control.tmp";
inline constexpr char StatusFsPath[]="/config/amazon-remote-nav14/status.bin";
#elif defined(ARMODULE_NAV13)
inline constexpr std::uint32_t Version = 3;
inline constexpr char Directory[] = "sdmc:/config/amazon-remote-nav13";
inline constexpr char CommandPath[] = "sdmc:/config/amazon-remote-nav13/command.bin";
inline constexpr char StatusPath[] = "sdmc:/config/amazon-remote-nav13/status.bin";
inline constexpr char CommandTemp[] = "sdmc:/config/amazon-remote-nav13/command.tmp";
inline constexpr char StatusTemp[] = "sdmc:/config/amazon-remote-nav13/status.tmp";
inline constexpr char EnablePath[] = "sdmc:/config/amazon-remote-nav13/enable.txt";
inline constexpr char LogPath[] = "sdmc:/config/amazon-remote-nav13/module.log";
inline constexpr char HdlsPath[] = "sdmc:/config/amazon-remote-nav13/hdls-backend.bin";
inline constexpr char HdlsTemp[] = "sdmc:/config/amazon-remote-nav13/hdls-backend.tmp";
inline constexpr char ControlPath[] = "sdmc:/config/amazon-remote-nav13/control.bin";
inline constexpr char ControlTemp[] = "sdmc:/config/amazon-remote-nav13/control.tmp";
inline constexpr char StatusFsPath[]="/config/amazon-remote-nav13/status.bin";
#elif defined(ARMODULE_NAV12)
inline constexpr std::uint32_t Version = 3;
inline constexpr char Directory[] = "sdmc:/config/amazon-remote-nav12";
inline constexpr char CommandPath[] = "sdmc:/config/amazon-remote-nav12/command.bin";
inline constexpr char StatusPath[] = "sdmc:/config/amazon-remote-nav12/status.bin";
inline constexpr char CommandTemp[] = "sdmc:/config/amazon-remote-nav12/command.tmp";
inline constexpr char StatusTemp[] = "sdmc:/config/amazon-remote-nav12/status.tmp";
inline constexpr char EnablePath[] = "sdmc:/config/amazon-remote-nav12/enable.txt";
inline constexpr char LogPath[] = "sdmc:/config/amazon-remote-nav12/module.log";
inline constexpr char HdlsPath[] = "sdmc:/config/amazon-remote-nav12/hdls-backend.bin";
inline constexpr char HdlsTemp[] = "sdmc:/config/amazon-remote-nav12/hdls-backend.tmp";
inline constexpr char ControlPath[] = "sdmc:/config/amazon-remote-nav12/control.bin";
inline constexpr char ControlTemp[] = "sdmc:/config/amazon-remote-nav12/control.tmp";
inline constexpr char StatusFsPath[]="/config/amazon-remote-nav12/status.bin";
#elif defined(ARMODULE_NAV11)
inline constexpr std::uint32_t Version = 3;
inline constexpr char Directory[] = "sdmc:/config/amazon-remote-nav11";
inline constexpr char CommandPath[] = "sdmc:/config/amazon-remote-nav11/command.bin";
inline constexpr char StatusPath[] = "sdmc:/config/amazon-remote-nav11/status.bin";
inline constexpr char CommandTemp[] = "sdmc:/config/amazon-remote-nav11/command.tmp";
inline constexpr char StatusTemp[] = "sdmc:/config/amazon-remote-nav11/status.tmp";
inline constexpr char EnablePath[] = "sdmc:/config/amazon-remote-nav11/enable.txt";
inline constexpr char LogPath[] = "sdmc:/config/amazon-remote-nav11/module.log";
inline constexpr char HdlsPath[] = "sdmc:/config/amazon-remote-nav11/hdls-backend.bin";
inline constexpr char HdlsTemp[] = "sdmc:/config/amazon-remote-nav11/hdls-backend.tmp";
inline constexpr char ControlPath[] = "sdmc:/config/amazon-remote-nav11/control.bin";
inline constexpr char ControlTemp[] = "sdmc:/config/amazon-remote-nav11/control.tmp";
inline constexpr char StatusFsPath[]="/config/amazon-remote-nav11/status.bin";
#elif defined(ARMODULE_NAV10)
inline constexpr std::uint32_t Version = 3;
inline constexpr char StatusFsPath[]="/config/amazon-remote-nav10/status.bin";
inline constexpr char Directory[] = "sdmc:/config/amazon-remote-nav10";
inline constexpr char CommandPath[] = "sdmc:/config/amazon-remote-nav10/command.bin";
inline constexpr char StatusPath[] = "sdmc:/config/amazon-remote-nav10/status.bin";
inline constexpr char CommandTemp[] = "sdmc:/config/amazon-remote-nav10/command.tmp";
inline constexpr char StatusTemp[] = "sdmc:/config/amazon-remote-nav10/status.tmp";
inline constexpr char EnablePath[] = "sdmc:/config/amazon-remote-nav10/enable.txt";
inline constexpr char LogPath[] = "sdmc:/config/amazon-remote-nav10/module.log";
inline constexpr char HdlsPath[] = "sdmc:/config/amazon-remote-nav10/hdls-backend.bin";
inline constexpr char HdlsTemp[] = "sdmc:/config/amazon-remote-nav10/hdls-backend.tmp";
inline constexpr char ControlPath[] = "sdmc:/config/amazon-remote-nav10/control.bin";
inline constexpr char ControlTemp[] = "sdmc:/config/amazon-remote-nav10/control.tmp";
#else
inline constexpr std::uint32_t Version = 2;
#ifdef ARMODULE_NAV9
inline constexpr char Directory[] = "sdmc:/config/amazon-remote-nav9";
inline constexpr char CommandPath[] = "sdmc:/config/amazon-remote-nav9/command.bin";
inline constexpr char StatusPath[] = "sdmc:/config/amazon-remote-nav9/status.bin";
inline constexpr char CommandTemp[] = "sdmc:/config/amazon-remote-nav9/command.tmp";
inline constexpr char StatusTemp[] = "sdmc:/config/amazon-remote-nav9/status.tmp";
inline constexpr char EnablePath[] = "sdmc:/config/amazon-remote-nav9/enable.txt";
inline constexpr char LogPath[] = "sdmc:/config/amazon-remote-nav9/module.log";
inline constexpr char HdlsPath[] = "sdmc:/config/amazon-remote-nav9/hdls-backend.bin";
inline constexpr char HdlsTemp[] = "sdmc:/config/amazon-remote-nav9/hdls-backend.tmp";
inline constexpr char ControlPath[] = "sdmc:/config/amazon-remote-nav9/control.bin";
inline constexpr char ControlTemp[] = "sdmc:/config/amazon-remote-nav9/control.tmp";
#else
inline constexpr char Directory[] = "sdmc:/config/amazon-remote-module";
inline constexpr char CommandPath[] = "sdmc:/config/amazon-remote-module/command.bin";
inline constexpr char StatusPath[] = "sdmc:/config/amazon-remote-module/status.bin";
inline constexpr char CommandTemp[] = "sdmc:/config/amazon-remote-module/command.tmp";
inline constexpr char StatusTemp[] = "sdmc:/config/amazon-remote-module/status.tmp";
inline constexpr char EnablePath[] = "sdmc:/config/amazon-remote-module/enable.txt";
inline constexpr char LogPath[] = "sdmc:/config/amazon-remote-module/module.log";
inline constexpr char HdlsPath[] = "sdmc:/config/amazon-remote-module/hdls-backend.bin";
inline constexpr char HdlsTemp[] = "sdmc:/config/amazon-remote-module/hdls-backend.tmp";
inline constexpr char ControlPath[] = "sdmc:/config/amazon-remote-module/control-diag1.bin";
inline constexpr char ControlTemp[] = "sdmc:/config/amazon-remote-module/control-diag1.tmp";
#endif
#endif
enum class CommandKind : std::uint32_t { Arm = 1, Link = 2, Stop = 3, CheckController = 4
#ifdef ARMODULE_NAV15
    , Enroll = 5
#endif
#ifdef ARMODULE_NATIVE_OWNER
    , NativeBond = 6
#endif
};
enum class ControllerCheck : std::uint32_t { NotRun, Passed, Failed };
// Appended without changing existing numeric values or wire layout. Older
// frontends reject the unknown state instead of acknowledging Stop early.
enum class State : std::uint32_t { Idle, Scanning, Linking, Ready, Stopped, Suspended, Stopping, Disconnected };
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
inline std::uint32_t StatusChecksum(const Status &s) {
    const auto *bytes=reinterpret_cast<const unsigned char *>(&s);std::uint32_t hash=2166136261u;
    for (std::size_t i=0;i<sizeof s;++i)
        if (i<offsetof(Status,reserved) || i>=offsetof(Status,reserved)+sizeof s.reserved) hash=(hash^bytes[i])*16777619u;
    return hash;
}
inline bool ValidCommand(const Command &c, std::uint64_t instance,
        std::uint64_t last_sequence, std::uint64_t now) {
    return c.magic == Magic && c.version == Version && !c.reserved && instance && c.instance == instance &&
        c.kind >= unsigned(CommandKind::Arm) && c.kind <=
#ifdef ARMODULE_AUTOMATIC_A1
        unsigned(CommandKind::Stop) && c.kind == unsigned(CommandKind::Stop) &&
#elif defined(ARMODULE_RECONNECT_CHECK)
        unsigned(CommandKind::CheckController) &&
#elif defined(ARMODULE_NATIVE_OWNER)
        unsigned(CommandKind::NativeBond) &&
#elif defined(ARMODULE_NAV15)
        unsigned(CommandKind::Enroll) &&
#else
        unsigned(CommandKind::CheckController) &&
#endif
        c.sequence && c.sequence > last_sequence && c.issued_ms && now >= c.issued_ms && now-c.issued_ms <= 2000;
}
inline bool FreshStatus(const Status &s, std::uint64_t now) {
    return s.magic == Magic && s.version == Version &&
        (Version==3?s.reserved==StatusChecksum(s):!s.reserved) && s.instance && s.now_ms &&
        s.state <= unsigned(State::Disconnected) && s.link_ready <= 1 && s.output_attached <= 1 &&
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
    // Presentation of confirmed owned-link absence, not permission to rearm.
    void StopDisconnected() { state_ = State::Disconnected; consumed_ = true; }
    bool BeginStop() {
        if (state_ == State::Stopped || state_ == State::Suspended || state_ == State::Stopping || state_ == State::Disconnected) return false;
        state_ = State::Stopping; consumed_ = true; return true;
    }
    void Suspend() { state_ = State::Suspended; consumed_ = true; }
    void Resume() { if (state_ == State::Suspended) state_ = State::Stopped; }
#ifdef ARMODULE_WAKE_A3
    bool ResumeVerifiedSleep() {
        if(state_!=State::Suspended)return false;
        state_=State::Idle;consumed_=false;return true;
    }
#endif
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
