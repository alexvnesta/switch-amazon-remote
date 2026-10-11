// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "observation_checkpoint_io.hpp"
#include "passive_read_flow.hpp"
namespace arpassiveread {
class FileStore : public arobservation::FileStore {
public:
    explicit FileStore(const char *directory,Write writer=::write,Sync syncer=::fsync)
        :arobservation::FileStore(directory),directory_(directory),writer_(writer),syncer_(syncer){}
    bool Empty() const {
        char path[512];struct stat entry{};
        return arobservation::FileStore::Empty() && SnapshotPath(path) &&
            ::lstat(path,&entry)!=0 && errno==ENOENT;
    }
    bool SaveSnapshot(const Record &record) const {
        char path[512];if(!Valid(record) || !SnapshotPath(path))return false;
        const int fd=::open(path,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return false;
        const bool wrote=writer_(fd,&record,sizeof record)==ssize_t(sizeof record);
        const bool synced=syncer_(fd)==0,closed=::close(fd)==0;
        Record back{};
        return wrote && synced && closed && armodule::ReadExact(path,back) && Valid(back) &&
            !std::memcmp(&record,&back,sizeof record);
    }
private:
    bool SnapshotPath(char (&path)[512]) const {
        if(!directory_ || !*directory_)return false;
        const auto count=std::snprintf(path,sizeof path,"%s/snapshot.bin",directory_);
        return count>0 && std::size_t(count)<sizeof path;
    }
    const char *directory_;
    Write writer_;
    Sync syncer_;
};
}
