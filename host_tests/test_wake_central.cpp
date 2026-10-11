// SPDX-License-Identifier: GPL-2.0-only
#define main LegacyCentralMain
#include "test_central_bond_runtime.cpp"
#undef main
cb::WakeStatus Wake(){cb::WakeStatus w;assert(rt::ReadWakeStatus(&w,sizeof w));return w;}
int main(int argc,char **argv){
    assert(argc==2);const std::string mode=argv[1];
    Reset();Saved();const auto original=f.committed;
    rt::Initialize(peer);rt::Poll();assert(State().store==cb::Store::Saved);
    assert(!rt::PauseSessionForPower() && !rt::ResumeSessionAfterPower());
    amazon_remote::bond::encryption::Callback old_cb{};void *old_ref{};
    for(unsigned i=0;i<8;++i){
        const u32 nonce=100+i;const u64 epoch=200+i;
        assert(State().phase==cb::Phase::Idle && State().store==cb::Store::Saved);
        Link();
        if(i)assert(!rt::Begin(73,{nonce-1,4,epoch}));
        assert(rt::Begin(73,{nonce,4,epoch}));rt::Poll();
        assert(commands.back()[0]==8);
        if(i){Event(0,nonce-1);Event(4,nonce-1);assert(State().phase==cb::Phase::Hello);}
        Event(0,nonce);rt::Poll();assert(State().phase==cb::Phase::Encrypting && commands.back()[0]==7);
        if(old_cb){old_cb(peer.data(),2,old_ref,0);rt::Poll();assert(State().phase==cb::Phase::Encrypting);}
        assert(!rt::PauseSessionForPower());
        if(mode=="pending-stop"){rt::Suspend();Event(4,nonce);rt::Poll();assert(!rt::ResumeSessionAfterPower() && !Wake().ready);return 0;}
        Event(4,nonce);rt::Poll();assert(State().phase==cb::Phase::Ready);
        old_cb=rt::callback;old_ref=rt::reference;
        assert(!f.writes && !f.commits && !creates && original==f.committed);
        if(i==7){assert(!rt::PauseSessionForPower() && !Wake().remaining);break;}
        assert(rt::PauseSessionForPower() && rt::PauseSessionForPower());
        const auto before=commands.size();rt::Poll();assert(commands.size()==before);
        assert(!Wake().ready && Wake().serial==i+1);
        Link(2);assert(!Wake().ready);assert(rt::ResumeSessionAfterPower());
        auto w=Wake();assert(w.ready && w.nonce==nonce && w.owner_epoch==epoch);
        cb::WakeRequest request{w.serial,w.nonce,w.connection,0,w.owner_epoch};
        assert(!rt::ResumeSavedSession(99,request));
        for(unsigned field=0;field<5;++field){auto bad=request;
            if(field==0){++bad.serial;}if(field==1){++bad.nonce;}if(field==2){++bad.connection;}
            if(field==3){++bad.owner_epoch;}if(field==4){++bad.reserved;}
            assert(!rt::ResumeSavedSession(73,bad));}
        if(mode=="stop"){rt::Cancel(73);assert(!Wake().ready && !rt::ResumeSavedSession(73,request));return 0;}
        if(mode=="shutdown"){rt::Suspend();assert(!Wake().ready && !rt::ResumeSavedSession(73,request));return 0;}
        if(mode=="live"){Link();assert(!Wake().ready && !rt::ResumeSavedSession(73,request));Link(2);}
        assert(rt::ResumeSavedSession(73,request));assert(!rt::ResumeSavedSession(73,request));
        assert(State().store==cb::Store::Unknown && State().phase==cb::Phase::Idle);
        if(mode=="bad-save")f.committed[0]^=1;
        rt::Poll();
        if(mode=="bad-save"){assert(State().store==cb::Store::Error && !f.writes);return 0;}
        assert(State().store==cb::Store::Saved && State().generation==1);
        time_ms+=1000;
    }
    assert(!f.writes && !f.commits && !creates && original==f.committed);
    std::puts("PASS production central A3 eight-session wake replay, retained callbacks, old-nonce rejection, saved-record preservation; synthetic only");
}
