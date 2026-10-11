// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "observation_checkpoint.hpp"
#include "../io.hpp"
#include <fcntl.h>

namespace arobservation {
// Called only outside Bluetooth callbacks. root is a trusted, task-owned
// directory prepared before the run, never a path supplied by a radio event.
class FileStore {
public:
    using Write = ssize_t (*)(int,const void *,size_t);
    using Sync = int (*)(int);
    explicit FileStore(const char *root,Write writer=::write,Sync syncer=::fsync)
        : root_(root),write_(writer),sync_(syncer) {}
    bool Empty() const {
        for(unsigned slot=0;slot<4;++slot) {
            char path[512];struct stat value{};
            if(!Path(slot,path) || ::lstat(path,&value)==0 || errno!=ENOENT) return false;
        }
        return true;
    }
    bool Matches(const Checkpoint &expected) const {
        Checkpoint back{};
        return Valid(expected) && ReadSlot(expected.sequence,back)==SlotState::Present &&
               !std::memcmp(&expected,&back,sizeof back);
    }
    SlotState ReadSlot(unsigned slot,Checkpoint &record) const {
        char path[512];struct stat entry{};
        if(!Path(slot,path))return SlotState::Invalid;
        if(::lstat(path,&entry)!=0)return errno==ENOENT ? SlotState::Missing : SlotState::Invalid;
        if(!S_ISREG(entry.st_mode) || !armodule::ReadExact(path,record) || !Valid(record))
            return SlotState::Invalid;
        return SlotState::Present;
    }
    bool Save(const Checkpoint &record) const {
        char path[512];
        if(!Valid(record) || !Path(record.sequence,path)) return false;
        const int fd=::open(path,O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd<0) return false;
        const bool wrote=write_(fd,&record,sizeof record)==ssize_t(sizeof record);
        const bool synced=sync_(fd)==0, closed=::close(fd)==0;
        // Keep failed/partial files. No retry, delete, rename or replacement.
        return wrote && synced && closed && Matches(record);
    }
private:
    bool Path(unsigned slot,char (&path)[512]) const {
        if(!root_ || !*root_ || slot>=4) return false;
        constexpr const char *names[]={"00-prepared.bin","01-before-read.bin",
                                      "02-read-returned.bin","03-finished.bin"};
        const auto count=std::snprintf(path,sizeof path,"%s/%s",root_,names[slot]);
        return count>0 && std::size_t(count)<sizeof path;
    }
    const char *root_;
    Write write_;
    Sync sync_;
};
}
