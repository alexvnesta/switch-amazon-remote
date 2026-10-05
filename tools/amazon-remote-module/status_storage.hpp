// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>
#include "protocol.hpp"
namespace armodule {
constexpr bool StatusFailureExpired(std::uint64_t first,std::uint64_t now,bool permanent) {
    return permanent || !first || now<first || now-first>=2000;
}
// This file is created only by this process. Never delete or rename it while the
// control app reads it. Checksum/freshness reject partial snapshots. Failed opens can
// be retried for a bounded interval; failed/partial writes are preserved.
class StatusWriter {
    bool created_{},initialized_{};
    Result failed_write_{};
public:
    enum Step : unsigned { Open=1,Create,Reopen,Size,Read,Prior,Write,Done };
    unsigned step{};bool permanent{};
    Result WriteRecord(FsFileSystem &fs,const Status &record) {
        constexpr auto path=StatusFsPath;
        step=Open;permanent=false;FsFile file{};
        if (failed_write_) { step=Write;permanent=true;return failed_write_; }
        if (!FreshStatus(record,record.now_ms)) { permanent=true;return 0x402; }
        auto rc=fsFsOpenFile(&fs,path,FsOpenMode_Read|FsOpenMode_Write,&file);
        if (rc==0x202 && !created_) {
            step=Create;rc=fsFsCreateFile(&fs,path,sizeof record,0);
            if (rc) return rc;
            created_=true;step=Reopen;rc=fsFsOpenFile(&fs,path,FsOpenMode_Read|FsOpenMode_Write,&file);
        }
        if (rc) return rc;
        s64 size{};step=Size;rc=fsFileGetSize(&file,&size);
        if (!rc && (!created_ || size!=sizeof record)) { permanent=true;rc=0x402; }
        Status previous{};u64 read{};
        if (!rc && initialized_) { step=Read;rc=fsFileRead(&file,0,&previous,sizeof previous,0,&read); }
        if (!rc && initialized_) {
            step=Prior;
            if (read!=sizeof previous || !FreshStatus(previous,previous.now_ms) || previous.instance!=record.instance) {
                permanent=true;rc=0x402;
            }
        }
        if (!rc) {
            // Allocation does not imply zero-filled bytes. Only a file created
            // by THIS writer may receive its first complete snapshot without a
            // prior-record check. Preexisting files are still refused above.
            step=Write;rc=fsFileWrite(&file,0,&record,sizeof record,FsWriteOption_Flush);
            if (rc) { failed_write_=rc;permanent=true; } // preserve partials even if caller retries
        }
        fsFileClose(&file);
        if (!rc) { initialized_=true;step=Done; }
        return rc;
    }
};
}
