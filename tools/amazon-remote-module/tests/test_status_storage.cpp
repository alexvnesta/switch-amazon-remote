// SPDX-License-Identifier: GPL-2.0-only
#include "../status_storage.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>
namespace {
std::array<unsigned char,88> bytes{};bool present{};s64 length=88;
Result open_error{},read_error{},write_error{};unsigned opens{},closes{},writes{},creates{};
void Reset() { bytes={};present=false;length=88;open_error=read_error=write_error=0;opens=closes=writes=creates=0; }
}
Result fsFsOpenFile(FsFileSystem *,const char *path,unsigned mode,FsFile *) {
    assert(!std::strcmp(path,"/config/amazon-remote-nav10/status.bin") && mode==3);++opens;
    return open_error?open_error:present?0:0x202;
}
Result fsFsCreateFile(FsFileSystem *,const char *,s64 size,unsigned flags) {
    assert(!present && size==88 && !flags);present=true;++creates;return 0;
}
Result fsFileGetSize(FsFile *,s64 *out) { *out=length;return 0; }
Result fsFileRead(FsFile *,s64 offset,void *out,std::uint64_t size,unsigned flags,std::uint64_t *read) {
    assert(!offset && size==88 && !flags);if(read_error)return read_error;
    std::memcpy(out,bytes.data(),size);*read=size;return 0;
}
Result fsFileWrite(FsFile *,s64 offset,const void *in,std::uint64_t size,unsigned flags) {
    assert(!offset && size==88 && flags==FsWriteOption_Flush);++writes;
    if (write_error) { std::memcpy(bytes.data(),in,40);return write_error; }
    std::memcpy(bytes.data(),in,size);return 0;
}
void fsFileClose(FsFile *) { ++closes; }
int main() {
    using namespace armodule;static_assert(Version==3);
    assert(!StatusFailureExpired(100,2099,false) && StatusFailureExpired(100,2100,false));
    assert(StatusFailureExpired(100,99,false) && StatusFailureExpired(100,100,true) && StatusFailureExpired(0,100,false));
    FsFileSystem fs{};Status s{};s.instance=10;s.now_ms=5000;s.reserved=StatusChecksum(s);
    assert(FreshStatus(s,5000) && !FreshStatus(s,4999) && !FreshStatus(s,7001));
    for (std::size_t i=0;i<sizeof s;++i) {
        auto bad=s;reinterpret_cast<unsigned char *>(&bad)[i]^=1;assert(!FreshStatus(bad,5000));
    }
    Reset();StatusWriter good;assert(!good.WriteRecord(fs,s) && creates==1 && writes==1 && closes==1);
    const auto saved=bytes;s.now_ms=5500;s.reserved=StatusChecksum(s);
    assert(!good.WriteRecord(fs,s) && creates==1 && writes==2 && closes==2);
    open_error=123;const auto before=bytes;assert(good.WriteRecord(fs,s)==123 && !good.permanent && bytes==before);
    open_error=0;s.now_ms=6000;s.reserved=StatusChecksum(s);assert(!good.WriteRecord(fs,s));
    read_error=456;assert(good.WriteRecord(fs,s)==456 && !good.permanent);read_error=0;
    length=87;assert(good.WriteRecord(fs,s)==0x402 && good.permanent);length=88;
    bytes[0]^=1;assert(good.WriteRecord(fs,s)==0x402 && good.permanent);bytes[0]^=1;
    s.instance=11;s.reserved=StatusChecksum(s);assert(good.WriteRecord(fs,s)==0x402 && good.permanent);
    Reset();present=true;bytes=saved;StatusWriter foreign;assert(foreign.WriteRecord(fs,s)==0x402 && foreign.permanent && !writes);
    Reset();StatusWriter partial;s.instance=10;s.reserved=StatusChecksum(s);write_error=789;
    assert(partial.WriteRecord(fs,s)==789 && partial.permanent && writes==1);
    write_error=0;assert(partial.WriteRecord(fs,s)==0x402 && partial.permanent && writes==1);
    std::puts("PASS nav10 status: checksum corruption/freshness guards, fixed raw FS write/flush, no delete/rename, reopen/read errors, preserved wrong-size/foreign-instance/partial records; NOT device proof.");
}
