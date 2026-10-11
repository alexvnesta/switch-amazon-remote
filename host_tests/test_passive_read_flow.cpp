// SPDX-License-Identifier: GPL-2.0-only
#include "../tools/amazon-remote-module/control/passive_read_io.hpp"
#include <cassert>
#include <vector>
#include <cstdio>
#include <filesystem>
#include <cstdlib>
namespace f=arpassiveread;
struct Store {
    std::vector<f::j::Checkpoint> records;
    unsigned fail_stage{99},snapshot_calls{};bool fail_snapshot{},existing{};
    bool Empty(){return records.empty() && !existing;}
    bool Save(const f::j::Checkpoint &r){if(r.sequence==fail_stage)return false;records.push_back(r);return true;}
    bool Matches(const f::j::Checkpoint &r){return !records.empty() && !std::memcmp(&r,&records.back(),sizeof r);}
    bool SaveSnapshot(const f::Record &r){++snapshot_calls;assert(f::Valid(r));return !fail_snapshot;}
};
struct Clock {unsigned time=100;unsigned Now(){return ++time;}};
struct Backend {
    Store &store;unsigned calls{},result{};bool bad{},conflict{};
    unsigned Read(f::p::Snapshot &out){
        ++calls;assert(store.records.size()==2 && store.records.back().stage==f::j::Stage::BeforeRead);
        f::p::Cache cache;cache.Start(50);
        f::p::Event event{};event.parameter_length=event.copied=9;event.prefix={0,8,0,0x34,0x12,0,1,16,0};
        cache.Observe(&event,sizeof event,60);
        if(conflict){event.prefix[7]=17;cache.Observe(&event,sizeof event,61);}
        out=cache.Read();if(bad)out.reserved0=1;return result;
    }
};
constexpr std::array<std::uint8_t,32> manifest{1,2,3};
struct Temp {
    std::string path;
    Temp(){char name[]="/tmp/passive-read-XXXXXX";auto p=::mkdtemp(name);assert(p);path=p;}
    ~Temp(){std::filesystem::remove_all(path);} // Only the freshly created test directory.
};
ssize_t ShortWrite(int fd,const void *p,size_t size){return ::write(fd,p,size/2);}
int FailSync(int){return -1;}
int main(){
    for(unsigned failure=0;failure<4;++failure){
        Store store;store.fail_stage=failure;Backend backend{store};Clock clock;f::Record record;
        assert(f::Collect(store,backend,clock,9,10,manifest,record)==f::Outcome::PersistenceFailed);
        assert(backend.calls==(failure>=2));
    }
    for(unsigned mode=0;mode<5;++mode){
        Store store;Backend backend{store};Clock clock;f::Record record;
        backend.result=mode==1?91:0;backend.bad=mode==2;backend.conflict=mode==3;store.fail_snapshot=mode==4;
        auto outcome=f::Collect(store,backend,clock,9,10,manifest,record);
        assert(backend.calls==1);
        if(mode==4){assert(outcome==f::Outcome::PersistenceFailed && store.records.size()==3);continue;}
        assert(store.records.size()==4);
        for(unsigned i=0;i<4;++i){assert(f::j::Valid(store.records[i]));if(i)assert(f::j::Follows(store.records[i],store.records[i-1]));}
        if(mode==1)assert(outcome==f::Outcome::ReadErrorSaved && !store.snapshot_calls && store.records.back().transport_result==91);
        else if(mode==2)assert(outcome==f::Outcome::InvalidReplySaved && !store.snapshot_calls &&
            store.records.back().reply.outcome==f::j::cap::Outcome::Malformed);
        else {
            assert(outcome==f::Outcome::Saved && f::Valid(record));
            assert((store.records.back().reply.outcome==f::j::cap::Outcome::NotObserved)==(mode==3));
            for(unsigned i=0;i<sizeof(record);++i){auto broken=record;reinterpret_cast<unsigned char*>(&broken)[i]^=1;assert(!f::Valid(broken));}
        }
    }
    Store store;store.existing=true;Backend backend{store};Clock clock;f::Record record;
    assert(f::Collect(store,backend,clock,9,10,manifest,record)==f::Outcome::PersistenceFailed && !backend.calls);
    f::p::Cache cache;cache.Start(50);record.run=9;record.reader_context=10;record.snapshot=cache.Read();record.checksum=f::Checksum(record);
    for(unsigned mode=0;mode<3;++mode){
        Temp temp;f::FileStore disk(temp.path.c_str(),mode==1?ShortWrite: ::write,mode==2?FailSync: ::fsync);
        assert(disk.Empty());assert(disk.SaveSnapshot(record)==(mode==0));
        assert(!disk.Empty() && !disk.SaveSnapshot(record)); // Orphan/partial snapshot blocks reuse.
        assert(std::filesystem::exists(temp.path+"/snapshot.bin"));
        f::j::Journal journal;assert(!journal.Start(disk,11,12,100,manifest));
        f::Record back{};const auto complete=armodule::ReadExact((temp.path+"/snapshot.bin").c_str(),back);
        assert(complete==(mode!=1));if(complete)assert(f::Valid(back));
    }
    std::puts("PASS passive read flow: checkpoints precede getter, all persistence boundaries, failed/invalid/conflicting replies, exact record validation, no radio operations");
}
