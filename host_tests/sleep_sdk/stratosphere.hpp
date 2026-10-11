// SPDX-License-Identifier: GPL-2.0-only
// Source-only fake filesystem/clock, no device implementation.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using s64=std::int64_t;
#define R_FAILED(x) ((x)!=0)
namespace fake_sleep {
inline std::uint64_t now=100000;
inline std::map<std::string,std::vector<std::uint8_t>> files;
inline unsigned creates{},writes{},reads{};
inline bool fail_create{},fail_write{},fail_open_write{},corrupt_readback{},short_readback{};
template<class F>struct Exit{F f;~Exit(){f();}};
struct Factory{};
template<class F>Exit<F> operator+(Factory,F f){return {f};}
}
#define ON_SCOPE_EXIT auto exit_guard=::fake_sleep::Factory{}+[&]()
namespace ams::os {
struct TimeSpan {std::uint64_t value;std::uint64_t GetMilliSeconds()const{return value;}};
struct Tick {TimeSpan ToTimeSpan()const{return {fake_sleep::now};}};
inline Tick GetSystemTick(){return {};}
}
namespace ams::fs {
struct FileHandle{std::string path;};
enum {OpenMode_Read=1,OpenMode_Write=2};enum class WriteOption{Flush};
inline std::uint32_t OpenFile(FileHandle *f,const char *p,int mode){
    if(!fake_sleep::files.count(p) || (mode==OpenMode_Write&&fake_sleep::fail_open_write))return 1;
    f->path=p;return 0;
}
inline void CloseFile(FileHandle){}
inline std::uint32_t GetFileSize(s64 *size,FileHandle f){*size=fake_sleep::files.at(f.path).size();return 0;}
inline std::uint32_t CreateFile(const char *p,std::size_t size){
    ++fake_sleep::creates;if(fake_sleep::fail_create || fake_sleep::files.count(p))return 1;
    fake_sleep::files[p].resize(size);return 0;
}
inline std::uint32_t WriteFile(FileHandle f,std::size_t offset,const void *p,std::size_t size,WriteOption){
    ++fake_sleep::writes;if(fake_sleep::fail_write)return 1;
    auto &bytes=fake_sleep::files.at(f.path);if(offset+size>bytes.size())return 1;
    std::memcpy(bytes.data()+offset,p,size);return 0;
}
inline std::uint32_t ReadFile(std::size_t *actual,FileHandle f,std::size_t offset,void *p,std::size_t size){
    ++fake_sleep::reads;auto &bytes=fake_sleep::files.at(f.path);if(offset+size>bytes.size())return 1;
    std::memcpy(p,bytes.data()+offset,size);*actual=size;
    if(f.path.find("sleep-resume-s1-")!=std::string::npos) {
        if(fake_sleep::corrupt_readback&&size)static_cast<std::uint8_t *>(p)[0]^=1;
        if(fake_sleep::short_readback&&size)--*actual;
    }
    return 0;
}
}
