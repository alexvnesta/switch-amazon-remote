// SPDX-License-Identifier: GPL-2.0-only
// R3 on the production central runtime. OS/radio/storage are synthetic only.
#define main LegacyCentralMain
#include "test_central_bond_runtime.cpp"
#undef main
cb::WakeStatus Wake(){cb::WakeStatus w;assert(rt::ReadWakeStatus(&w,sizeof w));return w;}
cb::PeerStatus Peer(){cb::PeerStatus s;assert(rt::ReadPeerStatus(&s,sizeof s));return s;}
void PeerReady(u32 nonce){
    assert(rt::PreparePeer(73,{nonce,0,61000}));rt::Poll();
    assert(Peer().phase==cb::PeerPhase::Waiting);
    std::array<u8,1024> e{};const u32 magic=cb::Magic;
    std::memcpy(e.data(),&magic,4);std::copy(peer.begin(),peer.end(),e.begin()+4);
    e[10]=5;std::memcpy(e.data()+12,&nonce,4);e[44]=2;e[46]=1;e[47]=2;
    rt::ConsumePrivate(e.data(),e.size());rt::Poll();assert(Peer().phase==cb::PeerPhase::Ready);
}
cb::WakeRequest Pending(const cb::WakeStatus &w){return {w.serial,w.nonce,w.connection,cb::WakeKindPending,w.owner_epoch};}
int main(int argc,char **argv){
    assert(argc==2);const std::string mode=argv[1];
    Reset();Saved();const auto original=f.committed;
    rt::Initialize(peer);rt::Poll();assert(State().store==cb::Store::Saved);
    assert(!rt::PauseSessionForPower() && !Wake().reserved);
    if(mode=="unprepared"){
        // Peer preparation still queued: no pending wake, sleep stays terminal.
        assert(rt::PreparePeer(73,{300,0,61000}));rt::Poll();assert(Peer().phase==cb::PeerPhase::Waiting);
        assert(!rt::PauseSessionForPower() && !Wake().reserved);
        std::puts("PASS R3 unprepared peer: no pending wake");return 0;
    }
    if(mode=="attempted"){
        // Connected and encrypting, not READY: retires by peer nonce, not session nonce.
        PeerReady(300);Link();assert(rt::Begin(73,{400,4,500}));rt::Poll();
        Event(0,400);rt::Poll();assert(State().phase==cb::Phase::Encrypting);
        assert(rt::PauseSessionForPower() && rt::PauseSessionForPower());
        auto w=Wake();assert(w.reserved==cb::WakeKindPending && w.nonce==300 && !w.connection && !w.owner_epoch && !w.ready);
        assert(rt::ResumeSessionAfterPower() && !Wake().ready); // link still alive
        Link(2);w=Wake();assert(w.ready);
        assert(!rt::ResumeSavedSession(73,{w.serial,400,4,0,500}));
        assert(rt::ResumeSavedSession(73,Pending(w)) && !Wake().reserved);rt::Poll();
        assert(State().store==cb::Store::Saved && State().phase==cb::Phase::Idle && !f.writes && f.committed==original);
        std::puts("PASS R3 attempted-incomplete session retires by peer nonce");return 0;
    }
    for(unsigned i=0;i<8;++i){
        const u32 nonce=300+i;
        assert(State().phase==cb::Phase::Idle && State().store==cb::Store::Saved);
        PeerReady(nonce);
        if(i==7){assert(!rt::PauseSessionForPower() && Wake().remaining==1);break;}
        assert(rt::PauseSessionForPower() && rt::PauseSessionForPower());
        auto w=Wake();
        assert(w.reserved==cb::WakeKindPending && w.nonce==nonce && !w.connection && !w.owner_epoch && w.serial==i+1 && !w.ready);
        assert(rt::ResumeSessionAfterPower());w=Wake();assert(w.ready);
        const auto request=Pending(w);
        assert(!rt::ResumeSavedSession(99,request));
        for(unsigned field=0;field<5;++field){auto bad=request;
            if(field==0){++bad.serial;}if(field==1){++bad.nonce;}if(field==2){bad.connection=4;}
            if(field==3){bad.owner_epoch=1;}if(field==4){bad.reserved=0;}
            assert(!rt::ResumeSavedSession(73,bad));}
        if(mode=="stop"){rt::Cancel(73);assert(!Wake().ready && !rt::ResumeSavedSession(73,request));
            std::puts("PASS R3 Cancel stops a pending wake");return 0;}
        if(mode=="shutdown"){rt::Suspend();assert(!Wake().ready && !rt::ResumeSavedSession(73,request));
            std::puts("PASS R3 terminal suspend stops a pending wake");return 0;}
        if(mode=="live"){Link();assert(!Wake().ready && !rt::ResumeSavedSession(73,request));Link(2);}
        assert(rt::ResumeSavedSession(73,request) && !rt::ResumeSavedSession(73,request));
        assert(!Wake().reserved && !Wake().ready); // consumed: plain kind while reloading
        assert(State().store==cb::Store::Unknown && State().phase==cb::Phase::Idle && Peer().phase==cb::PeerPhase::Idle);
        rt::Poll();assert(State().store==cb::Store::Saved && State().generation==1);
        if(mode=="mixed" && i==1){
            // A completed session after pending ones keeps the unchanged A3 wire.
            PeerReady(700);Link();assert(rt::Begin(73,{701,4,702}));rt::Poll();
            Event(0,701);rt::Poll();Event(4,701);rt::Poll();assert(State().phase==cb::Phase::Ready);
            assert(rt::PauseSessionForPower());auto a=Wake();assert(!a.reserved && a.nonce==701);
            Link(2);assert(rt::ResumeSessionAfterPower());a=Wake();assert(a.ready);
            assert(!rt::ResumeSavedSession(73,Pending(a)));
            assert(rt::ResumeSavedSession(73,{a.serial,a.nonce,a.connection,0,a.owner_epoch}));rt::Poll();
            assert(State().store==cb::Store::Saved && !f.writes && f.committed==original);
            std::puts("PASS R3 mixed pending/completed sessions share one budget; A3 wire unchanged");return 0;
        }
        time_ms+=1000;
    }
    assert(!f.writes && !f.commits && !creates && original==f.committed);
    std::printf("PASS R3 production central %s: pending attempts retire by peer nonce, kind/identity refusal, ACK-gated resume, budget reserve; synthetic only\n",mode.c_str());
}
