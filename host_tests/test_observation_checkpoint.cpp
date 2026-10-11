// SPDX-License-Identifier: GPL-2.0-only
#include "../tools/amazon-remote-module/control/observation_checkpoint_io.hpp"
#include <cassert>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdlib>

namespace j=arobservation;
struct FakeStore {
    std::vector<j::Checkpoint> records;
    bool fail{},changed{};
    bool Empty(){return records.empty();}
    bool Save(const j::Checkpoint &r){if(fail)return false;records.push_back(r);return true;}
    bool Matches(const j::Checkpoint &r){return !changed && !records.empty() &&
        !std::memcmp(&r,&records.back(),sizeof r);}
    j::SlotState ReadSlot(unsigned slot,j::Checkpoint &out) const {
        if(slot>=records.size())return j::SlotState::Missing;
        out=records[slot];return j::SlotState::Present;
    }
};
constexpr std::array<std::uint8_t,32> Manifest{1,2,3}; // Invented fixture, not deployment approval.
ssize_t ShortWrite(int fd,const void *p,size_t n){return ::write(fd,p,n/2);}
ssize_t CorruptWrite(int fd,const void *p,size_t n){
    auto copy=*static_cast<const j::Checkpoint *>(p);copy.time_ms^=1;return ::write(fd,&copy,n);
}
int FailSync(int){return -1;}
struct Temp {
    std::string root;
    Temp(){char path[]="/tmp/remote-checkpoint-XXXXXX";const auto *p=::mkdtemp(path);assert(p);root=p;}
    ~Temp(){std::filesystem::remove_all(root);} // Only this freshly-created test directory.
};
int main(){
    FakeStore store;j::Journal journal;
    assert(journal.Start(store,1,2,3,Manifest));
    assert(!journal.Start(store,1,2,3,Manifest));
    assert(journal.Advance(store,j::Stage::BeforeRead,4));
    assert(journal.Advance(store,j::Stage::ReadReturned,5));
    const std::array<std::uint8_t,9> bytes{0,8,0,0x34,0x12,0,1,16,0};
    j::cap::Reply reply{};assert(j::cap::Decode(j::cap::VendorCapabilities,bytes.data(),bytes.size(),reply));
    assert(journal.Advance(store,j::Stage::Finished,6,0,reply));
    assert(!journal.Advance(store,j::Stage::Finished,7));
    for(const auto &r:store.records){
        assert(j::Valid(r));
        for(unsigned i=0;i<sizeof r;++i){
            auto broken=r;reinterpret_cast<std::uint8_t *>(&broken)[i]^=1;
            assert(!j::Valid(broken));
        }
    }
    for(unsigned i=1;i<4;++i)assert(j::Follows(store.records[i],store.records[i-1]));
    auto evidence=j::Inspect(store);
    assert(evidence.valid && evidence.complete && evidence.verified_records==4);
    FakeStore empty;assert(j::Inspect(empty).valid && !j::Inspect(empty).verified_records);
    for(unsigned stage=0;stage<4;++stage){
        auto broken=store;broken.records[stage].run^=16;
        broken.records[stage].checksum=j::Checksum(broken.records[stage]);
        // A new valid first record breaks linkage at the second record.
        const auto result=j::Inspect(broken);
        assert(!result.valid && result.invalid_slot==(stage ? stage : 1));
    }
    auto bad=store.records[2];++bad.reader_context;bad.checksum=j::Checksum(bad);
    assert(j::Valid(bad) && !j::Follows(bad,store.records[1]));
    bad=store.records[2];bad.time_ms=1;bad.checksum=j::Checksum(bad);
    assert(!j::Follows(bad,store.records[1]));
    bad=store.records[3];++bad.transport_result;bad.checksum=j::Checksum(bad);assert(!j::Valid(bad));
    bad=store.records[0];bad.reviewed_manifest={};bad.checksum=j::Checksum(bad);assert(!j::Valid(bad));
    for(unsigned mode=0;mode<4;++mode){
        FakeStore s;j::Journal owner;assert(owner.Start(s,1,2,3,Manifest));
        if(mode==0)s.fail=true;
        if(mode==1)s.changed=true;
        assert(!owner.Advance(s,mode==2?j::Stage::ReadReturned:j::Stage::BeforeRead,mode==3?2:4));
        s.fail=s.changed=false;assert(owner.Failed() && !owner.Advance(s,j::Stage::BeforeRead,5));
    }
    FakeStore error_store;j::Journal error;
    assert(error.Start(error_store,1,2,3,Manifest));assert(error.Advance(error_store,j::Stage::BeforeRead,4));
    assert(error.Advance(error_store,j::Stage::ReadReturned,5,91));
    assert(!error.Advance(error_store,j::Stage::Finished,6,0)); // Must retain the driver error.
    // Real exclusive file I/O, failures and process restart at each checkpoint.
    for(unsigned last=0;last<4;++last){
        Temp temp;j::FileStore disk(temp.root.c_str());j::Journal first;
        assert(first.Start(disk,1,2,3,Manifest));
        for(unsigned stage=1;stage<=last;++stage)
            assert(first.Advance(disk,j::Stage(stage),3+stage,0,stage==3 ? reply : j::cap::Reply{}));
        for(unsigned stage=0;stage<=last;++stage)assert(disk.Matches(store.records[stage]));
        const auto evidence=j::Inspect(disk);
        assert(evidence.valid && evidence.verified_records==last+1 && evidence.complete==(last==3));
        j::Journal restarted;assert(!restarted.Start(disk,9,10,11,Manifest));
        assert(!restarted.Advance(disk,j::Stage::BeforeRead,12));
        assert(disk.Matches(first.Latest()));
    }
    for(unsigned failure=0;failure<3;++failure){
        Temp temp;j::FileStore failing(temp.root.c_str(),failure==0 ? ShortWrite : failure==1 ? CorruptWrite : ::write,
                                      failure==2 ? FailSync : ::fsync);
        j::Journal owner;assert(!owner.Start(failing,1,2,3,Manifest));assert(owner.Failed());
        assert(std::filesystem::exists(temp.root+"/00-prepared.bin"));
        j::FileStore normal(temp.root.c_str());j::Journal next;
        assert(!next.Start(normal,9,10,11,Manifest));
        const auto evidence=j::Inspect(normal);
        // Failed fsync can leave a readable complete file. A later read cannot
        // prove that the earlier sync succeeded or that an interaction ran.
        assert(evidence.valid==(failure==2));
    }
    {
        Temp gap;j::FileStore disk(gap.root.c_str());
        assert(disk.Save(store.records[0]) && disk.Save(store.records[2]));
        const auto evidence=j::Inspect(disk);
        assert(!evidence.valid && evidence.verified_records==1 && evidence.invalid_slot==2);
    }
    Temp temp;j::FileStore disk(temp.root.c_str());j::Journal owner;
    assert(owner.Start(disk,1,2,3,Manifest));assert(!disk.Save(store.records[0]));
    assert(disk.Matches(store.records[0]));
    std::puts("PASS observation checkpoints: immutable chain, corruption, read-only gap/partial inspection, no-overwrite, failed-I/O latch and interrupted-run refusal; no transport or hardware");
}
