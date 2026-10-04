// SPDX-License-Identifier: GPL-2.0-only
#include "../startup_storage.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
namespace {
bool exists; s64 file_size; Result open_error, create_error, size_error, write_error;
unsigned opens, creates, writes, closes;
std::vector<unsigned char> stored;
void Reset() { exists = false; file_size = 0; open_error = create_error = size_error = write_error = 0;
    opens = creates = writes = closes = 0; stored.clear(); }
}
Result fsFsOpenFile(FsFileSystem *, const char *path, unsigned mode, FsFile *) {
    assert(!std::strcmp(path, armodule::StartupFsPath) && mode == FsOpenMode_Write); ++opens;
    return open_error ? open_error : exists ? 0 : 0x202;
}
Result fsFsCreateFile(FsFileSystem *, const char *path, s64 size, unsigned option) {
    assert(!std::strcmp(path, armodule::StartupFsPath) && size == sizeof(armodule::StartupRecord) && !option); ++creates;
    if (create_error) return create_error;
    assert(!exists); exists = true; file_size = size; return 0;
}
Result fsFileGetSize(FsFile *, s64 *out) { if (!size_error) *out = file_size; return size_error; }
Result fsFileWrite(FsFile *, s64 offset, const void *data, std::uint64_t size, unsigned option) {
    assert(!offset && size == sizeof(armodule::StartupRecord) && option == FsWriteOption_Flush); ++writes;
    if (!write_error) stored.assign(static_cast<const unsigned char *>(data), static_cast<const unsigned char *>(data)+size);
    return write_error;
}
void fsFileClose(FsFile *) { ++closes; }
int main() {
    using namespace armodule;
    StartupRecord record{}; record.instance = 42; record.now_ms = 10; record.stage = unsigned(StartupStage::Mount);
    record.checksum = StartupChecksum(record); assert(ValidStartup(record));
    for (unsigned i = 0; i < sizeof record; ++i) {
        auto bad = record; reinterpret_cast<unsigned char *>(&bad)[i] ^= 1; assert(!ValidStartup(bad));
    }
    for (unsigned stage = 0; stage <= unsigned(StartupStage::Exit)+1; ++stage) {
        auto value = record; value.stage = stage; value.checksum = StartupChecksum(value);
        assert(ValidStartup(value) == (stage >= 1 && stage <= unsigned(StartupStage::Exit)));
    }
    FsFileSystem fs{};
    Reset(); assert(!WriteStartup(fs,record)); assert(opens == 2 && creates == 1 && writes == 1 && closes == 1);
    StartupRecord actual{}; std::memcpy(&actual, stored.data(), sizeof actual); assert(ValidStartup(actual));
    assert(!WriteStartup(fs,record)); assert(creates == 1 && writes == 2 && closes == 2);
    Reset(); open_error = 9; assert(WriteStartup(fs,record) == 9 && !creates && !closes && !writes);
    Reset(); create_error = 10; assert(WriteStartup(fs,record) == 10 && opens == 1 && !closes && !writes);
    Reset(); exists = true; file_size = 999; assert(WriteStartup(fs,record) == 0x402 && closes == 1 && !writes && file_size == 999);
    Reset(); exists = true; size_error = 11; assert(WriteStartup(fs,record) == 11 && closes == 1 && !writes);
    Reset(); exists = true; file_size = sizeof record; write_error = 12;
    assert(WriteStartup(fs,record) == 12 && closes == 1 && writes == 1 && stored.empty());
    std::puts("PASS startup checkpoint wire corruption, fixed raw-FS path/size/flush, permission/error preservation and handle cleanup; not device proof");
}
