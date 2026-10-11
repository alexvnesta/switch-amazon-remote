// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "deferred_reader_flow.hpp"
#include "observation_checkpoint_io.hpp"
namespace ardeferredreader {
class FileStore:public j::FileStore {
public:
    explicit FileStore(const char *directory,Write writer=::write,Sync syncer=::fsync)
        :j::FileStore(directory,writer,syncer),root_(directory),writer_(writer),syncer_(syncer){}
    bool Empty() const {
        struct stat value{};char path[512];
        if(!j::FileStore::Empty())return false;
        for(auto name:{"identity-matched.bin","snapshot.bin"})
            if(!Path(name,path) || ::lstat(path,&value)==0 || errno!=ENOENT)return false;
        return true;
    }
    bool SaveIdentityMatch(std::uint64_t run,std::uint64_t context) const {
        const std::array<std::uint64_t,3> record{0x314449444152,run,context};
        return run && context && SaveExclusive("identity-matched.bin",record);
    }
    bool SaveSnapshot(const Record &r)const{return Valid(r) && SaveExclusive("snapshot.bin",r);}
private:
    bool Path(const char *name,char (&path)[512])const {
        if(!root_ || !*root_)return false;
        const int count=std::snprintf(path,sizeof path,"%s/%s",root_,name);
        return count>0 && std::size_t(count)<sizeof path;
    }
    template<class T> bool SaveExclusive(const char *name,const T &value)const {
        char path[512];if(!Path(name,path))return false;
        const int fd=::open(path,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return false;
        const bool wrote=writer_(fd,&value,sizeof value)==ssize_t(sizeof value);
        const bool synced=syncer_(fd)==0,closed=::close(fd)==0;
        T back{};
        return wrote && synced && closed && armodule::ReadExact(path,back) && !std::memcmp(&back,&value,sizeof value);
    }
    const char *root_;
    Write writer_;
    Sync syncer_;
};
}
