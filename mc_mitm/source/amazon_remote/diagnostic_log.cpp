// Original independent diagnostic helpers. GPL-2.0-only.
#include "diagnostic_log.hpp"
#include "remote_decoder.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace amazon_remote {
namespace {
struct ButtonName { std::uint32_t bit; const char *name; };
constexpr ButtonName Buttons[]{
    {Up,"Up"}, {Down,"Down"}, {Left,"Left"}, {Right,"Right"}, {Confirm,"Confirm"},
    {Back,"Back"}, {Home,"Home"}, {Menu,"Menu"}, {PlayPause,"PlayPause"},
    {Rewind,"Rewind"}, {FastForward,"FastForward"}, {Voice,"Voice"},
    {App1,"App1"}, {App2,"App2"}, {App3,"App3"}, {App4,"App4"}
};
bool Append(char *out, std::size_t capacity, const char *text) {
    const auto used = std::strlen(out), length = std::strlen(text);
    if (used >= capacity || length >= capacity - used) return false;
    std::memcpy(out + used, text, length + 1); return true;
}
bool RemoveOptional(const char *path) { return unlink(path) == 0 || errno == ENOENT; }
bool RenameOptional(const char *from, const char *to) { return std::rename(from, to) == 0 || errno == ENOENT; }
}
bool FormatHex(char *out, std::size_t capacity, const std::uint8_t *bytes, std::size_t size) {
    if (!out || !capacity) return false;
    out[0] = 0;
    if (size > 32 || (size && !bytes) || size * 2 >= capacity) return false;
    constexpr char Hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < size; ++i) { out[2*i] = Hex[bytes[i] >> 4]; out[2*i+1] = Hex[bytes[i] & 15]; }
    out[size * 2] = 0; return true;
}
bool FormatButtons(char *out, std::size_t capacity, std::uint32_t before, std::uint32_t after) {
    if (!out || !capacity) return false;
    out[0] = 0;
    bool ok = Append(out, capacity, "pressed="); bool first = true;
    for (const auto &button : Buttons) if ((after & ~before) & button.bit) {
        if (!first) ok &= Append(out, capacity, ",");
        ok &= Append(out, capacity, button.name); first = false;
    }
    if (first) ok &= Append(out, capacity, "none");
    ok &= Append(out, capacity, " released="); first = true;
    for (const auto &button : Buttons) if ((before & ~after) & button.bit) {
        if (!first) ok &= Append(out, capacity, ",");
        ok &= Append(out, capacity, button.name); first = false;
    }
    if (first) ok &= Append(out, capacity, "none");
    return ok;
}
bool FormatLogLine(char *out, std::size_t capacity, std::uint64_t start, std::uint64_t now, const char *message) {
    if (!out || !capacity) return false;
    out[0] = 0; if (!message) return false;
    const auto written = std::snprintf(out, capacity, "[session=%llu boot=%llums +%llums] %s",
        static_cast<unsigned long long>(start), static_cast<unsigned long long>(now),
        static_cast<unsigned long long>(now >= start ? now - start : 0), message);
    return written >= 0 && static_cast<std::size_t>(written) < capacity;
}
void RotatingLog::Close() { if (file_) std::fclose(file_); file_ = nullptr; size_ = 0; }
bool FormatApiResult(char *out, std::size_t capacity, const char *api,
                     std::uint32_t result, unsigned phase, unsigned error) {
    if (!out || !capacity) return false;
    out[0] = 0; if (!api) return false;
    const auto written = std::snprintf(out, capacity,
        "API=%.80s Result=%08lx module=%u description=%u phase=%u error=%u (before cleanup)",
        api, static_cast<unsigned long>(result), unsigned(result & 0x1ff),
        unsigned((result >> 9) & 0x1fff), phase, error);
    return written >= 0 && static_cast<std::size_t>(written) < capacity;
}
bool RotatingLog::Open(const char *path) {
    Close();
    if (!path || !*path || std::strlen(path) >= sizeof path_ - 3) return false;
    std::strcpy(path_, path);
    // Old unbounded builds may have left oversized logs. Reset only these exact
    // three log slots, rather than silently retaining unbounded old history.
    for (unsigned slot = 0; slot <= BackupCount; ++slot) {
        char name[sizeof path_ + 3];
        if (slot) {
            const auto length = std::snprintf(name, sizeof name, "%s.%u", path_, slot);
            if (length < 0 || static_cast<std::size_t>(length) >= sizeof name) return false;
        }
        else std::strcpy(name, path_);
        struct stat info{};
        if (stat(name, &info) != 0) { if (errno != ENOENT) return false; continue; }
        if (!S_ISREG(info.st_mode)) return false;
        if (info.st_size < 0 || static_cast<std::uint64_t>(info.st_size) > FileLimit) {
            FILE *reset = std::fopen(name, "w"); if (!reset) return false;
            if (std::fclose(reset) != 0) return false;
        }
    }
    file_ = std::fopen(path_, "a");
    if (!file_) return false;
    if (std::fseek(file_, 0, SEEK_END) != 0) { Close(); return false; }
    const auto size = std::ftell(file_); if (size < 0) { Close(); return false; }
    size_ = static_cast<std::size_t>(size); return true;
}
bool RotatingLog::Rotate() {
    Close(); char first[sizeof path_ + 3], second[sizeof path_ + 3];
    std::snprintf(first, sizeof first, "%s.1", path_); std::snprintf(second, sizeof second, "%s.2", path_);
    if (!RemoveOptional(second) || !RenameOptional(first, second) || !RenameOptional(path_, first)) return false;
    file_ = std::fopen(path_, "w"); return file_ != nullptr;
}
bool RotatingLog::Write(const char *line) {
    if (!file_ || !line) return false;
    char bounded[LineLimit];
    auto length = std::strlen(line);
    if (length >= sizeof bounded) {
        constexpr char suffix[] = "...[truncated]";
        length = sizeof bounded - 1;
        const auto prefix = length - (sizeof suffix - 1);
        std::memcpy(bounded, line, prefix); std::memcpy(bounded + prefix, suffix, sizeof suffix - 1);
    } else std::memcpy(bounded, line, length);
    // Ensure one physical line per event, even if a future message embeds LF/CR.
    for (std::size_t i = 0; i < length; ++i) if (bounded[i] == '\n' || bounded[i] == '\r') bounded[i] = ' ';
    bounded[length] = '\n';
    if (size_ + length + 1 > FileLimit && !Rotate()) return false;
    if (std::fwrite(bounded, 1, length + 1, file_) != length + 1 || std::fflush(file_) != 0) { Close(); return false; }
    size_ += length + 1; return true;
}
}
