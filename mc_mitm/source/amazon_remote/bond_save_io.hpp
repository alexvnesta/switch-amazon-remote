// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>
#include "legacy_bond_material.hpp"
#include <algorithm>
#include <array>
#include <span>

namespace amazon_remote::bond::save_io {
// Private native I/O component, NOT a provisioning tool or complete bond store.
// This is the current candidate input-module ID, not a newly allocated save ID.
// Nothing creates/mounts this storage at runtime yet. Provisioning and approval
// of the final owner ID must precede a device test. No fallback to SD or system
// savedata is permitted. The caller initializes FS and serializes all writers.
inline constexpr u64 OwnerProgramId=0x010000000000a4d9;
inline constexpr std::size_t RecordBytes=512;
inline constexpr char SlotPath[FS_MAX_PATH]="/amazon-remote-bond-v1.bin";
using Bytes=std::array<std::uint8_t,RecordBytes>;
using Input=std::span<const std::uint8_t,RecordBytes>;
using Output=std::span<std::uint8_t,RecordBytes>;
enum class Outcome {
    Refused, PreviousRecordReadBack, CommittedAndReadBack, RecoveryRequired,
    AlreadyConsumed
};
enum class Step {
    Admission, Inventory, ExtraData, Open, ReadOpen, Size, Read, Compare,
    WriteOpen, Write, Flush, Commit, Complete
};
struct Receipt { Outcome outcome{Outcome::Refused}; Step step{Step::Admission}; Result result{}; };

inline FsSaveDataAttribute Attribute() {
    FsSaveDataAttribute a{};
    a.application_id=OwnerProgramId;
    a.save_data_type=FsSaveDataType_Device;
    a.save_data_rank=FsSaveDataRank_Primary;
    return a;
}
inline bool SameAttribute(const FsSaveDataAttribute &a) {
    return a.application_id==OwnerProgramId && !a.uid.uid[0] && !a.uid.uid[1] &&
        !a.system_save_data_id && a.save_data_type==FsSaveDataType_Device &&
        a.save_data_rank==FsSaveDataRank_Primary && !a.save_data_index &&
        !a.unk_x28 && !a.unk_x30 && !a.unk_x38;
}
inline bool SameInfo(const FsSaveDataInfo &a) {
    return a.save_data_id && a.application_id==OwnerProgramId &&
        a.save_data_space_id==FsSaveDataSpaceId_User &&
        a.save_data_type==FsSaveDataType_Device && !a.system_save_data_id &&
        !a.uid.uid[0] && !a.uid.uid[1] && !a.save_data_index &&
        a.save_data_rank==FsSaveDataRank_Primary;
}

class DeviceSlot {
    struct PrivateBuffer {
        Bytes bytes{};
        ~PrivateBuffer() { legacy::Erase(bytes); }
    };
    bool consumed_{};
    Step step_{Step::Admission}; Result result_{};
    bool Call(Result result, Step step) {
        step_=step; result_=result; return result==0;
    }
    bool Identify(u64 &id) {
        // Query only the exact project Device save in USER, never enumerate or
        // open account saves, Nintendo Bluetooth system saves or raw partitions.
        FsSaveDataFilter filter{};
        filter.filter_by_application_id=true;
        filter.filter_by_save_data_type=true;
        filter.filter_by_user_id=true;
        filter.filter_by_system_save_data_id=true;
        filter.filter_by_index=true;
        filter.save_data_rank=FsSaveDataRank_Primary;
        filter.attr=Attribute();
        FsSaveDataInfoReader reader{};
        if (!Call(fsOpenSaveDataInfoReaderWithFilter(&reader,FsSaveDataSpaceId_User,&filter),Step::Inventory))
            return false;
        std::array<FsSaveDataInfo,2> rows{}; s64 count{};
        bool ok=Call(fsSaveDataInfoReaderRead(&reader,rows.data(),rows.size(),&count),Step::Inventory);
        ok=ok && count==1 && SameInfo(rows[0]);
        if (ok) {
            id=rows[0].save_data_id;
            // A short read need not be EOF. Refuse a second matching save.
            ok=Call(fsSaveDataInfoReaderRead(&reader,rows.data(),rows.size(),&count),Step::Inventory);
            ok=ok && count==0;
        }
        fsSaveDataInfoReaderClose(&reader);
        if (!ok) return false;
        FsSaveDataExtraData extra{};
        if (!Call(fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(
                &extra,sizeof extra,FsSaveDataSpaceId_User,id),Step::ExtraData)) return false;
        return SameAttribute(extra.attr) && extra.owner_id==OwnerProgramId &&
            extra.data_size>=s64(RecordBytes) && extra.journal_size>=s64(RecordBytes);
    }
    bool Open(bool readonly, FsFileSystem &fs) {
        u64 id{};
        if (!Identify(id)) return false;
        const auto attr=Attribute();
        return Call(readonly ? fsOpenReadOnlySaveDataFileSystem(&fs,FsSaveDataSpaceId_User,&attr) :
            fsOpenSaveDataFileSystem(&fs,FsSaveDataSpaceId_User,&attr),Step::Open);
    }
    bool Read(FsFileSystem &fs, Output out) {
        legacy::Erase(out);
        FsFile file{};
        if (!Call(fsFsOpenFile(&fs,SlotPath,FsOpenMode_Read,&file),Step::ReadOpen)) return false;
        s64 size{}; u64 read{};
        bool ok=Call(fsFileGetSize(&file,&size),Step::Size) && size==s64(out.size());
        if (ok) ok=Call(fsFileRead(&file,0,out.data(),out.size(),FsReadOption_None,&read),Step::Read)
                   && read==out.size();
        fsFileClose(&file);
        if (!ok) legacy::Erase(out);
        return ok;
    }
    bool Equal(FsFileSystem &fs, Input expected) {
        PrivateBuffer buffer;
        if (!Read(fs,buffer.bytes)) return false;
        step_=Step::Compare; result_=0;
        // Local byte equality is integrity/readback, not a MAC or authentication.
        return std::equal(expected.begin(),expected.end(),buffer.bytes.begin());
    }
    bool ReopenAndEqual(Input expected) {
        FsFileSystem fs{};
        if (!Open(true,fs)) return false;
        const bool ok=Equal(fs,expected);
        fsFsClose(&fs);
        return ok;
    }
    Receipt Abandon(FsFileSystem &fs, Input previous) {
        const auto failed_step=step_; const auto failed_result=result_;
        fsFsClose(&fs);
        // Do not assume close rolled the journal back. Reopen READ ONLY and
        // compare every old byte. Never write an old record to undo an error.
        const bool retained=ReopenAndEqual(previous);
        return {retained ? Outcome::PreviousRecordReadBack : Outcome::RecoveryRequired,
                failed_step,failed_result};
    }
public:
    DeviceSlot()=default;
    DeviceSlot(const DeviceSlot &)=delete;
    DeviceSlot &operator=(const DeviceSlot &)=delete;
    // Caller supplies already validated private record bytes, including stable
    // identity/schema/generation and successful complete negotiated-bond proof.
    // This layer deliberately has no key decoder or enrollment policy. It is
    // NOT an IPC authority boundary; untrusted bytes must never reach Replace.
    // Previous/next buffers must remain stable under the same ownership lock.
    Receipt Replace(Input previous, Input next) {
        return Replace(previous,next,[]{ return true; });
    }
    // A private transaction rechecks live owner/epoch/deadline before opening,
    // before any write, and immediately before commit. The caller holds its
    // ownership/writer lock throughout; the guard is not itself a lock.
    template<class Guard> Receipt Replace(Input previous, Input next, Guard guard) {
        if (consumed_) return {Outcome::AlreadyConsumed,Step::Admission,0};
        consumed_=true;
        if (!guard() || std::equal(previous.begin(),previous.end(),next.begin())) return {};
        FsFileSystem fs{};
        if (!Open(false,fs)) return {Outcome::Refused,step_,result_};
        if (!Equal(fs,previous)) {
            fsFsClose(&fs); return {Outcome::Refused,step_,result_};
        }
        FsFile file{};
        if (!Call(fsFsOpenFile(&fs,SlotPath,FsOpenMode_Write,&file),Step::WriteOpen))
            return Abandon(fs,previous);
        if (!guard()) { fsFileClose(&file); fsFsClose(&fs); return {}; }
        // The fixed-size pre-existing slot was checked above. No create,
        // truncate, delete, rename, savedata creation/deletion or raw-partition
        // API exists here.
        bool ok=Call(fsFileWrite(&file,0,next.data(),next.size(),FsWriteOption_None),Step::Write);
        if (ok) ok=Call(fsFileFlush(&file),Step::Flush);
        fsFileClose(&file);
        if (!ok || !Equal(fs,next)) return Abandon(fs,previous);
        if (!guard()) {
            step_=Step::Admission; result_=0; return Abandon(fs,previous);
        }
        // A failed commit may have taken effect. Do NOT retry or roll back it.
        if (!Call(fsFsCommit(&fs),Step::Commit)) {
            fsFsClose(&fs); return {Outcome::RecoveryRequired,step_,result_};
        }
        fsFsClose(&fs);
        if (!ReopenAndEqual(next)) return {Outcome::RecoveryRequired,step_,result_};
        return {Outcome::CommittedAndReadBack,Step::Complete,0};
    }
    // Restart/recovery getter. The private record parser must validate these
    // bytes before native reload. It has no automatic pairing or connection.
    bool ReadCommitted(Output out) {
        legacy::Erase(out);
        FsFileSystem fs{};
        if (!Open(true,fs)) return false;
        const bool ok=Read(fs,out);
        fsFsClose(&fs);
        return ok;
    }
};
// Native API implementation, host tested with an injected SDK. No hardware
// durability/provisioning or access proof, and no production caller yet.
// Private means outside the FTP SD mount, NOT inaccessible to privileged mods.
}
