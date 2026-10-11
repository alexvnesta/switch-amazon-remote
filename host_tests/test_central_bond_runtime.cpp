// SPDX-License-Identifier: GPL-2.0-only
// Execute the production runtime. OS/radio/storage are explicitly synthetic.
#define BOND_SAVE_FIXTURE_ONLY
#include "test_bond_save_io.cpp"
#include "bond_host_sha256.hpp"
#include "../mc_mitm/source/amazon_remote/central_bond_runtime.cpp"
namespace rt=ams::mc::central_bond_runtime;
namespace cb=amazon_remote::central_bond;
namespace record=amazon_remote::bond::record;
namespace legacy=amazon_remote::bond::legacy;
namespace {
u64 time_ms=1000;
unsigned creates{},send_failure{};
bool capture_live=true;
std::vector<std::array<u8,44>> commands;
constexpr legacy::Address peer{6,5,4,3,2,1},local{2,3,4,5,6,7};
cb::Status State(){cb::Status s;assert(rt::ReadStatus(&s,sizeof s));return s;}
void Event(unsigned kind,unsigned nonce=23,unsigned code=0){
    std::array<u8,1024> e{};const u32 magic=cb::Magic;
    std::memcpy(e.data(),&magic,4);std::copy(peer.begin(),peer.end(),e.begin()+4);
    e[10]=kind;e[11]=kind==0?1:code;std::memcpy(e.data()+12,&nonce,4);
    if(kind==1){for(unsigned i=0;i<26;++i)e[16+i]=8+i;e[42]=1;e[43]=16;}
    e[44]=0x23;e[45]=1;if(kind==4){e[46]=1;e[47]=16;}
    rt::ConsumePrivate(e.data(),e.size());assert(std::all_of(e.begin(),e.end(),[](auto b){return b==0;}));
}
void Link(unsigned state=0){std::array<u8,20> e{};e[4]=state;e[5]=4;e[8]=4;std::copy(peer.begin(),peer.end(),e.begin()+12);rt::ObserveBle(4,e.data(),e.size());}
record::Binding Binding(){record::Binding b{};b.peer=peer;b.local=local;b.profile=0x314e4543;
    std::array<u8,16> ns{'A','R','-','C','E','N','1',0};std::copy(local.begin(),local.end(),ns.begin()+8);
    sha256CalculateHash(b.controller_context.data(),ns.data(),ns.size());return b;}
void Saved(){std::array<u8,36> e{};std::copy(peer.begin(),peer.end(),e.begin());e[6]=1;
    for(unsigned i=0;i<26;++i)e[8+i]=8+i;e[34]=1;e[35]=16;
    legacy::Material key;assert(legacy::Decode(legacy::Layout::AuditedC91Legacy,peer,e,key));
    record::Buffer out;record::Materials material{&key,nullptr,nullptr,nullptr,nullptr};
    assert(record::Build(Binding(),1,material,1,16,false,out));std::copy(out.Read().begin(),out.Read().end(),f.committed.begin());f.pending=f.committed;
}
}
u64 armGetSystemTick(){return time_ms;}
u64 armTicksToNs(u64 ticks){return ticks*1000000;}
Result fsInitialize(){return 0;}
Result pminfoGetProgramId(u64 *out,u64 pid){*out=pid==73?OwnerProgramId:99;return 0;}
Result btdrvGetAdapterProperty(int kind,BtdrvAdapterProperty *out){assert(kind==BtdrvAdapterPropertyType_Address);std::copy(local.begin(),local.end(),out->data);return 0;}
Result btdrvWriteHidData2(BtdrvAddress address,const void *p,std::size_t n){assert(n==44 && !legacy::Nonzero(*reinterpret_cast<legacy::Address*>(&address)));
    std::array<u8,44> a{};std::memcpy(a.data(),p,n);commands.push_back(a);return send_failure?0xdead:0;}
