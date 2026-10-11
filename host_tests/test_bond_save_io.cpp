// SPDX-License-Identifier: GPL-2.0-only
#include "../mc_mitm/source/amazon_remote/bond_save_io.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace amazon_remote::bond::save_io;
static_assert(sizeof(FsSaveDataAttribute)==0x40);
static_assert(sizeof(FsSaveDataInfo)==0x60);
static_assert(sizeof(FsSaveDataFilter)==0x48);
static_assert(sizeof(FsSaveDataExtraData)==0x200);
static_assert(offsetof(FsSaveDataInfo,application_id)==0x28);
static_assert(offsetof(FsSaveDataExtraData,owner_id)==0x40);

namespace {
enum class Fault {
    None, Missing, Duplicate, LateDuplicate, BadCount, OverflowCount, WrongApp, WrongSpace,
    WrongType, WrongUser, WrongSystemId, WrongIndex, WrongRank, ZeroSaveId,
    WrongOwner, WrongExtraAttr, ExtraUnknown, TooSmall, NoJournal, WrongLength,
    ShortRead, StagedMismatch, PostMismatch, PostOwnerChange, ClosePersistsPartial
};
struct Fake {
    Bytes committed{},pending{};
    FsSaveDataInfo info{}; FsSaveDataExtraData extra{};
    Fault fault{};
    unsigned fail_call{},fail_call2{},calls{},files{},filesystems{},readers{},writes{},commits{},opens{};
    bool committed_once{},commit_failure_persists{};
    std::vector<std::string> trace;
    Result Call(const char *name) {
        trace.emplace_back(name); ++calls;
        return calls==fail_call || calls==fail_call2 ? 0xdead : 0;
    }
} f;
Bytes Before() { Bytes a{}; for(std::size_t i=0;i<a.size();++i)a[i]=std::uint8_t(i); return a; }
Bytes After() { auto a=Before(); for(auto &b:a)b^=0xa5; return a; }
void Reset(Fault fault=Fault::None,unsigned fail=0) {
    f=Fake{}; f.fault=fault; f.fail_call=fail;
    f.committed=Before(); f.pending=f.committed;
    f.info.save_data_id=17; f.info.save_data_space_id=FsSaveDataSpaceId_User;
    f.info.save_data_type=FsSaveDataType_Device; f.info.application_id=OwnerProgramId;
    f.extra.attr=Attribute(); f.extra.owner_id=OwnerProgramId;
    f.extra.data_size=f.extra.journal_size=0x4000;
    switch(fault) {
        case Fault::WrongApp: ++f.info.application_id; break;
        case Fault::WrongSpace: f.info.save_data_space_id=FsSaveDataSpaceId_System; break;
        case Fault::WrongType: f.info.save_data_type=FsSaveDataType_Account; break;
        case Fault::WrongUser: f.info.uid.uid[1]=1; break;
        case Fault::WrongSystemId: f.info.system_save_data_id=1; break;
        case Fault::WrongIndex: f.info.save_data_index=1; break;
        case Fault::WrongRank: f.info.save_data_rank=1; break;
        case Fault::ZeroSaveId: f.info.save_data_id=0; break;
        case Fault::WrongOwner: ++f.extra.owner_id; break;
        case Fault::WrongExtraAttr: ++f.extra.attr.application_id; break;
        case Fault::ExtraUnknown: f.extra.attr.unk_x30=1; break;
        case Fault::TooSmall: f.extra.data_size=RecordBytes-1; break;
        case Fault::NoJournal: f.extra.journal_size=0; break;
        default: break;
    }
}
void Closed() { assert(!f.files && !f.filesystems && !f.readers); }
void Scope(FsSaveDataSpaceId space,const FsSaveDataAttribute *a) {
    assert(space==FsSaveDataSpaceId_User && SameAttribute(*a));
}
[[maybe_unused]] void NoMoreCalls(DeviceSlot &s) {
    auto before=f.calls;
    assert(s.Replace(Before(),After()).outcome==Outcome::AlreadyConsumed);
    assert(f.calls==before); Closed();
}
}

