// SPDX-License-Identifier: GPL-2.0-only
#include "../mc_mitm/source/amazon_remote/legacy_encryption_request.hpp"
#include <cassert>
#include <cstdio>
namespace e=amazon_remote::bond::encryption;
struct Backend {
    std::uint64_t now{100};
    bool admit{true},owns{true},encrypted{},inline_callback{};
    std::uint8_t result{1},callback_result{},transport{2};
    e::Address peer{{1,2,3,4,5,6}};
    e::Callback callback{};void *reference{};unsigned calls{};
    std::uint64_t Now(){return now;}
    bool Admit(const e::Target &){return admit;}
    bool Owns(const e::Target &){return owns;}
    bool Encrypted(const e::Target &){return encrypted;}
    std::uint8_t SetEncryption(const std::uint8_t *p,std::uint8_t t,e::Callback cb,void *ref) {
        assert(t==2 && *static_cast<std::uint8_t*>(ref)==1);
        for(unsigned i=0;i<6;++i)assert(p[i]==peer[i]);
        ++calls;callback=cb;reference=ref;if(inline_callback)Finish();return result;
    }
    void Finish(){assert(callback);callback(peer.data(),transport,reference,callback_result);}
};
e::Target Target(){return {{{1,2,3,4,5,6}},1,2,3,0,16,false};}
int main(){
    using R=e::Request<Backend>;using S=e::State;
    {Backend b;R r(b);assert(r.Begin(Target())==S::Waiting && !r.TakeReady());
     b.encrypted=true;b.Finish();assert(r.GetState()==S::Complete && r.TakeReady());
     b.callback_result=10;b.Finish();assert(r.CompletionResult()==0);
     assert(!r.TakeReady() && r.Begin(Target())==S::Consumed && b.calls==1);}
    for(unsigned result:{0u,1u}){
        Backend b;b.result=result;b.encrypted=true;b.inline_callback=true;R r(b);
        assert(r.Begin(Target())==S::Complete && r.TakeReady());
    }
    for(unsigned result=2;result<256;++result){
        Backend b;b.result=result;b.callback_result=result;b.inline_callback=true;R r(b);
        assert(r.Begin(Target())==S::Rejected && !r.TakeReady() && b.calls==1);
        b.encrypted=true;b.callback_result=0;b.Finish();assert(!r.TakeReady());
    }
    {Backend b;b.result=0;R r(b);assert(r.Begin(Target())==S::Rejected);}
    {Backend b;b.now=0;R r(b);assert(r.Begin(Target())==S::Rejected && !b.calls);}
    for(unsigned failure=0;failure<8;++failure){
        Backend b;R r(b);assert(r.Begin(Target())==S::Waiting);b.encrypted=true;
        if(failure==0)b.peer[0]^=1;if(failure==1)b.transport=1;
        if(failure==2)b.callback_result=10;if(failure==3)b.owns=false;
        if(failure==4)b.encrypted=false;if(failure==5)b.now=99;
        if(failure==6)b.now=15100;if(failure==7)r.Cancel();
        b.Finish();assert(!r.TakeReady());const auto terminal=r.GetState();
        b.peer=Target().peer;b.transport=2;b.callback_result=0;b.owns=true;b.encrypted=true;b.now=101;
        b.Finish();assert(r.GetState()==terminal && !r.TakeReady());
        r.Begin(Target());assert(b.calls==1);
    }
    for(unsigned failure=0;failure<3;++failure){
        Backend b;R r(b);r.Begin(Target());b.encrypted=true;b.Finish();assert(r.GetState()==S::Complete);
        if(failure==0)b.owns=false;if(failure==1)b.encrypted=false;if(failure==2)b.now=15100;
        assert(!r.TakeReady());
    }
    for(unsigned bad=0;bad<11;++bad){
        Backend b;R r(b);auto t=Target();std::uint64_t timeout=15000;
        if(bad==0)t.driver_instance=0;if(bad==1)t.connection_epoch=0;
        if(bad==2)t.record_generation=0;if(bad==3)t.peer.fill(0);
        if(bad==4)t.peer.fill(255);if(bad==5)t.hci_handle=0xeff+1;
        if(bad==6)t.minimum_key_bytes=6;if(bad==7)t.minimum_key_bytes=17;
        if(bad==8)timeout=0;if(bad==9)timeout=30001;if(bad==10)b.admit=false;
        assert(r.Begin(t,timeout)==S::Rejected && b.calls==0);
    }
    {Backend b;b.now=~std::uint64_t{}-10;R r(b);assert(r.Begin(Target())==S::Rejected && !b.calls);}
    {Backend b;R r(b);r.Begin(Target());b.now=15100;assert(r.Poll()==S::Expired);
     b.encrypted=true;b.Finish();assert(r.GetState()==S::Expired);}
    std::puts("PASS private encryption request: inline/delayed callbacks,254 native errors,stale ownership,wrong target/transport,expiry,cancellation,one-shot GATT admission. No production backend or hardware proof.");
}