Result fsCreateSaveDataFileSystem(const FsSaveDataAttribute *a,const FsSaveDataCreationInfo *c,const FsSaveDataMetaInfo *m){
    assert(SameAttribute(*a) && c->owner_id==OwnerProgramId && c->save_data_space_id==FsSaveDataSpaceId_User &&
        c->save_data_size==0x10000 && c->journal_size==0x10000 && c->available_size==0x4000 && !m->size && f.fault==Fault::Missing);
    ++creates;f.fault=Fault::None;f.committed={};f.pending={};return 0;
}
Result fsFsCreateFile(FsFileSystem *fs,const char *name,s64 n,u32 flags){assert(fs->handle && !fs->readonly && !std::strcmp(name,SlotPath) && n==512 && !flags && creates==1);return 0;}
bool ams::mc::amazon_observer::ReadCaptureStatus(void *out,std::size_t size){
    using namespace amazon_remote;
    assert(size==sizeof(CaptureLeaseWire));
    CaptureLeaseWire w{CaptureLeaseMagic,CaptureLeaseVersion,unsigned(capture_live?CaptureLeaseState::Active:CaptureLeaseState::Ended),0,time_ms,61000};
    std::memcpy(out,&w,size);return true;
}
int main(int argc,char **argv){
    assert(argc==2);const std::string mode=argv[1];const bool saved=mode=="reconnect" || mode=="reconnect-fail" || mode.starts_with("peer-");
    Reset(saved?Fault::None:Fault::Missing);if(saved)Saved();
    if(mode=="invalid-record"){Reset();}
    rt::Initialize(peer);rt::Poll();
    if(mode=="invalid-record"){assert(State().store==cb::Store::Error && commands.empty() && !f.writes);return 0;}
    assert(State().store==(saved?cb::Store::Saved:cb::Store::Empty) && !creates && commands.empty());
#ifdef ARMODULE_NATIVE_HCI_C2
    cb::HciTrace initial{};assert(rt::ReadHciTrace(&initial,sizeof initial) && initial.supported && !initial.count);
    assert(!rt::ReadHciTrace(nullptr,sizeof initial) && !rt::ReadHciTrace(&initial,sizeof initial-1));
    if(mode=="c2-empty"){Link();assert(!rt::Begin(73,{23,4,99}) && commands.empty() && !f.writes);return 0;}
#endif
    if(mode.starts_with("peer-")){
        auto state=[] {cb::PeerStatus s;assert(rt::ReadPeerStatus(&s,sizeof s));return s;};
        const auto original=f.committed;
        assert(!rt::PreparePeer(99,{23,0,61000}));
        assert(!rt::PreparePeer(73,{0,0,61000}) && !rt::PreparePeer(73,{23,1,61000}));
        assert(!rt::PreparePeer(73,{23,0,62000}));
        capture_live=false;assert(!rt::PreparePeer(73,{23,0,61000}));capture_live=true;
        if(mode=="peer-live"){Link();assert(!rt::PreparePeer(73,{23,0,61000}));return 0;}
        assert(rt::PreparePeer(73,{23,0,61000}) && !rt::PreparePeer(73,{24,0,61000}));
        assert(state().phase==cb::PeerPhase::Queued && !rt::PauseUnusedForPower());
        if(mode=="peer-cancel-queued"){
            rt::Cancel(99);assert(state().phase==cb::PeerPhase::Queued);
            rt::Cancel(73);rt::Poll();assert(commands.empty() && state().phase==cb::PeerPhase::Cancelled);return 0;
        }
        if(mode=="peer-send-fail")send_failure=1;
        rt::Poll();assert(commands.size()==1 && commands[0][0]==10);
        assert(std::all_of(commands[0].begin()+12,commands[0].end(),[](auto v){return v==0;}));
        if(mode=="peer-send-fail"){assert(state().phase==cb::PeerPhase::Failed);return 0;}
        assert(state().phase==cb::PeerPhase::Waiting);
        std::array<u8,1024> e{};const u32 magic=cb::Magic,nonce=mode=="peer-wrong-nonce"?24:23;
        std::memcpy(e.data(),&magic,4);std::copy(peer.begin(),peer.end(),e.begin()+4);
        e[10]=5;std::memcpy(e.data()+12,&nonce,4);e[44]=2;e[46]=mode=="peer-existing"?1:0;
#ifdef ARMODULE_NATIVE_HCI_C2
        e[47]=mode=="peer-old-bridge"?0:2;
#endif
        if(mode=="peer-native-fail")e[11]=0xf0;
        if(mode=="peer-secret-rejected")e[16]=1;
        if(mode=="peer-wrong-type")e[44]=1;
        if(mode=="peer-wrong-address-type")e[45]=1;
        if(mode=="peer-invalid-existed")e[46]=2;
        if(mode=="peer-wrong-target")e[4]^=1;
        if(mode=="peer-timeout")time_ms+=3000;
        if(mode=="peer-lease-end")capture_live=false;
        if(mode=="peer-suspend")rt::Suspend();
        if(mode=="peer-link-event")Link();
        rt::ConsumePrivate(e.data(),e.size());assert(std::all_of(e.begin(),e.end(),[](auto v){return v==0;}));
        rt::Poll();
        if(mode=="peer-wrong-nonce" || mode=="peer-wrong-target"){
            assert(state().phase==cb::PeerPhase::Waiting);time_ms+=3000;rt::Poll();
        }
        const bool success=mode=="peer-new" || mode=="peer-existing" || mode=="peer-ready-expired" || mode=="peer-ready-ended" || mode=="peer-hci";
        assert(state().phase==(success?cb::PeerPhase::Ready:mode=="peer-suspend"?cb::PeerPhase::Cancelled:cb::PeerPhase::Failed));
        assert(!creates && !f.writes && !f.commits && f.committed==original && commands.size()==1);
        assert(State().phase==cb::Phase::Idle && !rt::PreparePeer(73,{24,0,61000}));
#ifdef ARMODULE_NATIVE_HCI_C2
        if(mode=="peer-hci"){
            auto hci=[] {cb::HciTrace t;assert(rt::ReadHciTrace(&t,sizeof t));return t;};
            auto send_hci=[&] {std::array<u8,1024> packet{};const u32 opcode=0x200d;
                std::memcpy(packet.data(),&magic,4);std::copy(peer.begin(),peer.end(),packet.begin()+4);
                packet[10]=6;packet[11]=12;std::memcpy(packet.data()+20,&opcode,4);
                rt::ConsumePrivate(packet.data(),packet.size());assert(std::all_of(packet.begin(),packet.end(),[](auto v){return !v;}));};
            send_hci();assert(hci().count==1 && hci().entries[0].status==12 && hci().window_nonce==23);
            capture_live=false;send_hci();assert(hci().count==1);capture_live=true;
            rt::Cancel(73);send_hci();assert(hci().count==1 && !f.writes && f.committed==original);return 0;
        }
#endif
        if(mode=="peer-ready-expired" || mode=="peer-ready-ended"){
            if(mode=="peer-ready-expired")time_ms=61000;else capture_live=false;
            assert(state().phase==cb::PeerPhase::Failed && !f.writes && commands.size()==1);
        }
        rt::Cancel(73);assert(state().phase==cb::PeerPhase::Cancelled);
        std::puts("C1 production peer preparation runtime passed; synthetic only");return 0;
    }
    if(mode=="idle-power" || mode=="power-live" || mode=="power-terminal") {
        assert(rt::PauseUnusedForPower() && rt::PauseUnusedForPower());
        const auto calls=f.calls;rt::Poll();assert(f.calls==calls && commands.empty() && !f.writes);
        if(mode=="power-live"){Link();assert(!rt::ResumeUnusedAfterPower());assert(!rt::Begin(73,{23,4,99}));return 0;}
        if(mode=="power-terminal"){rt::Suspend();assert(!rt::PauseUnusedForPower() && !rt::ResumeUnusedAfterPower());return 0;}
        assert(rt::ResumeUnusedAfterPower() && !rt::ResumeUnusedAfterPower());
    }
    assert(!rt::Begin(73,{23,4,99}));Link();assert(!rt::Begin(99,{23,4,99}));
    assert(!rt::Begin(73,{23,5,99}));assert(rt::Begin(73,{23,4,99}));assert(!rt::Begin(73,{24,4,99}));
    if(mode=="power-attempted"){assert(!rt::PauseUnusedForPower() && !rt::ResumeUnusedAfterPower());rt::Suspend();assert(!rt::Begin(73,{24,4,99}));return 0;}
    rt::Poll();assert(commands.size()==1 && commands[0][0]==8);
    Event(0,24);assert(State().phase==cb::Phase::Hello);Event(0);rt::Poll();
    assert(commands.size()==2 && commands[1][0]==(saved?7:9) && State().phase==cb::Phase::Encrypting);
    if(mode=="cancel"){rt::Cancel(73);Event(1);Event(4);rt::Poll();assert(State().phase==cb::Phase::Cancelled && !creates && !f.writes);return 0;}
    if(mode=="disconnect"){Link(2);Event(1);Event(4);rt::Poll();assert(State().phase==cb::Phase::Failed && !creates && !f.writes);return 0;}
    if(mode=="timeout"){time_ms+=31000;Event(1);Event(4);rt::Poll();assert(State().phase==cb::Phase::Failed && !creates && !f.writes);return 0;}
    if(mode=="suspend"){rt::Suspend();Event(1);Event(4);rt::Poll();assert(State().phase==cb::Phase::Cancelled && !creates && !f.writes);return 0;}
    if(!saved && mode!="missing-key")Event(1);
    if(mode=="duplicate-key"){Event(1);Event(4);rt::Poll();assert(State().phase==cb::Phase::Failed && !creates && !f.writes);return 0;}
    Event(4,23,mode=="reconnect-fail"?5:0);rt::Poll();
    if(mode=="missing-key" || mode=="reconnect-fail"){assert(State().phase==cb::Phase::Failed && !creates && !f.writes);return 0;}
    assert(State().phase==cb::Phase::Ready && State().store==cb::Store::Saved && State().encrypted && State().key_bytes==16);
    assert(creates==(saved?0u:1u) && f.commits==(saved?0u:2u));
    record::Metadata meta{};assert(record::Inspect(f.committed,Binding(),16,meta) && meta.occupied && meta.mask==1);
    Link(2);rt::Cancel(73);rt::Poll();assert(!rt::Begin(73,{24,4,99}));Closed();
    std::puts("B1 integrated synthetic runtime passed");
    return 0;
}
