#include "../mc_mitm/source/amazon_remote/diagnostic_log.hpp"
#include "../mc_mitm/source/amazon_remote/remote_decoder.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace amazon_remote;
std::string Read(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
std::size_t Size(const std::string &path) { struct stat info{}; assert(stat(path.c_str(), &info) == 0); return info.st_size; }
int main() {
    char text[512]{}; const std::uint8_t bytes[]{0, 0xab, 0xff};
    assert(FormatHex(text, sizeof text, bytes, sizeof bytes)); assert(std::strcmp(text, "00abff") == 0);
    assert(FormatHex(text, 1, nullptr, 0)); assert(!text[0]);
    assert(!FormatHex(text, sizeof text, nullptr, 1)); assert(!text[0]);
    std::array<std::uint8_t, 33> large{};
    assert(!FormatHex(text, sizeof text, large.data(), large.size()));
    assert(!FormatHex(text, 6, bytes, 3)); assert(!text[0]);
    assert(FormatButtons(text, sizeof text, Up | Voice, Confirm | PlayPause | Rewind | FastForward));
    assert(std::strcmp(text, "pressed=Confirm,PlayPause,Rewind,FastForward released=Up,Voice") == 0);
    assert(FormatButtons(text, sizeof text, 0, Voice)); assert(std::strcmp(text, "pressed=Voice released=none") == 0);
    assert(FormatButtons(text, sizeof text, Voice, 0)); assert(std::strcmp(text, "pressed=none released=Voice") == 0);
    assert(FormatButtons(text, sizeof text, Up, Up)); assert(std::strcmp(text, "pressed=none released=none") == 0);
    assert(!FormatButtons(text, 3, 0, Up)); assert(text[2] == '\0');
    assert(FormatLogLine(text, sizeof text, 123, 456, "input"));
    assert(std::strcmp(text, "[session=123 boot=456ms +333ms] input") == 0);
    assert(FormatLogLine(text, sizeof text, 456, 123, "clock")); assert(std::strstr(text, "+0ms"));
    assert(!FormatLogLine(text, 1, 0, 0, "input")); assert(!text[0]);
    assert(!FormatLogLine(nullptr, 1, 0, 0, "input"));
    assert(!FormatLogLine(text, sizeof text, 0, 0, nullptr));
    assert(FormatApiResult(text, sizeof text, "btdevInitialize", 0x57a8f, 3, 7));
    assert(std::strcmp(text, "API=btdevInitialize Result=00057a8f module=143 description=701 phase=3 error=7 (before cleanup)") == 0);
    assert(FormatApiResult(text, sizeof text, "success", 0, 0, 0));
    assert(std::strstr(text, "module=0 description=0"));
    assert(FormatApiResult(text, sizeof text, "reserved", 0xffffffff, 0, 0));
    assert(std::strstr(text, "module=511 description=8191"));
    const std::string long_api(1024, 'x');
    assert(FormatApiResult(text, sizeof text, long_api.c_str(), 0, 0, 0));
    assert(std::strlen(text) < 200);
    assert(!FormatApiResult(text, 2, "api", 1, 0, 0)); assert(text[1] == '\0');
    assert(!FormatApiResult(nullptr, 1, "api", 0, 0, 0));
    assert(!FormatApiResult(text, sizeof text, nullptr, 0, 0, 0)); assert(!text[0]);

    char temporary[] = "/tmp/amazon-remote-log-test-XXXXXX";
    const auto directory = mkdtemp(temporary); assert(directory);
    const std::string path = std::string(directory) + "/probe.log";
    RotatingLog log; assert(!log.Open(nullptr)); assert(!log.Open(""));
    const std::string oversized_path(400, 'a'); assert(!log.Open(oversized_path.c_str()));
    assert(log.Open(path.c_str())); assert(log.Write("one\ntwo\rthree"));
    assert(Read(path) == "one two three\n");
    const std::string long_line(1024, 'x'); assert(log.Write(long_line.c_str()));
    auto data = Read(path); assert(data.find("...[truncated]\n") != std::string::npos);
    const std::string line(511, 'y');
    for (unsigned i = 0; i < 2100; ++i) assert(log.Write(line.c_str()));
    log.Close(); assert(!log.Write("closed"));
    assert(Size(path) <= RotatingLog::FileLimit);
    assert(Size(path + ".1") <= RotatingLog::FileLimit);
    assert(Size(path + ".2") <= RotatingLog::FileLimit);
    assert(access((path + ".3").c_str(), F_OK) != 0);
    assert(log.Open(path.c_str())); assert(log.Write("another session")); log.Close();
    assert(Read(path).find("another session\n") != std::string::npos);
    // Old builds' oversized slots cannot violate the storage cap after Open.
    for (const auto &slot : {path, path + ".1", path + ".2"}) {
        FILE *file = std::fopen(slot.c_str(), "w"); assert(file);
        const std::string over(RotatingLog::FileLimit + 1, 'z');
        assert(std::fwrite(over.data(), 1, over.size(), file) == over.size()); assert(std::fclose(file) == 0);
    }
    assert(log.Open(path.c_str())); log.Close();
    assert(Size(path) == 0 && Size(path + ".1") == 0 && Size(path + ".2") == 0);
    // Non-file slots and rotation failures disable persistent logging safely.
    assert(std::remove((path + ".2").c_str()) == 0);
    assert(mkdir((path + ".2").c_str(), 0700) == 0);
    assert(!log.Open(path.c_str())); assert(!log.Active());
    assert(rmdir((path + ".2").c_str()) == 0);
    assert(log.Open(path.c_str())); assert(mkdir((path + ".2").c_str(), 0700) == 0);
    for (unsigned i = 0; i < RotatingLog::FileLimit / RotatingLog::LineLimit; ++i) assert(log.Write(line.c_str()));
    assert(!log.Write(line.c_str())); assert(!log.Active());
    assert(std::remove(path.c_str()) == 0); assert(std::remove((path + ".1").c_str()) == 0);
    assert(rmdir((path + ".2").c_str()) == 0); assert(rmdir(directory) == 0);
    std::cout << "PASS timestamp/hex/press-release formatting, truncation, 768KiB log cap, rotation, reopen and storage failures\n";
}
