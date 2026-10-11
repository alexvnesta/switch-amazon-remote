// SPDX-License-Identifier: GPL-2.0-only
#include <stratosphere.hpp>
#include <switch.h>
#include "filter_correction_runtime.hpp"
#include "manufacturer_filter_correction.hpp"
#include "managed_observer_runtime.hpp"
#include "capture_lease.hpp"
#include <cstring>
namespace ams::mc::amazon_filter_correction {
namespace {
constinit os::SdkMutex g_lock;
::amazon_remote::ManufacturerFilterCorrection g_policy;
bool g_opted_in{}; // Immutable after startup, before IPC producers launch.
bool g_storage_attempted{}; // Main timer only. Never retry/delete partials.
std::atomic<bool> g_suspended{};
std::uint64_t g_process{},g_add_ms{},g_delete_ms{};
std::uint32_t g_add_result{UINT32_MAX},g_delete_result{UINT32_MAX};
constexpr char ModePath[]="sdmc:/config/amazon-remote/filter-correction-mode.txt";
constexpr char ResultPath[]="sdmc:/config/amazon-remote/filter-correction-result.bin";
struct Record {
    std::uint32_t magic{0x41465231},version{1},state{},flags{};
    std::uint64_t owner_process{},add_boot_ms{},delete_boot_ms{};
    std::uint32_t add_result{UINT32_MAX},delete_result{UINT32_MAX},original_hash{},corrected_hash{};
    std::uint64_t owner_program{::amazon_remote::FilterBtmProgram};
    std::uint32_t reserved{},checksum{};
};
static_assert(sizeof(Record)==72 && offsetof(Record,checksum)==68);
static_assert(sizeof(BtdrvBleAdvertiseFilter)==62);
static_assert(offsetof(BtdrvBleAdvertiseFilter,index)==0);
static_assert(offsetof(BtdrvBleAdvertiseFilter,adv)==1);
static_assert(offsetof(BtdrvBleAdvertiseFilter,mask)==32);
static_assert(offsetof(BtdrvBleAdvertiseFilter,mask_size)==61);
std::uint32_t Fnv(const void *data,std::size_t size) {
    std::uint32_t h=2166136261u;const auto *p=static_cast<const std::uint8_t *>(data);
    for(std::size_t i=0;i<size;++i) h=(h^p[i])*16777619u;
    return h;
}
bool Live() {
    ::amazon_remote::CaptureLeaseWire lease{};
    if(!amazon_observer::ReadCaptureStatus(&lease,sizeof lease))return false;
    const auto now=armTicksToNs(armGetSystemTick())/1000000;
    return !g_suspended.load(std::memory_order_acquire) &&
        lease.magic==::amazon_remote::CaptureLeaseMagic && lease.version==1 && !lease.reserved &&
        lease.state==static_cast<std::uint32_t>(::amazon_remote::CaptureLeaseState::Active) &&
        lease.now_boot_ms && lease.now_boot_ms<=now && now-lease.now_boot_ms<=3000 &&
        lease.deadline_boot_ms>now && lease.deadline_boot_ms>60000 &&
        now>=lease.deadline_boot_ms-60000;
}
}
void Initialize() {
    fs::FileHandle file{};if(R_FAILED(fs::OpenFile(&file,ModePath,fs::OpenMode_Read)))return;
    ON_SCOPE_EXIT{fs::CloseFile(file);};
    constexpr char expected[]="amazon-general-v1\n";
    char mode[sizeof expected-1]{};s64 size{};std::size_t actual{};
    if(R_FAILED(fs::GetFileSize(&size,file)) || size!=sizeof mode ||
       R_FAILED(fs::ReadFile(&actual,file,0,mode,sizeof mode)) || actual!=sizeof mode)return;
    g_opted_in=std::memcmp(mode,expected,sizeof mode)==0;
}
bool Correct(std::uint32_t command,std::uint64_t program,std::uint64_t process,
             const void *data,std::size_t size,void *context,Forward forward,
             std::uint32_t &result) {
    if(!forward || !context || (command!=57 && command!=58))return false;
    using Policy=::amazon_remote::ManufacturerFilterCorrection;
    Policy::Plan plan{};
    {
        // Only bounded policy/snapshot work is locked. NEVER hold this lock
        // across driver IPC: Main's timer/PSC path must not wait for a driver.
        std::scoped_lock lk(g_lock);
        plan=command==57?g_policy.Add(program,process,g_opted_in,Live(),data,size):
            g_policy.Delete(program,process,data,size);
        if(plan.operation==Policy::Operation::PassOriginal)return false;
    }
    const auto now=armTicksToNs(armGetSystemTick())/1000000;
    result=forward(context,command,plan.forwarded.data(),plan.forwarded.size());
    {
        std::scoped_lock lk(g_lock);
        (void)g_policy.Commit(plan.operation,result);
        if(command==57){g_process=process;g_add_ms=now;g_add_result=result;}
        else {g_delete_ms=now;g_delete_result=result;}
    }
    return true;
}
void Poll() {
    if(!g_opted_in || g_storage_attempted || g_suspended.load(std::memory_order_acquire))return;
    ::amazon_remote::CaptureLeaseWire lease{};
    // Expiry alone can precede the caller's cleanup. Wait for its explicit End
    // (called after own stop), rather than save a premature Installed snapshot.
    if(!amazon_observer::ReadCaptureStatus(&lease,sizeof lease) ||
       lease.state!=static_cast<std::uint32_t>(::amazon_remote::CaptureLeaseState::Ended))return;
    Record r{};
    {
        std::scoped_lock lk(g_lock);
        r.state=static_cast<std::uint32_t>(g_policy.Current());r.flags=1;
        r.owner_process=g_process;r.add_boot_ms=g_add_ms;r.delete_boot_ms=g_delete_ms;
        r.add_result=g_add_result;r.delete_result=g_delete_result;
    }
    using Policy=::amazon_remote::ManufacturerFilterCorrection;
    if(r.state==static_cast<std::uint32_t>(Policy::State::Adding) ||
       r.state==static_cast<std::uint32_t>(Policy::State::Removing))return;
    const auto original=::amazon_remote::OriginalAmazonGeneralFilter();
    const auto corrected=::amazon_remote::CorrectedAmazonGeneralFilter();
    r.original_hash=Fnv(original.data(),original.size());r.corrected_hash=Fnv(corrected.data(),corrected.size());
    r.checksum=Fnv(&r,offsetof(Record,checksum));
    g_storage_attempted=true;
    // Exclusive create, no overwrite/truncate. A failed/partial file is evidence.
    if(R_FAILED(fs::CreateFile(ResultPath,sizeof r)))return;
    fs::FileHandle file{};if(R_FAILED(fs::OpenFile(&file,ResultPath,fs::OpenMode_Write)))return;
    ON_SCOPE_EXIT{fs::CloseFile(file);};
    (void)fs::WriteFile(file,0,&r,sizeof r,fs::WriteOption::Flush);
}
void Suspend(){g_suspended.store(true,std::memory_order_release);}
}
