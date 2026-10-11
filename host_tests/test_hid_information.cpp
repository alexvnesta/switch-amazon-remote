// SPDX-License-Identifier: GPL-2.0-only
#define main LegacyAdapterMain
#include "test_libnx_input_adapter.cpp"
#undef main
void ParserAndSelection() {
    HidInformation info{};
    for (unsigned flags=0;flags<256;++flags) {
        const u8 value[]{0x11,0x01,7,u8(flags)};
        assert(DecodeHidInformation(value,sizeof value,info));
        assert(info.bcd_hid==0x111 && info.country==7 && info.flags==flags);
        assert(info.RemoteWake()==bool(flags&1) && info.NormallyConnectable()==bool(flags&2));
        assert(info.OtherFlags()==(flags&0xfc));
    }
    const auto before=info.flags;
    const u8 value[5]{};
    for(auto size:{0u,1u,2u,3u,5u})assert(!DecodeHidInformation(value,size,info) && info.flags==before);
    assert(!DecodeHidInformation(nullptr,4,info));
    const Attribute hid{Kind::Service,0x25,0x45,0x1812,0,0,true};
    Attribute records[]{ {Kind::Characteristic,0x28,0,0x2a4a,0,0,false} },out{};
    assert(SelectHidInformation(hid,records,1,out) && out.handle==0x28 && !out.properties);
    auto bad=hid;bad.primary=false;assert(!SelectHidInformation(bad,records,1,out));
    assert(!SelectHidInformation(hid,nullptr,1,out));
    assert(!SelectHidInformation(hid,records,100,out));
    records[0].handle=0x25;assert(!SelectHidInformation(hid,records,1,out));
    records[0].handle=0x46;assert(!SelectHidInformation(hid,records,1,out));
    records[0].handle=0x28;records[0].kind=Kind::Descriptor;assert(!SelectHidInformation(hid,records,1,out));
}
void ConnectH1(LibnxSession &r,unsigned mode,bool resumed=false) {
    f=FakeState{};f.system=f.native_requests=true;f.bond.generation=7;f.info_mode=mode;
    if(resumed)f.now=30000;
    if(mode==7)f.info_flags=0x81;
    assert(!r.InitializeIdentityTest(f.target));assert(!r.PrepareKnownReconnect(f.now,f.now+60000));
    f.peer.phase=central_bond::PeerPhase::Ready;f.peer.device_type=2;f.peer.address_type=f.peer.existed=0;
    f.now+=100;assert(!r.Poll(f.now));assert(!r.ConfirmKnownReconnect(f.now,f.peer.capture_deadline_ms));
    f.context_active=true;assert(!Advance(r,200));assert(!r.Ready() && !f.info_reads);
    f.bond.phase=central_bond::Phase::Ready;f.bond.encrypted=1;f.bond.key_bytes=16;f.bond.native_status=0;
    assert(!Advance(r) && r.Ready() && !f.info_reads);
}
void Transport(unsigned mode) {
    Sandbox sandbox;LibnxSession r(Log,true,true,true,true,true,true);ConnectH1(r,mode);
    const auto getters=f.descriptor_getters,reads=f.security_reads,writes=f.writes;
    const u8 home[]{0x23,2,0,0},release[]{0,0,0,0};
    if(mode==8) {Queue(4,0x1812,0x2a4d,0,home,sizeof home);assert(!r.Poll(f.now));}
    f.now+=4999;assert(!r.Poll(f.now) && !f.info_reads);
    f.now+=1;assert(!r.Poll(f.now));
    if(mode==8) {
        assert(!f.info_reads && r.Buttons()==Home);
        Queue(4,0x1812,0x2a4d,0,release,sizeof release);f.now+=100;assert(!r.Poll(f.now));
    }
    if(mode==1 || mode==2 || mode==9) assert(!f.info_reads && Logged("HID_INFO_H1_UNAVAILABLE"));
    else assert(f.info_reads==1);
    f.now+=100;assert(!r.Poll(f.now) && r.Ready());
    if(mode==0 || mode==8) assert(Logged("RemoteWake=1 NormallyConnectable=1 other_flags=00; host_wake_proven=0"));
    if(mode==7) assert(Logged("RemoteWake=1 NormallyConnectable=0 other_flags=80; host_wake_proven=0"));
    if(mode==3 || mode==6) assert(Logged("HID_INFO_H1_NO_VALUE") && !Logged("HID_INFO_H1_VALUE"));
    if(mode==4) {
        const u8 value[]{0x11,1,0,3};
        Queue(0,0x1812,0x2a4a,0,value,4,0,5);f.now+=100;
        assert(!r.Poll(f.now) && !Logged("HID_INFO_H1_VALUE"));
        Queue(4,0x1812,0x2a4d,0,home,sizeof home);f.now+=100;
        assert(!r.Poll(f.now) && r.Buttons()==Home);
        Queue(4,0x1812,0x2a4d,0,release,sizeof release);f.now+=100;
        assert(!r.Poll(f.now) && !r.Buttons());
        f.now+=3000;assert(!r.Poll(f.now) && Logged("HID_INFO_H1_TIMEOUT"));
        Queue(0,0x1812,0x2a4a,0,value,4);
        f.now+=100;assert(!r.Poll(f.now) && Logged("HID_INFO_H1_IGNORED") && !Logged("HID_INFO_H1_VALUE"));
    }
    if(mode==5) assert(!Logged("HID_INFO_H1_VALUE"));
    Queue(4,0x1812,0x2a4d,0,home,sizeof home);f.now+=100;assert(!r.Poll(f.now) && r.Buttons()==Home);
    Queue(4,0x1812,0x2a4d,0,release,sizeof release);f.now+=100;assert(!r.Poll(f.now) && !r.Buttons());
    f.now+=10000;assert(!r.Poll(f.now) && r.Ready());
    assert(f.info_reads<=1 && f.security_reads==reads && f.writes==writes && f.descriptor_getters==getters);
    assert(f.registers==2 && f.connects==1 && f.bond_begins==1 && f.bond.generation==7);
    assert(!f.pair_calls && !f.native_bonds && !f.scan_starts && !f.general_starts);
    r.Close();
}
void SleepLifetime() {
    Sandbox sandbox;LibnxSession r(Log,true,true,true,true,true,true);ConnectH1(r,4);
    f.now+=5000;assert(!r.Poll(f.now) && f.info_reads==1);
    f.wake.serial=f.wake.ready=1;f.wake.remaining=7;
    f.wake.nonce=f.bond.nonce;f.wake.connection=f.bond.connection;f.wake.owner_epoch=f.bond.owner_epoch;
    f.connected=f.context_active=false;f.now+=500;
    assert(r.Poll(f.now) && r.CompletedLinkLost());
    assert(r.RetireForSleep());
    bool ready{};assert(!r.ResumeAfterSleep(f.now,ready) && !ready);
    f.now+=250;assert(!r.ResumeAfterSleep(f.now,ready) && ready);
    ConnectH1(r,0,true);
    const u8 value[]{0x11,1,0,3};Queue(0,0x1812,0x2a4a,0,value,4);
    f.now+=100;assert(!r.Poll(f.now) && Logged("HID_INFO_H1_IGNORED") && !Logged("HID_INFO_H1_VALUE"));
    f.now+=6000;assert(!r.Poll(f.now) && r.Ready() && !f.info_reads);
    r.Close();
}
int main() {
    ParserAndSelection();
    for(unsigned mode=0;mode<10;++mode)Transport(mode);
    SleepLifetime();
    std::puts("PASS H1: all flag bytes, bounds, READY gate, missing/duplicate/oversize instance, malformed/error/rejected/late/timeout replies, held Home, one read only and no extra pairing/writes; synthetic only");
}
