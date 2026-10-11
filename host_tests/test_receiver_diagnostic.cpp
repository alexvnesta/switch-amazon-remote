#include "../mc_mitm/source/amazon_remote/receiver_diagnostic.hpp"
#include <cassert>
#include <cstdio>
#include <thread>
using namespace amazon_remote;
struct Storage {
    unsigned creates{},writes{},reads{};bool occupied{},fail_write{},bad_read{};
    ReceiverRecord record{};
    bool Create(std::size_t size){++creates;assert(size==88);if(occupied)return false;occupied=true;return true;}
    bool Write(const ReceiverRecord &r){++writes;if(fail_write){record.magic=r.magic;return false;}record=r;return true;}
    bool Read(ReceiverRecord &r){++reads;r=record;if(bad_read)++r.counts[0];return true;}
};
int main(){
    SightingAddress target{1,2,3,4,5,6};std::array<std::uint8_t,328> valid{};
    valid[4]=2;std::copy(target.begin(),target.end(),valid.begin()+7);
    unsigned cases{};
    for(unsigned stage=0;stage<11;++stage){
        ReceiverDiagnostic counter;auto raw=valid;auto type=6u;const std::uint8_t *data=raw.data();auto size=raw.size();
        switch(stage){case 0:break;case 1:type=4;break;case 2:size=327;break;case 3:data=nullptr;break;
            case 4:raw[0]=1;break;case 5:raw[4]=1;break;case 6:raw[5]=3;break;case 7:raw[6]=2;break;
            case 8:raw[7]^=1;break;case 9:raw[323]=11;break;case 10:raw[5]=0;break;}
        const auto before=raw;
        counter.Observe(type,data,size,target,false);assert(counter.Read()==ReceiverCounts{});
        counter.Observe(type,data,size,target,true);auto counts=counter.Read();assert(counts[0]==1 && raw==before);
        const ReceiverCount wanted[]={ReceiverCount::Eligible,ReceiverCount::OtherType,ReceiverCount::ShortInput,
            ReceiverCount::ShortInput,ReceiverCount::ResultError,ReceiverCount::StatusRejected,ReceiverCount::DeviceRejected,
            ReceiverCount::AddressTypeRejected,ReceiverCount::AddressMismatch,ReceiverCount::CountRejected,ReceiverCount::Eligible};
        assert(counts[std::size_t(wanted[stage])]==1);++cases;
    }
    std::uint32_t random=1;
    for(unsigned i=0;i<4096;++i){
        auto raw=valid;random=random*1664525u+1013904223u;
        raw[i%13]=random>>24;raw[323]=(random>>16)%16;
        ReceiverDiagnostic counter;counter.Observe(6,raw.data(),raw.size(),target,true);
        assert(bool(counter.Read()[std::size_t(ReceiverCount::Eligible)])==sighting_detail::TargetView(raw.data(),target));
    }
    for(unsigned fail=0;fail<4;++fail){
        ReceiverRecordWriter writer;Storage storage;storage.occupied=fail==1;storage.fail_write=fail==2;storage.bad_read=fail==3;
        assert(!writer.Save(100,101,1,{},storage) && !storage.creates);
        assert(writer.Save(100,60100,2,{},storage)==(fail==0));
        assert(!writer.Save(100,60101,2,{},storage) && storage.creates==1);
        if(fail==0){auto record=storage.record;assert(ValidReceiverRecord(record));
            for(unsigned i=0;i<88;++i){auto changed=record;reinterpret_cast<std::uint8_t *>(&changed)[i]^=1;assert(!ValidReceiverRecord(changed));}}
    }
    ReceiverDiagnostic concurrent;std::atomic<bool> done{};
    std::thread producer([&]{for(unsigned i=0;i<100000;++i)concurrent.Observe(6,valid.data(),valid.size(),target,true);done=true;});
    while(!done.load())(void)concurrent.Read();producer.join();assert(concurrent.Read()[0]==100000);
    std::printf("PASS %u receiver stages,4096 TargetView-equivalence cases,4 exclusive storage/partial cases,88 corruption cases and concurrent atomic reads; no radio\n",cases);
}
