#include "../mc_mitm/source/amazon_remote/hogp_client.hpp"
#include "../mc_mitm/source/amazon_remote/input_diagnostic_policy.hpp"
#include "../mc_mitm/source/amazon_remote/k7q3m7_descriptor.hpp"
#include "../mc_mitm/source/amazon_remote/remote_decoder.hpp"
#include <array>
#include <cassert>
#include <iostream>
#include <vector>
using namespace amazon_remote;
Attribute A(Kind kind, std::uint16_t handle, std::uint16_t uuid,
        std::uint16_t end=0, std::uint8_t properties=0, std::uint8_t instance=0) {
    return {kind,handle,end,uuid,properties,instance,kind==Kind::Service};
}
const std::array<Attribute,4> Fingerprint{
    A(Kind::Service,0x12,0x180a,0x24), A(Kind::Characteristic,0x24,0x2a50),
    A(Kind::Service,0x25,0x1812,0x41), A(Kind::Characteristic,0x2e,0x2a4b)};
std::vector<Attribute> Inputs() {
    return {A(Kind::Characteristic,0x30,0x2a4d,0,0x12,0), A(Kind::Descriptor,0x31,0x2902), A(Kind::Descriptor,0x32,0x2908),
            A(Kind::Characteristic,0x34,0x2a4d,0,0x12,1), A(Kind::Descriptor,0x35,0x2902), A(Kind::Descriptor,0x36,0x2908),
            A(Kind::Characteristic,0x38,0x2a4d,0,0x12,2), A(Kind::Descriptor,0x39,0x2908)};
}
void Prepare(HogpClient &c) {
    c.Connect(4);
    assert(!c.BeginSecurityRead() && !c.BeginVerifiedInputs(nullptr,0));
    assert(c.Append(4,Fingerprint.data(),Fingerprint.size()) && c.FinishCache(4));
    Request r;
    const std::uint8_t pnp[]{2,0x71,1,0x27,4,0x60,0};
    assert(c.Next(r,1) && r.authentication==Authentication::None && IsFingerprintRead(r));
    assert(!c.BeginSecurityRead());
    assert(c.Complete(r,true,pnp,sizeof pnp));
    assert(c.Next(r,2) && r.authentication==Authentication::None && IsFingerprintRead(r));
    assert(c.Complete(r,true,K7q3m7Descriptor,sizeof K7q3m7Descriptor));
    assert(c.GetPhase()==Phase::FingerprintVerified && !c.Next(r,3));
    assert(!c.BeginVerifiedInputs(nullptr,0));
}
void Secure(HogpClient &c) {
    Prepare(c);
    assert(c.BeginSecurityRead() && !c.BeginSecurityRead());
    Request r;
    assert(c.Next(r,10) && r.operation==Operation::ReadCharacteristic && r.characteristic.uuid==0x2a4b && r.authentication==Authentication::NoMitm);
    assert(IsInputDiagnosticRequest(r,c.GetPhase(),true,false,false));
    assert(!IsInputDiagnosticRequest(r,c.GetPhase(),false,false,false));
    assert(!IsInputDiagnosticRequest(r,c.GetPhase(),true,true,false));
    for (unsigned variant=0;variant<8;++variant) {
        auto forged=r;
        if (variant==0) forged.authentication=Authentication::None;
        if (variant==1) ++forged.generation;
        if (variant==2) ++forged.connection;
        if (variant==3) ++forged.characteristic.instance;
        if (variant==4) ++forged.characteristic.handle;
        if (variant==5) forged.operation=Operation::WriteDescriptor;
        if (variant==6) ++forged.size;
        if (variant==7) ++forged.service.end;
        assert(!c.Complete(forged,true,K7q3m7Descriptor,sizeof K7q3m7Descriptor));
        assert(c.GetPhase()==Phase::SecurityRead);
    }
    assert(c.Complete(r,true,K7q3m7Descriptor,sizeof K7q3m7Descriptor));
    assert(c.GetPhase()==Phase::SecurityReadVerified && !c.Next(r,20));
    assert(!c.BeginSecurityRead() && !c.Complete(r,true,K7q3m7Descriptor,sizeof K7q3m7Descriptor));
}
void References(HogpClient &c, bool expected=true, bool duplicate=false) {
    unsigned index=0;
    for (const std::array<std::uint8_t,2> reference : {std::array<std::uint8_t,2>{1,1}, {2,1}, {0xef,1}}) {
        Request r;
        assert(c.Next(r,30+index) && r.operation==Operation::ReadDescriptor && r.descriptor.uuid==0x2908 && r.authentication==Authentication::NoMitm);
        assert(IsInputDiagnosticRequest(r,c.GetPhase(),true,true,true));
        assert(!IsInputDiagnosticRequest(r,c.GetPhase(),true,false,true));
        assert(!IsInputDiagnosticRequest(r,c.GetPhase(),true,true,false));
        auto value=reference; if (duplicate && index==1) value[0]=1;
        const bool completed=c.Complete(r,true,value.data(),value.size());
        assert(completed==(index==2 ? expected : true));
        ++index;
    }
}
void Success() {
    HogpClient c(K7q3m7Descriptor,sizeof K7q3m7Descriptor,true);
    Secure(c); const auto generation=c.Generation(); const auto input=Inputs();
    assert(c.BeginVerifiedInputs(input.data(),input.size()));
    assert(c.Generation()==generation && c.GetPhase()==Phase::References && !c.BeginVerifiedInputs(input.data(),input.size()));
    References(c);
    unsigned operations=0;
    Request r;
    while (c.Next(r,50)) {
        assert(r.characteristic.handle==0x30 || r.characteristic.handle==0x34);
        assert(IsInputDiagnosticRequest(r,c.GetPhase(),true,true,true));
        assert(r.authentication==Authentication::NoMitm);
        if (r.operation==Operation::WriteDescriptor) {
            assert(r.descriptor.uuid==0x2902 && r.value[0]==1 && r.value[1]==0 && r.size==2);
            auto bad=r; bad.value[0]=0; assert(!c.Complete(bad,true,nullptr,0));
        } else assert(r.operation==Operation::RegisterNotification);
        assert(c.Complete(r,true,nullptr,0)); ++operations;
    }
    assert(operations==4 && c.GetPhase()==Phase::Ready);
    const std::uint8_t keyboard[]{0x52,0x58,0}, consumer[]{0x23,2,0,0};
    InputReport out; RemoteDecoder decoder; assert(decoder.Configure(K7q3m7Descriptor,sizeof K7q3m7Descriptor));
    assert(c.Notification(4,0x1812,0x2a4d,keyboard,sizeof keyboard,out) && out.id==1);
    assert(decoder.Ingest(out.id,out.payload,out.size)==Update::Changed);
    assert((decoder.Buttons() & (Up|Confirm))==(Up|Confirm));
    assert(c.Notification(4,0x1812,0x2a4d,consumer,sizeof consumer,out) && out.id==2);
    assert(decoder.Ingest(out.id,out.payload,out.size)==Update::Changed && (decoder.Buttons()&Home));
    assert(!c.Notification(5,0x1812,0x2a4d,keyboard,sizeof keyboard,out));
    assert(!c.Notification(4,0xfe03,0x2a4d,keyboard,sizeof keyboard,out));
    assert(!c.Notification(4,0x1812,0x2a4d,consumer,2,out));
    c.Disconnect(); decoder.Disconnect(); assert(!decoder.Buttons());
    assert(!c.Notification(4,0x1812,0x2a4d,keyboard,sizeof keyboard,out));
    assert(!c.BeginSecurityRead() && !c.BeginVerifiedInputs(input.data(),input.size()));
}
void Failures() {
    for (unsigned variant=0;variant<4;++variant) {
        HogpClient c(K7q3m7Descriptor,sizeof K7q3m7Descriptor,true); Prepare(c);
        assert(c.BeginSecurityRead()); Request r; assert(c.Next(r,100));
        auto map=std::to_array(K7q3m7Descriptor); map[0]^=1;
        if (variant==0) assert(!c.Complete(r,false,nullptr,0));
        if (variant==1) assert(!c.Complete(r,true,map.data(),map.size()));
        if (variant==2) assert(!c.Complete(r,true,K7q3m7Descriptor,sizeof K7q3m7Descriptor-1));
        if (variant==3) assert(!c.Tick(10100));
        assert(c.GetPhase()==Phase::Failed && !c.Next(r,20000) && !c.BeginSecurityRead());
        assert(!c.Complete(r,true,K7q3m7Descriptor,sizeof K7q3m7Descriptor));
    }
    for (unsigned variant=0;variant<11;++variant) {
        HogpClient c(K7q3m7Descriptor,sizeof K7q3m7Descriptor,true); Secure(c); auto input=Inputs();
        if (variant==0) input[3].instance=0;
        if (variant==1) input[0].handle=0x2e; // Existing map handle.
        if (variant==2) input[0].handle=0x24; // Outside HID.
        if (variant==3) input.back().handle=0x42;
        if (variant==4) input[1].uuid=0x2901;
        if (variant==5) input[1].kind=Kind::Service;
        if (variant==6) input[1].handle=input[2].handle;
        if (variant==7) input[2].uuid=0x2902; // Duplicate CCC and no reference.
        if (variant==8) input[1].instance=1;
        if (variant==9) input.pop_back();
        if (variant==10) input.resize(3); // Only one report.
        assert(!c.BeginVerifiedInputs(input.data(),input.size()));
        assert(c.GetPhase()==Phase::Failed); Request r; assert(!c.Next(r,100));
    }
    for (unsigned variant=0;variant<3;++variant) {
        HogpClient c(K7q3m7Descriptor,sizeof K7q3m7Descriptor,true); Secure(c); auto input=Inputs();
        if (variant==0) input[0].properties=0; // Observed zero is not fabricated into Notify.
        if (variant==1) input[1].uuid=0x2908, input.erase(input.begin()+2); // No keyboard CCC.
        assert(c.BeginVerifiedInputs(input.data(),input.size()));
        References(c,false,variant==2);
        assert(c.GetPhase()==Phase::Failed); Request r; assert(!c.Next(r,100));
        if(variant==0) assert(c.GetError()==Error::NotifyUnavailable);
    }
    for (const std::size_t count : {0,49,257}) {
        HogpClient c(K7q3m7Descriptor,sizeof K7q3m7Descriptor,true); Secure(c); const auto input=Inputs();
        assert(!c.BeginVerifiedInputs(input.data(),count)); assert(c.GetPhase()==Phase::Failed);
    }
    HogpClient c(K7q3m7Descriptor,sizeof K7q3m7Descriptor,true); Secure(c);
    assert(!c.BeginVerifiedInputs(nullptr,1)); assert(c.GetPhase()==Phase::Failed);
}
void Whitelist() {
    Request r; r.service=A(Kind::Service,0x25,0x1812,0x41); r.characteristic=A(Kind::Characteristic,0x30,0x2a4d);
    r.authentication=Authentication::NoMitm; r.descriptor=A(Kind::Descriptor,0x31,0x2902); r.size=2; r.value={1,0};
    for (const auto phase : {Phase::Identity,Phase::Map,Phase::Ready,Phase::FingerprintVerified,Phase::Failed,Phase::SecurityReadVerified}) {
        r.operation=Operation::WriteDescriptor;
        assert(!IsInputDiagnosticRequest(r,phase,true,true,true));
    }
    assert(IsInputDiagnosticRequest(r,Phase::Subscribe,true,true,true));
    r.value[1]=1; assert(!IsInputDiagnosticRequest(r,Phase::Subscribe,true,true,true)); r.value[1]=0;
    r.size=1; assert(!IsInputDiagnosticRequest(r,Phase::Subscribe,true,true,true)); r.size=2;
    r.authentication=Authentication::None; assert(!IsInputDiagnosticRequest(r,Phase::Subscribe,true,true,true));
}
int main() {
    Success(); Failures(); Whitelist();
    std::cout << "PASS explicit fingerprint/security/input continuation, no-security fallback, metadata ambiguity/zero properties, bounded CCC whitelist, stale responses and decoded input reset\n";
}
