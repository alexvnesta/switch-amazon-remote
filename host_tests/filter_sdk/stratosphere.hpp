// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>
using u64=std::uint64_t;using s64=std::int64_t;
#define R_FAILED(x) ((x)!=0)
namespace fake_filter {
inline std::uint64_t now=100000;
inline std::map<std::string,std::vector<std::uint8_t>> files;
inline unsigned creates{},writes{},opens{};
inline bool fail_create{},fail_write{};
template<class F>struct Exit{F f;~Exit(){f();}};
struct ExitFactory{};
template<class F>Exit<F> operator+(ExitFactory,F f){return {f};}
}
#define ON_SCOPE_EXIT auto exit_guard=::fake_filter::ExitFactory{}+[&]()
namespace ams {
namespace os {using SdkMutex=std::mutex;}
namespace fs {
struct FileHandle{std::string path;};
enum {OpenMode_Read=1,OpenMode_Write=2};enum class WriteOption{Flush};
inline std::uint32_t OpenFile(FileHandle *h,const char *p,int){++fake_filter::opens;if(!fake_filter::files.count(p))return 1;h->path=p;return 0;}
inline void CloseFile(FileHandle){}
inline std::uint32_t GetFileSize(s64 *out,FileHandle h){*out=fake_filter::files.at(h.path).size();return 0;}
inline std::uint32_t ReadFile(std::size_t *out,FileHandle h,std::size_t offset,void *p,std::size_t size){
    const auto &v=fake_filter::files.at(h.path);if(offset+size>v.size())return 1;
    std::memcpy(p,v.data()+offset,size);*out=size;return 0;
}
inline std::uint32_t CreateFile(const char *p,std::size_t size){
    ++fake_filter::creates;if(fake_filter::fail_create || fake_filter::files.count(p))return 1;
    fake_filter::files[p].resize(size);return 0;
}
inline std::uint32_t WriteFile(FileHandle h,std::size_t offset,const void *p,std::size_t size,WriteOption){
    ++fake_filter::writes;if(fake_filter::fail_write)return 1;
    auto &v=fake_filter::files.at(h.path);if(offset+size>v.size())return 1;std::memcpy(v.data()+offset,p,size);return 0;
}
}
}
