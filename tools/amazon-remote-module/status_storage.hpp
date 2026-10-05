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
public:
    enum Step : unsigned { Open=1,Create,Reopen,Size,Read,Prior,Write,Done };
    unsigned step{};bool permanent{};
    Result WriteRecord(FsFileSystem &fs,const Status &record) {
        constexpr char path[]="/config/amazon-remote-nav10/status.bin";
        step=Open;permanent=false;FsFile file{};
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
        if (!rc) { step=Read;rc=fsFileRead(&file,0,&previous,sizeof previous,0,&read); }
        if (!rc) {
            step=Prior;
            // Newly created FS bytes are zero, not a default-constructed Status.
            const auto *bytes=reinterpret_cast<const unsigned char *>(&previous);
            bool all_zero=true;for (std::size_t i=0;i<sizeof previous;++i) all_zero&=bytes[i]==0;
            if (read!=sizeof previous || (!(created_ && !initialized_ && all_zero) &&
                !(FreshStatus(previous,previous.now_ms) && previous.instance==record.instance))) {
                permanent=true;rc=0x402;
            }
        }
        if (!rc) {
            step=Write;rc=fsFileWrite(&file,0,&record,sizeof record,FsWriteOption_Flush);
            if (rc) permanent=true; // do not overwrite a possibly partial failed write
        }
        fsFileClose(&file);
        if (!rc) { initialized_=true;step=Done; }
        return rc;
    }
};
}
