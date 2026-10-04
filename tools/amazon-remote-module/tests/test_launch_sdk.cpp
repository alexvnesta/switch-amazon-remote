// SPDX-License-Identifier: GPL-2.0-only
// Actual adapter through fake public SDK; not a Switch ABI/permission test.
#include "../launch_sdk.hpp"
#include <cassert>
#include <filesystem>
#include <string>
namespace {
std::uint32_t query_init{}, query_result{}, shell_init{}, launch_result{};
unsigned query_init_calls{}, query_calls{}, query_exits{}, shell_init_calls{}, launch_calls{}, shell_exits{};
std::size_t hashed_bytes{};
auto output_hash = armodule::LaunchModuleHash;
}
u64 armGetSystemTick() { return 100; }
u64 armTicksToNs(u64 tick) { return tick*1000000; }
void sha256ContextCreate(Sha256Context *) { hashed_bytes = 0; }
void sha256ContextUpdate(Sha256Context *, const void *, std::size_t size) { hashed_bytes += size; }
void sha256ContextGetHash(Sha256Context *, void *out) { std::memcpy(out,output_hash.data(),output_hash.size()); }
Result pmdmntInitialize() { ++query_init_calls; return query_init; }
Result pmdmntGetProcessId(u64 *pid, u64 id) { assert(id == armodule::ModuleProgramId); ++query_calls; *pid = query_result ? 0 : 321; return query_result; }
void pmdmntExit() { ++query_exits; }
Result pmshellInitialize() { ++shell_init_calls; return shell_init; }
Result pmshellLaunchProgram(unsigned flags, const NcmProgramLocation *location, u64 *pid) {
    assert(!flags && location->program_id == armodule::ModuleProgramId && !location->storageID);
    for (auto byte : location->reserved) assert(!byte);
    ++launch_calls; *pid = launch_result ? 0 : 456; return launch_result;
}
void pmshellExit() { ++shell_exits; }
int main() {
    using namespace armodule;
    LaunchSdk sdk; assert(sdk.Now() == 100);
    std::uint64_t pid{}; assert(!sdk.QueryProcess(pid) && pid == 321 && query_init_calls == 1 && query_calls == 1 && query_exits == 1);
    query_result = ProcessNotFoundResult; assert(sdk.QueryProcess(pid) == ProcessNotFoundResult && !pid && query_exits == 2);
    query_init = 77; assert(sdk.QueryProcess(pid) == 77 && query_calls == 2 && query_exits == 2);
    assert(!sdk.InitializeShell() && shell_init_calls == 1);
    assert(!sdk.LaunchDiagnosticModule(pid) && pid == 456 && launch_calls == 1);
    launch_result = 99; assert(sdk.LaunchDiagnosticModule(pid) == 99 && !pid && launch_calls == 2);
    sdk.CloseShell(); assert(shell_exits == 1);
    // SDK SHA implementation is not mocked into a security proof. Here we test
    // the real adapter's bounded file reading/exact-size/digest admission only.
    char directory[] = "/tmp/amazon-launch-sdk-XXXXXX"; assert(::mkdtemp(directory));
    const auto original = std::filesystem::current_path(); std::filesystem::current_path(directory);
    std::filesystem::create_directories("sdmc:/atmosphere/contents/0100000000a4d001");
    std::filesystem::create_directories("sdmc:/config/amazon-remote-module");
    constexpr char module[] = "sdmc:/atmosphere/contents/0100000000a4d001/exefs.nsp";
    std::array<std::uint8_t,32> hash{};
    assert(!sdk.VerifyModule(hash));
    FILE *file = std::fopen(module,"wb"); assert(file);
    for (unsigned i = 0; i < 142150; ++i) assert(std::fputc(0,file) == 0);
    assert(!std::fclose(file));
    assert(sdk.VerifyModule(hash) && hash == LaunchModuleHash && hashed_bytes == 142150);
    output_hash[0] ^= 1; assert(!sdk.VerifyModule(hash)); output_hash = LaunchModuleHash;
    file = std::fopen(module,"ab"); assert(file); std::fputc(0,file); std::fclose(file);
    assert(!sdk.VerifyModule(hash));
    LaunchRecord record{}; record.stage = unsigned(LaunchStage::Intent); record.issued_ms = 100;
    record.before_query_result = ProcessNotFoundResult; record.hash_verified = 1; record.module_sha256 = LaunchModuleHash;
    assert(sdk.CreateIntent(record)); assert(!sdk.CreateIntent(record));
    LaunchRecord saved{}; assert(ReadExact(LaunchPath,saved) && !std::memcmp(&saved,&record,sizeof record));
    record.stage = unsigned(LaunchStage::Returned); record.returned_ms = 101; record.launch_result = 99;
    assert(sdk.UpdateReceipt(record) && ReadExact(LaunchPath,saved) && saved.launch_result == 99);
    file = std::fopen(LaunchTempPath,"wb"); assert(file); std::fputc(42,file); std::fclose(file);
    record.launch_result = 88; assert(!sdk.UpdateReceipt(record));
    assert(ReadExact(LaunchPath,saved) && saved.launch_result == 99);
    std::filesystem::current_path(original); std::filesystem::remove_all(directory);
    std::puts("PASS actual launch adapter: fixed program/location/zero flags, getter/shell Results, exact-size/digest admission, exclusive intent/readback, preserved second attempt/temp and receipt updates. NOT hardware proof.");
}