Result fsOpenSaveDataInfoReaderWithFilter(FsSaveDataInfoReader *r,FsSaveDataSpaceId s,const FsSaveDataFilter *a) {
    Scope(s,&a->attr);
    assert(a->filter_by_application_id && a->filter_by_save_data_type &&
           a->filter_by_user_id && a->filter_by_system_save_data_id && a->filter_by_index &&
           a->save_data_rank==FsSaveDataRank_Primary);
    if(auto rc=f.Call("inventory-open"))return rc;
    r->handle=1; r->reads=0; ++f.readers; return 0;
}
Result fsSaveDataInfoReaderRead(FsSaveDataInfoReader *r,FsSaveDataInfo *out,std::size_t n,s64 *count) {
    assert(r->handle && n==2);
    if(auto rc=f.Call("inventory-read"))return rc;
    *count=0;
    if(r->reads++==0) {
        *count=f.fault==Fault::Missing ? 0 : f.fault==Fault::Duplicate ? 2 :
               f.fault==Fault::BadCount ? -1 : f.fault==Fault::OverflowCount ? 3 : 1;
        out[0]=out[1]=f.info;
    } else if(f.fault==Fault::LateDuplicate) { *count=1; out[0]=f.info; }
    return 0;
}
void fsSaveDataInfoReaderClose(FsSaveDataInfoReader *r) {
    assert(r->handle && f.readers); r->handle=0; --f.readers;
}
Result fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(void *out,std::size_t n,FsSaveDataSpaceId s,u64 id) {
    assert(s==FsSaveDataSpaceId_User && id==17 && n==sizeof f.extra);
    if(auto rc=f.Call("extra"))return rc;
    auto extra=f.extra;
    if(f.fault==Fault::PostOwnerChange && f.committed_once)++extra.owner_id;
    std::memcpy(out,&extra,n); return 0;
}
Result fsOpenSaveDataFileSystem(FsFileSystem *fs,FsSaveDataSpaceId s,const FsSaveDataAttribute *a) {
    Scope(s,a); assert(!f.filesystems);
    if(auto rc=f.Call("open-rw"))return rc;
    ++f.filesystems; ++f.opens; *fs={1,false}; f.pending=f.committed; return 0;
}
Result fsOpenReadOnlySaveDataFileSystem(FsFileSystem *fs,FsSaveDataSpaceId s,const FsSaveDataAttribute *a) {
    Scope(s,a); assert(!f.filesystems);
    if(auto rc=f.Call("open-ro"))return rc;
    ++f.filesystems; ++f.opens; *fs={1,true}; return 0;
}
Result fsFsOpenFile(FsFileSystem *fs,const char *path,u32 mode,FsFile *file) {
    assert(fs->handle && f.filesystems && !f.files);
    assert(std::strcmp(path,SlotPath)==0 && path[FS_MAX_PATH-1]==0);
    assert(mode==FsOpenMode_Read || (mode==FsOpenMode_Write && !fs->readonly));
    if(auto rc=f.Call(mode==FsOpenMode_Read ? "file-read" : "file-write"))return rc;
    *file={mode,fs->readonly}; ++f.files; return 0;
}
Result fsFileGetSize(FsFile *file,s64 *n) {
    assert(file->handle && f.files);
    if(auto rc=f.Call("size"))return rc;
    *n=f.fault==Fault::WrongLength ? RecordBytes+1 : RecordBytes; return 0;
}
Result fsFileRead(FsFile *file,s64 off,void *out,u64 n,u32 opt,u64 *read) {
    assert(file->handle==FsOpenMode_Read && off==0 && n==RecordBytes && opt==FsReadOption_None);
    if(auto rc=f.Call("read"))return rc;
    std::memcpy(out,(file->readonly ? f.committed : f.pending).data(),n);
    *read=f.fault==Fault::ShortRead ? n-1 : n;
    if((f.fault==Fault::StagedMismatch && !file->readonly && f.writes) ||
       (f.fault==Fault::PostMismatch && f.committed_once && file->readonly))
        static_cast<u8*>(out)[30]^=1;
    return 0;
}
Result fsFileWrite(FsFile *file,s64 off,const void *in,u64 n,u32 opt) {
    assert(file->handle==FsOpenMode_Write && !file->readonly && off==0 &&
           n==RecordBytes && opt==FsWriteOption_None);
    ++f.writes;
    // Simulate a partial write even when the syscall returns an error.
    if(auto rc=f.Call("write")) { std::memcpy(f.pending.data(),in,n/2); return rc; }
    if(f.fault==Fault::ClosePersistsPartial) {
        std::memcpy(f.pending.data(),in,n/2); return 0xdead;
    }
    std::memcpy(f.pending.data(),in,n); return 0;
}
Result fsFileFlush(FsFile *file) {
    assert(file->handle==FsOpenMode_Write && f.files); return f.Call("flush");
}
void fsFileClose(FsFile *file) {
    assert(file->handle && f.files); file->handle=0; --f.files;
}
Result fsFsCommit(FsFileSystem *fs) {
    assert(fs->handle && !fs->readonly && f.filesystems && !f.files);
    ++f.commits;
    const auto rc=f.Call("commit");
    if(!rc || f.commit_failure_persists) { f.committed=f.pending; f.committed_once=true; }
    return rc;
}
void fsFsClose(FsFileSystem *fs) {
    assert(fs->handle && f.filesystems && !f.files);
    if(!fs->readonly && f.fault==Fault::ClosePersistsPartial)f.committed=f.pending;
    fs->handle=0; --f.filesystems;
}

