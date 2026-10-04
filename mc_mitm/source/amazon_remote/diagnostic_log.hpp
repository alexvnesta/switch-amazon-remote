// Bounded diagnostic formatting/storage, independent of Switch APIs. GPL-2.0-only.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace amazon_remote {
// False means truncation/invalid arguments; buffers are always terminated when
// capacity is nonzero. Hex consumes at most 32 bytes per chunk.
bool FormatHex(char *out, std::size_t capacity, const std::uint8_t *bytes, std::size_t size);
bool FormatButtons(char *out, std::size_t capacity, std::uint32_t before, std::uint32_t after);
bool FormatLogLine(char *out, std::size_t capacity, std::uint64_t session_start_ms,
                   std::uint64_t now_ms, const char *message);
// Public Switch Result packing; this deliberately assigns no symbolic meaning
// to unknown service descriptions. API labels are bounded to 80 characters.
bool FormatApiResult(char *out, std::size_t capacity, const char *api,
                     std::uint32_t result, unsigned phase, unsigned error);
class RotatingLog {
public:
    static constexpr std::size_t FileLimit = 256 * 1024;
    static constexpr std::size_t LineLimit = 512; // Includes the newline.
    static constexpr unsigned BackupCount = 2;
    ~RotatingLog() { Close(); }
    RotatingLog() = default;
    RotatingLog(const RotatingLog &) = delete;
    RotatingLog &operator=(const RotatingLog &) = delete;
    bool Open(const char *path);
    bool Write(const char *line);
    void Close();
    bool Active() const { return file_ != nullptr; }
private:
    bool Rotate();
    char path_[384]{};
    FILE *file_{};
    std::size_t size_{};
};
}
