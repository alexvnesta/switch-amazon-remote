// SPDX-License-Identifier: GPL-2.0-only
#include "../tools/amazon-remote-module/control/setup_view.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace arsetupview;
void Expect(const Input &input,const char *status,const char *action) {
    const auto v=Select(input);assert(!std::strcmp(v.status,status));
    assert(std::strstr(v.action,action));assert(std::strlen(v.action)<70 && std::strlen(v.note)<70);
}
int main() {
    Input i{};Expect(i,"NOT STARTED","A:");
    i.process_present=true;Expect(i,"WAITING FOR STATUS","Wait");
    i.process_present=false;i.launch_consumed=true;Expect(i,"SETUP NOT READY","FTPD");
    i.fresh=true;Expect(i,"CHECKING STARTUP","15 seconds");
    i.idle_passed=true;Expect(i,"READY TO SEARCH","A:");
    i.state=1;Expect(i,"SEARCHING","REMOTE Home");
    i.link_ready=true;Expect(i,"REMOTE FOUND","ZL:");
    i.state=2;Expect(i,"CONNECTING","Wait");
    i.state=3;i.link_ready=false;Expect(i,"CONNECTION NOT READY","Wait");
    i.pad_attached=true;Expect(i,"RELEASE REMOTE BUTTONS","Release");
    i.remote_neutral=true;Expect(i,"CONNECTED - CHECK NOT SAVED","Y:");
    i.context_requested=true;Expect(i,"SAVING CONNECTION CHECK","Wait");
    i.context_saved=true;Expect(i,"CHECK NOT USABLE","FTPD");
    i.context_usable=true;Expect(i,"CHECK SAVED","Trial NOW");
    assert(Select(i).tone==Tone::Good);
    assert(std::strstr(Select(i).note,"Do NOT press X"));
    // An old scan flag cannot prove an active connection or bypass context.
    i.link_ready=true;Expect(i,"CHECK SAVED","Trial NOW");
    i.link_ready=false;
    i.state=4;Expect(i,"STOPPED","FTPD");
    i.result=0x1759;Expect(i,"STOPPED - DETAILS SAVED","FTPD");
    assert(Select(i).tone==Tone::Error);
    assert(StopAcknowledged(i,11,11));
    assert(!StopAcknowledged(i,10,11));
    i.fresh=false;assert(!StopAcknowledged(i,11,11));i.fresh=true;
    i.result=0;
    i.state=5;Expect(i,"SUSPENDED","FTPD");
    i.result=1;Expect(i,"SETUP ERROR","FTPD");
    i.result=0;i.cleanup=1;Expect(i,"SETUP ERROR","FTPD");
    i.state=6;Expect(i,"DISCONNECTING","Wait up to five seconds");
    assert(!StopAcknowledged(i,11,11));
    i.result=i.cleanup=0;i.pad_attached=i.link_ready=false;i.state=7;
    Expect(i,"DISCONNECTED","FTPD");
    assert(StopAcknowledged(i,11,11) && !StopAcknowledged(i,10,11));
    assert(!StopAcknowledged(i,0,0));
    for(unsigned failure=0;failure<4;++failure) {
        auto bad=i;
        if(failure==0)bad.result=1;
        if(failure==1)bad.cleanup=1;
        if(failure==2)bad.pad_attached=true;
        if(failure==3)bad.link_ready=true;
        assert(SelectDisconnectCheck(bad).tone==Tone::Error);
    }
    i.fresh=false;assert(std::strcmp(SelectDisconnectCheck(i).status,"DISCONNECTED"));
    i.fresh=true;i.state=6;
    assert(!StopAcknowledged(i,11,11));
    for (const auto select : {SelectReconnect,SelectKnownReconnect,SelectScanAssisted,SelectManufacturerScan,SelectStartupTrace}) {
        assert(!std::strcmp(select(i).status,"DISCONNECTING"));
        assert(select(i).tone==Tone::Waiting);
    }
    // A saved historical artifact cannot advance an unready session.
    i.cleanup=0;i.fresh=false;Expect(i,"SETUP NOT READY","FTPD");
    i.fresh=true;i.state=3;i.pad_attached=false;Expect(i,"CONNECTION NOT READY","Wait");
    for (unsigned state=0;state<=5;++state) for (bool fresh:{false,true})
        for (bool failed:{false,true}) for (bool pad:{false,true})
        for (bool found:{false,true}) for (bool saved:{false,true}) {
            Input r{};r.state=state;r.fresh=fresh;r.result=failed;r.pad_attached=pad;
            r.idle_passed=true;r.link_ready=found;r.context_saved=r.context_usable=saved;
            const auto v=SelectReconnect(r);
            assert(std::strlen(v.action)<70 && std::strlen(v.note)<70);
            assert(!std::strstr(v.action,"Trial") && !std::strstr(v.action,"Y:"));
            assert(!std::strstr(v.action,"Hold") && !std::strstr(v.note,"Then hold"));
            if(fresh && !failed && state==3 && pad)
                assert(!std::strcmp(v.status,"CONNECTED - TRY NAVIGATION"));
            if(!fresh || failed || state==4 || state==5)
                assert(!std::strcmp(v.status,Select(r).status));
            const auto known=SelectKnownReconnect(r);
            assert(std::strlen(known.action)<70 && std::strlen(known.note)<70);
            assert(!std::strstr(known.action,"Trial") && !std::strstr(known.action,"Y:"));
            assert(!std::strstr(known.status,"FOUND") && !std::strstr(known.status,"SEARCH"));
            if(fresh && !failed && state==1 && found)
                assert(!std::strcmp(known.status,"READY TO CONNECT") && std::strstr(known.action,"ZL ONCE"));
            if(!fresh || failed || state==4 || state==5)
                assert(!std::strcmp(known.status,Select(r).status));
            const auto scan=SelectScanAssisted(r);
            const auto manufacturer=SelectManufacturerScan(r);
            assert(std::strlen(manufacturer.action)<70 && std::strlen(manufacturer.note)<70);
            assert(!std::strstr(manufacturer.action,"Trial") && !std::strstr(manufacturer.action,"Y:"));
            if(fresh && !failed && (state==0 || (state==1 && !found)))
                assert(std::strstr(manufacturer.note,"Do NOT hold Home") || std::strstr(manufacturer.action,"Do NOT hold Home"));
            else {
                assert(!std::strcmp(manufacturer.status,scan.status));
                assert(!std::strcmp(manufacturer.action,scan.action));
            }
            assert(std::strlen(scan.action)<70 && std::strlen(scan.note)<70);
            assert(!std::strstr(scan.action,"Trial") && !std::strstr(scan.action,"Y:"));
            if(fresh && !failed && state==1)
                assert(found ? std::strstr(scan.action,"ZL:")!=nullptr : std::strstr(scan.action,"REMOTE Home")!=nullptr);
            if(fresh && !failed && state==3 && pad)
                assert(!std::strcmp(scan.status,"CONNECTED - TRY NAVIGATION"));
            if(!fresh || failed || state==4 || state==5)
                assert(!std::strcmp(scan.status,Select(r).status));
        }
    std::puts("PASS manufacturer S2 presentation:192 combinations; ordinary-Up instructions, unchanged fresh-sighting, stale/error/stop guards.");
    std::puts("PASS scan-assisted presentation:192 combinations; flashing instructions, fresh-sighting gate, no Y/trial, unchanged stale/error/stop guards.");
    std::puts("PASS reconnect presentation:192 combinations; no pairing/Y/trial instruction, preserved stale/error/stop guards.");
    std::puts("PASS setup presentation: every phase, prominent context acknowledgement, errors and stale/artifact guards; no API permissions.");
    for(unsigned state=0;state<8;++state)for(bool fresh:{false,true})for(bool failed:{false,true})for(bool found:{false,true}) {
        Input d{};d.fresh=fresh;d.state=state;d.result=failed;d.idle_passed=true;
        d.link_ready=found;d.pad_attached=state==3;d.remote_neutral=true;
        const auto v=SelectDisconnectCheck(d);
        assert(std::strlen(v.action)<70 && std::strlen(v.note)<70);
        assert(!std::strstr(v.action,"Trial") && !std::strstr(v.action,"Y:"));
        if(fresh && !failed && (state==0 || (state==1 && !found)))
            assert(std::strstr(v.action,"REMOTE Home") || std::strstr(v.note,"REMOTE Home"));
        if(state==6)assert(!StopAcknowledged(d,2,2));
        if(fresh && state==7 && !failed && !found)assert(v.tone==Tone::Good);
    }
    std::puts("PASS D1 presentation: flashing discovery, explicit disconnect outcome, no premature/zero-sequence Stop acknowledgement, stale/error/contradictory status rejection.");
}
