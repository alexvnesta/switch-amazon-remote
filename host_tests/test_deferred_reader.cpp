// SPDX-License-Identifier: GPL-2.0-only
#include "../tools/amazon-remote-module/control/deferred_reader_io.hpp"
#include "../tools/amazon-remote-module/control/deferred_reader_identity.hpp"
#include <cassert>
#include <vector>
#include <filesystem>
#include <cstdlib>
namespace f=ardeferredreader;
struct Store {
    std::vector<f::j::Checkpoint> records;
    unsigned fail_stage{99},snapshot_calls{},identities{};bool fail_snapshot{},fail_identity{},existing{};
    bool Empty(){return records.empty() && !existing;}
    bool Save(const f::j::Checkpoint &r){if(r.sequence==fail_stage)return false;records.push_back(r);return true;}
    bool Matches(const f::j::Checkpoint &r){return !records.empty() && !std::memcmp(&r,&records.back(),sizeof r);}
    bool SaveSnapshot(const f::Record &r){++snapshot_calls;assert(f::Valid(r));return !fail_snapshot;}
    bool SaveIdentityMatch(std::uint64_t,std::uint64_t){++identities;return !fail_identity;}
};
struct Clock {unsigned time=100;unsigned Now(){return ++time;}};
struct Backend {
    Store &store;unsigned checks{},begins{},reads{},waits{},identity_error{},begin_error{},read_error{};
    bool bad{},permanent_pending{},partial_error{};
    unsigned CheckIdentity(){++checks;assert(store.records.size()==2);return identity_error;}
    unsigned Begin(std::uint64_t nonce){++begins;assert(nonce==9 && store.identities==1);return begin_error;}
    unsigned Read(f::d::Snapshot &out) {
        ++reads;f::d::Session session;assert(session.Begin(9,50,true));
        f::t::Request request{};assert(session.TakeWork(51,request));session.Sent(0,52);
        if(!permanent_pending && reads>1) {
            f::t::Event event{};event.nonce=9;event.record.event.magic=event.record.event.version=0;
            assert(session.Consume(f::t::EventType,&event,80,53));
        }
        out=session.Read(54);
        if(bad || (partial_error && reads>1))out.magic=123;
        return partial_error && reads>1?71:read_error;
    }
    void Wait(){++waits;}
};
constexpr std::array<std::uint8_t,32> manifest{1,2,3};
struct Temp {
    std::string path;
    Temp(){char name[]="/tmp/deferred-reader-XXXXXX";auto p=::mkdtemp(name);assert(p);path=p;}
    ~Temp(){std::filesystem::remove_all(path);}
};
ssize_t ShortWrite(int fd,const void *p,size_t size){return ::write(fd,p,size/2);}
int FailSync(int){return -1;}
int main(){
    for(unsigned fail=0;fail<4;++fail) {
        Store store;store.fail_stage=fail;Backend backend{store};Clock clock;f::Record record;
        assert(f::Collect(store,backend,clock,9,10,manifest,record)==f::Outcome::PersistenceFailed);
        assert(backend.begins==(fail>=2));
    }
    for(unsigned mode=0;mode<10;++mode) {
        Store store;Backend backend{store};Clock clock;f::Record record;
        backend.identity_error=mode==1?99:0;backend.begin_error=mode==2?98:0;backend.read_error=mode==3?97:0;
        backend.bad=mode==4;store.fail_identity=mode==5;store.fail_snapshot=mode==6;
        backend.permanent_pending=mode==7;store.existing=mode==8;backend.partial_error=mode==9;
        const auto result=f::Collect(store,backend,clock,9,10,manifest,record);
        assert(backend.begins<=1 && backend.reads<=120);
        if(mode==5 || mode==6 || mode==8){assert(result==f::Outcome::PersistenceFailed);continue;}
        assert(store.records.size()==4);
        for(unsigned i=0;i<4;++i){assert(f::j::Valid(store.records[i]));if(i)assert(f::j::Follows(store.records[i],store.records[i-1]));}
        assert(result==(mode==0?f::Outcome::Saved:mode==4?f::Outcome::InvalidReplySaved:f::Outcome::ReadErrorSaved));
        if(mode==0 || mode==7 || mode==9)assert(store.snapshot_calls==1 && f::Valid(record));
        else assert(!store.snapshot_calls);
        if(mode==1)assert(!backend.begins);
        if(mode==7)assert(backend.reads==120 && backend.waits==119);
        if(mode==9)assert(record.snapshot.state==f::d::State::Awaiting); // Not partial failed output.
    }
    f::Record record;record.run=9;record.reader_context=10;f::d::Session session;
    assert(session.Begin(9,50,false));record.snapshot=session.Read(51);record.checksum=f::Checksum(record);
    for(unsigned mode=0;mode<3;++mode) {
        Temp temp;f::FileStore disk(temp.path.c_str(),mode==1?ShortWrite: ::write,mode==2?FailSync: ::fsync);
        assert(disk.Empty());assert(disk.SaveSnapshot(record)==(mode==0));
        assert(!disk.Empty() && !disk.SaveSnapshot(record));
        f::j::Journal journal;assert(!journal.Start(disk,11,12,100,manifest));
        assert(std::filesystem::exists(temp.path+"/snapshot.bin"));
    }
    {Temp temp;f::FileStore disk(temp.path.c_str());assert(disk.SaveIdentityMatch(9,10));assert(!disk.Empty());
     assert(!disk.SaveIdentityMatch(9,10));}
    struct Module{std::uint8_t build_id[32]{};std::uint64_t base_address{},size{};};
    std::array<Module,16> modules{};f::BuildId main{1},addon{2};
    std::memcpy(modules[0].build_id,main.data(),32);std::memcpy(modules[1].build_id,addon.data(),32);
    modules[0].base_address=0x123000;modules[0].size=0x2f4000;
    modules[1].base_address=0x417000;modules[1].size=0x3000;
    assert(f::MatchedDriver(modules,2,4,4,main,addon,0x2f4000));
    for(unsigned mode=0;mode<8;++mode) {
        auto bad=modules;int count=2;std::uint64_t after=4;
        if(mode==0){bad[0].build_id[0]^=1;}if(mode==1){bad[1].base_address+=4096;}
        if(mode==2){bad[1].size+=4096;}if(mode==3){count=3;}if(mode==4){after=5;}
        if(mode==5)bad[0].base_address=~std::uint64_t{}-4095;
        if(mode==6){bad[0].base_address++;}if(mode==7){bad[0].size++;}
        assert(!f::MatchedDriver(bad,count,4,after,main,addon,0x2f4000));
    }
    assert(f::MatchedCompanion(modules,1,4,4,main));assert(!f::MatchedCompanion(modules,2,4,4,main));
    modules[1].size=0x4000;
    assert(!f::MatchedDriver(modules,2,4,4,main,addon,0x2f4000));
    assert(f::MatchedDriver(modules,2,4,4,main,addon,0x2f4000,0x4000));
    for(auto size:{0ull,4096ull,0x3001ull,0x4001ull,0x5000ull})
        assert(!f::MatchedDriver(modules,2,4,4,main,addon,0x2f4000,size));
    std::puts("PASS deferred reader: exact metadata/layout gate, checkpoints before request, one Begin, bounded polls, immutable reports and partial failures");
}
