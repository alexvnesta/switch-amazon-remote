// SPDX-License-Identifier: GPL-2.0-only
// G1 storage gate: closed for every power transition, reopened only by an
// acknowledged FullAwake. Synthetic PSC sequences; no device claim.
#include "../mc_mitm/source/amazon_remote/storage_gate.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
using amazon_remote::StorageGate;
namespace {
void Step(StorageGate &g,unsigned state,unsigned request_result=0,unsigned ack_result=0,bool ack=true){
    g.Request(state,request_result);
    assert(!g.Open() || (state==0 && !request_result)); // handlers never see an open gate mid-transition
    if(ack)g.Acknowledge(ack_result);
}
std::string Read(const char *path){std::ifstream in(path);std::stringstream s;s<<in.rdbuf();return s.str();}
}
int main(){
    {   StorageGate g;assert(g.Open());              // boot is FullAwake
        Step(g,0);assert(g.Open());                   // an initial FullAwake keeps it open
        for(unsigned s:{1u,2u,3u}){Step(g,s);assert(!g.Open());} // sleep: closed at every edge
        for(unsigned s:{4u,1u}){Step(g,s);assert(!g.Open());}    // wake edges stay closed
        g.Request(0,0);assert(!g.Open());             // FullAwake request alone does not reopen
        g.Acknowledge(0);assert(g.Open());            // only the acknowledged FullAwake reopens
    }
    {   StorageGate g;Step(g,1);Step(g,0);assert(g.Open()); }      // MinimumAwake and back
    {   StorageGate g;Step(g,5);assert(!g.Open()); }                // shutdown stays closed
    {   StorageGate g;Step(g,0,0xdead,0,false);assert(!g.Open());   // failed getter: no ack
        Step(g,0);assert(g.Open()); }                               // a later good FullAwake recovers
    {   StorageGate g;Step(g,2);g.Request(0,0);g.Acknowledge(0xdead);assert(!g.Open()); } // failed ack
    {   StorageGate g;Step(g,2);g.Request(0,0xdead);g.Acknowledge(0);assert(!g.Open()); } // failed request
    {   StorageGate g;Step(g,6);assert(!g.Open());Step(g,0);assert(g.Open()); }          // unknown state
    // Source contract: the runtime closes the gate before handlers and every
    // maintenance entry point honors it. The runtime itself needs the console SDK.
    const auto src=Read("../mc_mitm/source/amazon_remote/managed_observer_runtime.cpp");
    assert(!src.empty());
    const auto guard="#ifdef ARMODULE_SLEEP_STORAGE_GUARD_G1";
    auto after=[&](const char *anchor,const char *needle){
        const auto a=src.find(anchor);assert(a!=std::string::npos);
        const auto n=src.find(needle,a);const auto g=src.find(guard,a);
        return n!=std::string::npos && g!=std::string::npos && g<n && n-a<400;
    };
    assert(after("void PowerRequest(","g_storage_gate.Request(state,result)"));
    assert(src.find("g_storage_gate.Request(state,result)")<src.find("sleep_resume_trace::PowerRequest(state,flags,result)",src.find("void PowerRequest(")));
    assert(after("void PowerAcknowledge(","g_storage_gate.Acknowledge(result)"));
    assert(after("void PollHandshake()","if(!g_storage_gate.Open())return;"));
    assert(after("void Poll() {","if(!g_storage_gate.Open())return;"));
    const auto poll=src.find("void Poll() {");
    assert(src.find("if(!g_storage_gate.Open())return;",poll)<src.find("amazon_filter_correction::Poll();",poll));
    std::puts("PASS G1 storage gate: closed at every sleep/wake edge and on any failure, reopened only by acknowledged FullAwake; runtime closes it before handlers and gates both maintenance polls ahead of every writer");
}
