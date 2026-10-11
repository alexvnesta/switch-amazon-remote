// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "bond_save_io.hpp"

namespace amazon_remote::bond::record {
// Private, fixed-size, canonical storage format, never SD/FTP/public IPC.
// SHA256 detects accidental corruption. It is NOT a MAC, encryption, rollback
// protection or proof of pairing. Privileged software can read/modify this save.
// No deployed consumer/provisioning exists yet. This is schema 1, legacy ABI only.
using Input=save_io::Input;
using Output=save_io::Output;
inline constexpr std::array<std::uint8_t,8> Magic{'A','R','B','O','N','D','0','1'};
inline constexpr std::array<std::uint8_t,5> Kinds{1,2,4,8,32};
inline constexpr std::uint32_t KnownMask=0x2f;
inline constexpr std::size_t DigestOffset=480, SlotsOffset=112, SlotBytes=32;
struct Binding {
    // Profile-specific stable binding. General full-key records require native
    // root-lifetime provenance. Profile CEN1 (0x314e4543) contains only central
    // PENC and binds the public local controller identity namespace instead.
    // CEN1 is NOT root provenance and must never contain root-dependent keys.
    std::array<std::uint8_t,32> controller_context{};
    legacy::Address local{},peer{}; // Native byte order, resolved identities only.
    std::uint8_t local_type{},peer_type{}; // Public=0, static random identity=1.
    std::uint32_t profile{};
    bool operator==(const Binding &) const = default;
};
// Immutable under the caller's enrollment/writer lock. Expected distribution
// comes from trusted negotiated evidence, NEVER just OR-ing observed events.
using Materials=std::array<const legacy::Material*,5>;
struct Metadata {
    std::uint64_t generation{};
    std::uint32_t mask{};
    std::uint8_t key_bytes{};
    bool occupied{},mitm{};
};
class Buffer {
    save_io::Bytes bytes_{};
public:
    Buffer()=default;
    ~Buffer() { Clear(); }
    Buffer(const Buffer &)=delete;
    Buffer &operator=(const Buffer &)=delete;
    void Clear() { legacy::Erase(bytes_); }
    Input Read() const { return bytes_; }
    Output Write() { return bytes_; } // Private adapter only, not a key exporter.
};
inline bool Zero(std::span<const std::uint8_t> a) {
    return std::all_of(a.begin(),a.end(),[](auto b){return b==0;});
}
inline bool LocalValid(const Binding &b) {
    return !Zero(b.controller_context) && legacy::Nonzero(b.local) &&
        b.local_type<=1 && b.profile;
}
inline bool Valid(const Binding &b) {
    return LocalValid(b) && legacy::Nonzero(b.peer) && b.peer_type<=1;
}
inline void Put(Output out,std::size_t pos,std::uint64_t value,unsigned n) {
    for(unsigned i=0;i<n;++i) out[pos+i]=std::uint8_t(value>>(i*8));
}
inline std::uint64_t Get(Input in,std::size_t pos,unsigned n) {
    std::uint64_t value{};
    for(unsigned i=0;i<n;++i) value|=std::uint64_t(in[pos+i])<<(i*8);
    return value;
}
inline void Seal(Output out) { sha256CalculateHash(out.data()+DigestOffset,out.data(),DigestOffset); }
inline bool DigestValid(Input in) {
    std::array<std::uint8_t,32> digest{};
    sha256CalculateHash(digest.data(),in.data(),DigestOffset);
    const bool ok=std::equal(digest.begin(),digest.end(),in.begin()+DigestOffset);
    legacy::Erase(digest); return ok;
}
inline void Header(Output out,const Binding &b) {
    legacy::Erase(out);
    std::copy(Magic.begin(),Magic.end(),out.begin());
    Put(out,8,1,4); Put(out,12,out.size(),4); Put(out,16,save_io::OwnerProgramId,8);
    Put(out,36,b.profile,4);
    std::copy(b.controller_context.begin(),b.controller_context.end(),out.begin()+40);
    std::copy(b.local.begin(),b.local.end(),out.begin()+72); out[78]=b.local_type;
}
inline bool Empty(const Binding &b,Buffer &out) {
    out.Clear();
    if(!LocalValid(b)) return false;
    Header(out.Write(),b); Seal(out.Write()); return true;
}
inline bool SlotValid(std::span<const std::uint8_t,28> u,std::uint8_t kind,
                      const Binding &b,std::uint8_t key_bytes,bool mitm) {
    const auto length=legacy::MeaningfulBytes(kind);
    if(!length || !Zero(u.subspan(length))) return false;
    // Legacy field positions from the public pre-SC BTM contract, not C++
    // structure casts. Conservative acceptance only; does not prove native load.
    const std::uint8_t security=mitm ? 4 : 1;
    switch(kind) {
        case 1: return u[26]==security && u[27]==key_bytes;
        case 2: return u[16]==b.peer_type && std::equal(b.peer.begin(),b.peer.end(),u.begin()+17);
        case 4: return u[20]==security;
        case 8: return u[2]==key_bytes && u[3]==security;
        case 32: return u[6]==security;
    }
    return false;
}
inline bool SharedLocalDivValid(std::span<const std::uint8_t,28> encryption,
                                std::span<const std::uint8_t,28> signing) {
    // The audited legacy native store has ONE local DIV. LENC and LCSRK
    // must agree, or restoring the second would silently replace the first.
    // Peer EDIV is a different field and must not be compared with local DIV.
    return encryption[0]==signing[4] && encryption[1]==signing[5];
}
inline bool Build(const Binding &b,std::uint64_t generation,const Materials &keys,
                  std::uint32_t expected_mask,std::uint8_t key_bytes,bool mitm,Buffer &out) {
    out.Clear();
    // This central-reconnect format requires a peer encryption record. No
    // guessed keys/IRK, Secure Connections union, or partial distribution.
    if(!Valid(b) || !generation || !(expected_mask&1) || (expected_mask&~KnownMask) ||
       key_bytes<7 || key_bytes>16) return false;
    for(std::size_t i=0;i<Kinds.size();++i) {
        const bool present=(expected_mask&Kinds[i])!=0;
        if(present!=bool(keys[i])) return false;
        if(present && (keys[i]->KeyKind()!=Kinds[i] ||
            !SlotValid(keys[i]->NativeUnion(),Kinds[i],b,key_bytes,mitm))) return false;
    }
    if((expected_mask&0x28)==0x28 &&
       !SharedLocalDivValid(keys[3]->NativeUnion(),keys[4]->NativeUnion())) return false;
    auto a=out.Write(); Header(a,b); Put(a,24,generation,8); Put(a,32,1,4);
    a[79]=b.peer_type; std::copy(b.peer.begin(),b.peer.end(),a.begin()+80);
    Put(a,88,expected_mask,4); Put(a,92,expected_mask,4);
    a[96]=key_bytes; a[97]=mitm ? 4 : 1; a[98]=mitm; a[99]=1; // Audited legacy layout.
    for(std::size_t i=0;i<Kinds.size();++i) if(keys[i]) {
        const auto p=SlotsOffset+i*SlotBytes;
        a[p]=Kinds[i]; a[p+1]=legacy::MeaningfulBytes(Kinds[i]);
        std::copy(keys[i]->NativeUnion().begin(),keys[i]->NativeUnion().end(),a.begin()+p+4);
    }
    Seal(a); return true;
}
// Validate before use, even after SHA verification. Exact binding is supplied
// by the private driver, not accepted from the record. Metadata is cleared on
// all failures. Empty is an explicit initialized sentinel, never an all-zero or
// missing file. This does not open storage, pair, or load keys into the driver.
inline bool Inspect(std::span<const std::uint8_t> bytes,const Binding &b,
                    std::uint8_t minimum_key_bytes,Metadata &out) {
    out={};
    if(bytes.size()!=save_io::RecordBytes || !Valid(b) || minimum_key_bytes<7 ||
       minimum_key_bytes>16) return false;
    const Input a(bytes.data(),save_io::RecordBytes);
    if(!std::equal(Magic.begin(),Magic.end(),a.begin()) || Get(a,8,4)!=1 ||
       Get(a,12,4)!=a.size() || Get(a,16,8)!=save_io::OwnerProgramId ||
       Get(a,36,4)!=b.profile || !DigestValid(a) ||
       !std::equal(b.controller_context.begin(),b.controller_context.end(),a.begin()+40) ||
       !std::equal(b.local.begin(),b.local.end(),a.begin()+72) || a[78]!=b.local_type ||
       !Zero(a.subspan(86,2)) || !Zero(a.subspan(100,12)) || !Zero(a.subspan(272,208))) return false;
    if(Get(a,32,4)==0) return !Get(a,24,8) && Zero(a.subspan(79,21)) && Zero(a.subspan(112,160));
    if(Get(a,32,4)!=1 || !Get(a,24,8) || a[79]!=b.peer_type ||
       !std::equal(b.peer.begin(),b.peer.end(),a.begin()+80)) return false;
    const auto mask=std::uint32_t(Get(a,88,4));
    if(!(mask&1) || (mask&~KnownMask) || Get(a,92,4)!=mask ||
       a[96]<minimum_key_bytes || a[96]>16 || a[98]>1 || a[99]!=1 ||
       a[97]!=(a[98] ? 4 : 1)) return false;
    for(std::size_t i=0;i<Kinds.size();++i) {
        const auto p=SlotsOffset+i*SlotBytes;
        if(!(mask&Kinds[i])) { if(!Zero(a.subspan(p,SlotBytes))) return false; }
        else if(a[p]!=Kinds[i] || a[p+1]!=legacy::MeaningfulBytes(Kinds[i]) ||
                a[p+2] || a[p+3] ||
                !SlotValid(std::span<const std::uint8_t,28>(a.data()+p+4,28),Kinds[i],b,a[96],a[98])) return false;
    }
    if((mask&0x28)==0x28 && !SharedLocalDivValid(
        std::span<const std::uint8_t,28>(a.data()+SlotsOffset+3*SlotBytes+4,28),
        std::span<const std::uint8_t,28>(a.data()+SlotsOffset+4*SlotBytes+4,28))) return false;
    out={Get(a,24,8),mask,a[96],true,a[98]!=0}; return true;
}
}