// Reuse the same syscall fault injector in the integrated private-store test.
// Each test remains a separate executable; no production code includes this TU.
#ifndef BOND_SAVE_FIXTURE_ONLY
int main() {
    Reset(); DeviceSlot first;
    auto ok=first.Replace(Before(),After());
    assert(ok.outcome==Outcome::CommittedAndReadBack && ok.step==Step::Complete && !ok.result);
    assert(f.writes==1 && f.commits==1 && f.committed==After()); Closed();
    auto count=f.calls; const auto successful_trace=f.trace; NoMoreCalls(first);
    // Failure at every Result-returning SDK call in the success route.
    for(unsigned failure=1;failure<=count;++failure) {
        Reset(Fault::None,failure); DeviceSlot slot;
        auto r=slot.Replace(Before(),After());
        assert(r.outcome!=Outcome::CommittedAndReadBack && r.outcome!=Outcome::AlreadyConsumed);
        assert(f.writes<=1 && f.commits<=1); Closed();
        if(f.committed_once) {
            assert(r.outcome==Outcome::RecoveryRequired && f.committed==After());
        } else {
            assert(f.committed==Before());
            if(successful_trace[failure-1]=="commit")assert(r.outcome==Outcome::RecoveryRequired);
        }
        NoMoreCalls(slot);
    }
    // Commit returns an error after actually committing: never retry/undo it.
    auto commit=std::find(successful_trace.begin(),successful_trace.end(),"commit");
    assert(commit!=successful_trace.end());
    Reset(Fault::None,unsigned(commit-successful_trace.begin()+1));
    f.commit_failure_persists=true; DeviceSlot unknown;
    assert(unknown.Replace(Before(),After()).outcome==Outcome::RecoveryRequired);
    assert(f.committed==After() && f.commits==1 && f.writes==1); NoMoreCalls(unknown);
    // An initial partial-write error followed by a SECOND error during recovery
    // must not report that the prior record was retained. No retry/repair write.
    auto write=std::find(successful_trace.begin(),successful_trace.end(),"write");
    assert(write!=successful_trace.end());
    const auto write_call=unsigned(write-successful_trace.begin()+1);
    Reset(Fault::None,write_call); DeviceSlot abandoning;
    assert(abandoning.Replace(Before(),After()).outcome==Outcome::PreviousRecordReadBack);
    const auto recovery_end=f.calls;
    for(unsigned second=write_call+1;second<=recovery_end;++second) {
        Reset(Fault::None,write_call); f.fail_call2=second; DeviceSlot slot;
        assert(slot.Replace(Before(),After()).outcome==Outcome::RecoveryRequired);
        assert(f.writes==1 && !f.commits && f.committed==Before()); NoMoreCalls(slot);
    }
    for(auto fault:{Fault::Missing,Fault::Duplicate,Fault::LateDuplicate,Fault::BadCount,Fault::OverflowCount,
                   Fault::WrongApp,Fault::WrongSpace,Fault::WrongType,Fault::WrongUser,
                   Fault::WrongSystemId,Fault::WrongIndex,Fault::WrongRank,Fault::ZeroSaveId,
                   Fault::WrongOwner,Fault::WrongExtraAttr,Fault::ExtraUnknown,Fault::TooSmall,
                   Fault::NoJournal,Fault::WrongLength,Fault::ShortRead}) {
        Reset(fault); DeviceSlot slot;
        assert(slot.Replace(Before(),After()).outcome==Outcome::Refused);
        assert(!f.writes && !f.commits && f.committed==Before()); NoMoreCalls(slot);
    }
    for(auto fault:{Fault::StagedMismatch,Fault::PostMismatch,Fault::PostOwnerChange,Fault::ClosePersistsPartial}) {
        Reset(fault); DeviceSlot slot;
        auto expected=fault==Fault::StagedMismatch ? Outcome::PreviousRecordReadBack : Outcome::RecoveryRequired;
        assert(slot.Replace(Before(),After()).outcome==expected);
        assert(f.writes==1 && f.commits==((fault==Fault::PostMismatch || fault==Fault::PostOwnerChange) ? 1u : 0u)); NoMoreCalls(slot);
    }
    Reset(); DeviceSlot unchanged;
    assert(unchanged.Replace(Before(),Before()).outcome==Outcome::Refused && !f.calls);
    NoMoreCalls(unchanged);
    Reset(); DeviceSlot stale; auto wrong=Before(); wrong[12]^=1;
    assert(stale.Replace(wrong,After()).outcome==Outcome::Refused && !f.writes && !f.commits);
    NoMoreCalls(stale);
    // The recovery read is metadata + read-only filesystem only, with cleared
    // output on EVERY error/short read. It never pairs or reloads a key.
    Reset(); DeviceSlot reader; Bytes bytes{};
    assert(reader.ReadCommitted(bytes) && bytes==Before()); Closed();
    const auto reads=f.calls;
    for(unsigned failure=1;failure<=reads;++failure) {
        Reset(Fault::None,failure); DeviceSlot slot; bytes.fill(0xa5);
        assert(!slot.ReadCommitted(bytes) && bytes==Bytes{} && !f.writes && !f.commits); Closed();
    }
    Reset(Fault::ShortRead); DeviceSlot short_read; bytes.fill(0xa5);
    assert(!short_read.ReadCommitted(bytes) && bytes==Bytes{}); Closed();
    std::printf("PASS native private-save adapter: %u syscall failures, %u recovery-double-failures, %u getter failures, ownership/missing/duplicate/partial/ambiguous-commit checks. SDK MOCK, NOT hardware durability.\n",count,recovery_end-write_call,reads);
}
#endif
