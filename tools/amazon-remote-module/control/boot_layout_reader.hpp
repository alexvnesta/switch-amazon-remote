// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
namespace arbootlayout {
using Id=std::array<std::uint8_t,32>;
struct Module {std::uint64_t base{},size{};Id id{};};
using Modules=std::array<Module,16>;
struct Expected {std::uint32_t firmware{};std::uint64_t main_size{};Id main{},addon{};};
enum class Stage:std::uint32_t {Prepared,PmOpen,LoaderOpen,PidBefore,ModulesRead,PidAfter,Complete};
enum class Verdict:std::uint32_t {Incomplete,ReadError,WrongFirmware,WrongModules,Matched};
struct Record {
    std::uint32_t magic{0x314c5241},version{1},size{152},firmware{};
    Stage stage{Stage::Prepared};std::uint32_t result{},count{};Verdict verdict{Verdict::Incomplete};
    std::uint64_t run{},before{},after{},main_size{},addon_size{},addon_delta{};
    Id main{},addon{};std::uint32_t checksum{},reserved{};
};
static_assert(sizeof(Record)==152 && offsetof(Record,checksum)==144);
inline std::uint32_t Checksum(const Record &r) {
    const auto *bytes=reinterpret_cast<const std::uint8_t *>(&r);std::uint32_t h=2166136261u;
    for(std::size_t i=0;i<offsetof(Record,checksum);++i)h=(h^bytes[i])*16777619u;
    return h;
}
inline Record Seal(Record r){r.checksum=Checksum(r);return r;}
inline bool Valid(const Record &r) {
    if(r.magic!=0x314c5241 || r.version!=1 || r.size!=sizeof r || !r.run || r.reserved ||
       r.stage>Stage::Complete || r.verdict>Verdict::Matched || r.count>16 || r.checksum!=Checksum(r))return false;
    if(r.verdict==Verdict::Incomplete)return r.stage==Stage::Prepared && !r.result && !r.count;
    if(r.verdict==Verdict::WrongFirmware)return r.stage==Stage::Prepared && !r.result && !r.count;
    if(r.verdict==Verdict::ReadError)return r.result && r.stage!=Stage::Prepared && r.stage!=Stage::Complete;
    if(r.stage!=Stage::Complete || r.result)return false;
    return r.verdict!=Verdict::Matched || (r.count==2 && r.before && r.before==r.after &&
        r.main_size && !(r.main_size%4096) && r.addon_delta==r.main_size && r.addon_size==0x3000);
}
template<class Backend> Record Collect(Backend &b,const Expected &expected,std::uint64_t run) {
    Record r{};r.run=run;r.firmware=b.Firmware();
    if(r.firmware!=expected.firmware){r.verdict=Verdict::WrongFirmware;return Seal(r);}
    r.verdict=Verdict::ReadError;r.stage=Stage::PmOpen;r.result=b.PmOpen();
    if(r.result)return Seal(r);
    r.stage=Stage::LoaderOpen;r.result=b.LoaderOpen();
    if(r.result){b.PmClose();return Seal(r);}
    Modules modules{};std::int32_t count=-1;std::uint64_t pid{};
    r.stage=Stage::PidBefore;r.result=b.Pid(pid);if(!r.result)r.before=pid;
    if(!r.result){r.stage=Stage::ModulesRead;r.result=b.ReadModules(r.before,modules,count);}
    // Discard outputs from failed IPC and never serialize an unvalidated count.
    if(!r.result && (count<0 || count>16))r.result=0x4c310001;
    if(!r.result){pid=0;r.stage=Stage::PidAfter;r.result=b.Pid(pid);if(!r.result)r.after=pid;}
    b.LoaderClose();b.PmClose();
    if(r.result)return Seal(r);
    r.stage=Stage::Complete;r.count=static_cast<std::uint32_t>(count);r.verdict=Verdict::WrongModules;
    if(count>0){r.main=modules[0].id;r.main_size=modules[0].size;}
    if(count>1){r.addon=modules[1].id;r.addon_size=modules[1].size;
        r.addon_delta=modules[1].base>=modules[0].base?modules[1].base-modules[0].base:std::numeric_limits<std::uint64_t>::max();}
    if(count==2 && r.before && r.before==r.after && modules[0].base && !(modules[0].base%4096) &&
       modules[0].size==expected.main_size && modules[1].size==0x3000 &&
       modules[0].base<=std::numeric_limits<std::uint64_t>::max()-expected.main_size &&
       modules[1].base==modules[0].base+expected.main_size &&
       r.main==expected.main && r.addon==expected.addon)r.verdict=Verdict::Matched;
    return Seal(r);
}
}
