// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "status_storage.hpp"
#include <cstdio>
#include <cstring>
namespace armodule {
// Only the optional automatic module uses this wrapper. At process startup it
// preserves a valid prior instance's status by a no-overwrite rename, verifies
// the archived bytes, then lets the original strict writer create its own file.
// No transport lease, pending attempt, bond, or callback state is reset here.
class RestartableStatusWriter {
    StatusWriter writer_;
    bool prepared_{}, archived_{};
    Result sticky_{};
    unsigned sticky_step_{};
    std::uint64_t instance_{}, previous_instance_{};
    char archive_[160]{};
    static constexpr Result Invalid() { return MAKERESULT(Module_Libnx,LibnxError_BadInput); }
    Result Fail(Result rc, bool sticky) {
        permanent=sticky;
        if (sticky) { sticky_=rc;sticky_step_=step; }
        return rc;
    }
    static Result Read(FsFileSystem &fs, const char *path, Status &record) {
        FsFile file{};auto rc=fsFsOpenFile(&fs,path,FsOpenMode_Read,&file);
        if(rc)return rc;
        s64 size{};rc=fsFileGetSize(&file,&size);
        if(!rc && size!=sizeof record)rc=Invalid();
        u64 actual{};
        if(!rc)rc=fsFileRead(&file,0,&record,sizeof record,0,&actual);
        if(!rc && actual!=sizeof record)rc=Invalid();
        fsFileClose(&file);return rc;
    }
public:
    enum Step : unsigned { PreviousRead=9,ArchiveDestination,ArchiveRename,ArchiveVerify };
    unsigned step{};bool permanent{};
    bool Archived() const { return archived_; }
    std::uint64_t PreviousInstance() const { return previous_instance_; }
    const char *ArchivePath() const { return archive_; }
    Result WriteRecord(FsFileSystem &fs,const Status &record) {
        permanent=false;
        if(sticky_){step=sticky_step_;permanent=true;return sticky_;}
        step=PreviousRead;
        if(!FreshStatus(record,record.now_ms) || (instance_ && instance_!=record.instance))return Fail(Invalid(),true);
        instance_=record.instance;
        if(!prepared_) {
            Status previous{};auto rc=Read(fs,StatusFsPath,previous);
            if(rc==0x202)prepared_=true; // Original writer will create exclusively.
            else {
                if(rc)return Fail(rc,rc!=0xe02);
                if(!FreshStatus(previous,previous.now_ms) || previous.instance==record.instance)return Fail(Invalid(),true);
                previous_instance_=previous.instance;
                step=ArchiveDestination;
                const auto count=std::snprintf(archive_,sizeof archive_,ARMODULE_NAV15_CONFIG_FS "/status-%llu-before-%llu.bin",
                    static_cast<unsigned long long>(previous.instance),static_cast<unsigned long long>(record.instance));
                if(count<=0 || std::size_t(count)>=sizeof archive_)return Fail(Invalid(),true);
                FsFile existing{};rc=fsFsOpenFile(&fs,archive_,FsOpenMode_Read,&existing);
                if(!rc){fsFileClose(&existing);return Fail(Invalid(),true);}
                if(rc!=0x202)return Fail(rc,rc!=0xe02);
                // Recheck source just before moving it. Ambiguous rename results
                // are terminal; never retry a possibly completed mutation.
                Status checked{};step=PreviousRead;rc=Read(fs,StatusFsPath,checked);
                if(rc)return Fail(rc,rc!=0xe02);
                if(std::memcmp(&checked,&previous,sizeof previous))return Fail(Invalid(),true);
                step=ArchiveRename;rc=fsFsRenameFile(&fs,StatusFsPath,archive_);
                if(rc)return Fail(rc,true);
                step=ArchiveVerify;Status preserved{};rc=Read(fs,archive_,preserved);
                if(rc)return Fail(rc,true);
                if(std::memcmp(&preserved,&previous,sizeof previous))return Fail(Invalid(),true);
                archived_=prepared_=true;
            }
        }
        const auto rc=writer_.WriteRecord(fs,record);step=writer_.step;permanent=writer_.permanent;
        if(rc && permanent)return Fail(rc,true);
        return rc;
    }
};
}
