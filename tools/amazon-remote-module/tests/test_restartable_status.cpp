// SPDX-License-Identifier: GPL-2.0-only
#include "../restartable_status.hpp"
#include "../current_status.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using namespace armodule;
namespace {
using Bytes=std::vector<unsigned char>;
std::map<std::string,Bytes> files;
std::map<unsigned,std::string> handles;
unsigned next_handle{},calls{},fail_call{},writes{},renames{},creates{};
bool short_read{},partial_write{},rename_ambiguous{},change_source{},archive_corrupt{};
unsigned source_reads{};
std::string crash_after;
struct PowerLoss {};
Result IO(){return ++calls==fail_call?0xe02:0;}
void Crash(const char *point){if(crash_after==point)throw PowerLoss{};}
void Reset(){files.clear();handles.clear();next_handle=calls=fail_call=writes=renames=creates=source_reads=0;
    short_read=partial_write=rename_ambiguous=change_source=archive_corrupt=false;crash_after.clear();}
void Restart(){handles.clear();calls=fail_call=source_reads=0;crash_after.clear();rename_ambiguous=partial_write=false;}
Bytes Encode(const Status &s){auto p=reinterpret_cast<const unsigned char *>(&s);return {p,p+sizeof s};}
Status Record(std::uint64_t instance,std::uint64_t now=5000){Status s{};s.instance=instance;s.now_ms=now;s.reserved=StatusChecksum(s);return s;}
std::string Archive(std::uint64_t from,std::uint64_t to){return std::string(ARMODULE_NAV15_CONFIG_FS)+"/status-"+std::to_string(from)+"-before-"+std::to_string(to)+".bin";}
}
Result fsFsOpenFile(FsFileSystem *,const char *path,unsigned mode,FsFile *file){
    assert(mode==1 || mode==3);if(auto r=IO())return r;
    if(!files.count(path))return 0x202;
    file->test_handle=++next_handle;handles[file->test_handle]=path;return 0;
}
Result fsFsCreateFile(FsFileSystem *,const char *path,s64 size,unsigned flags){
    assert(size==88 && !flags);if(auto r=IO())return r;
    if(files.count(path))return 0x402;
    files[path]=Bytes(size,0xa5);++creates;Crash("create");return 0;
}
Result fsFsRenameFile(FsFileSystem *,const char *from,const char *to){
    if(auto r=IO())return r;
    for(const auto &h:handles)assert(h.second!=from);
    if(!files.count(from))return 0x202;
    if(files.count(to))return 0x402;
    files[to]=files.at(from);files.erase(from);++renames;
    if(archive_corrupt)files[to][0]^=1;
    Crash("rename");return rename_ambiguous?0xe02:0;
}
Result fsFileGetSize(FsFile *f,s64 *size){if(auto r=IO())return r;*size=files.at(handles.at(f->test_handle)).size();return 0;}
Result fsFileRead(FsFile *f,s64 offset,void *out,std::uint64_t size,unsigned flags,std::uint64_t *actual){
    assert(!offset && size==88 && !flags);if(auto r=IO())return r;
    const auto &path=handles.at(f->test_handle);
    if(path==StatusFsPath && ++source_reads==2 && change_source)files[path][8]^=1;
    const auto &bytes=files.at(path);*actual=short_read?40:size;
    assert(bytes.size()>=*actual);std::memcpy(out,bytes.data(),*actual);return 0;
}
Result fsFileWrite(FsFile *f,s64 offset,const void *in,std::uint64_t size,unsigned flags){
    assert(!offset && size==88 && flags==FsWriteOption_Flush);if(auto r=IO())return r;
    ++writes;auto &bytes=files.at(handles.at(f->test_handle));
    std::memcpy(bytes.data(),in,partial_write?40:size);Crash("write");return partial_write?0xe02:0;
}
void fsFileClose(FsFile *f){assert(handles.erase(f->test_handle)==1);}
int main(){
    FsFileSystem fs{};
    // Regression: the old writer intentionally rejects every existing file.
    Reset();files[StatusFsPath]=Encode(Record(143706393));StatusWriter old;
    assert(old.WriteRecord(fs,Record(142509930))==0x402 && old.permanent && !writes);
    // Actual new writer, three successive process instances, same filesystem.
    RestartableStatusWriter boot2;assert(!boot2.WriteRecord(fs,Record(142509930)));
    assert(boot2.Archived() && files.at(Archive(143706393,142509930))==Encode(Record(143706393)));
    assert(!boot2.WriteRecord(fs,Record(142509930,5500)) && renames==1);
    RestartableStatusWriter boot3;assert(!boot3.WriteRecord(fs,Record(9)) && renames==2);
    assert(files.at(Archive(142509930,9))==Encode(Record(142509930,5500)));
    RestartableStatusWriter boot4;assert(!boot4.WriteRecord(fs,Record(8)) && renames==3);
    assert(files.at(StatusFsPath)==Encode(Record(8)) && handles.empty());
    // All individual reads/opens/create/rename/write failures, preserving data.
    Reset();files[StatusFsPath]=Encode(Record(1));RestartableStatusWriter count;
    assert(!count.WriteRecord(fs,Record(2)));const auto operations=calls;
    for(unsigned failure=1;failure<=operations;++failure){
        Reset();files[StatusFsPath]=Encode(Record(1));fail_call=failure;RestartableStatusWriter w;
        assert(w.WriteRecord(fs,Record(2)) && handles.empty());
        assert(files.count(StatusFsPath) || files.count(Archive(1,2)));
        if(files.count(Archive(1,2)))assert(files.at(Archive(1,2))==Encode(Record(1)));
        if(w.permanent){auto saved=files;fail_call=0;assert(w.WriteRecord(fs,Record(2)) && files==saved);}
    }
    for(unsigned kind=0;kind<6;++kind){
        Reset();auto previous=Record(kind==0?2:1);files[StatusFsPath]=Encode(previous);
        if(kind==1)files[StatusFsPath].resize(87);
        if(kind==2)files[StatusFsPath].resize(89);
        if(kind==3)files[StatusFsPath][0]^=1;
        if(kind==4)files[StatusFsPath][60]^=1;
        if(kind==5)files[Archive(1,2)]={1,2,3};
        const auto before=files;RestartableStatusWriter w;
        assert(w.WriteRecord(fs,Record(2)) && w.permanent && files==before && !writes && !renames);
    }
    Reset();files[StatusFsPath]=Encode(Record(1));short_read=true;RestartableStatusWriter short_file;
    assert(short_file.WriteRecord(fs,Record(2)) && !writes && !renames);
    Reset();files[StatusFsPath]=Encode(Record(1));change_source=true;RestartableStatusWriter changed;
    assert(changed.WriteRecord(fs,Record(2)) && !writes && !renames);
    Reset();files[StatusFsPath]=Encode(Record(1));archive_corrupt=true;RestartableStatusWriter corrupt;
    assert(corrupt.WriteRecord(fs,Record(2)) && corrupt.permanent && !writes && renames==1);
    Reset();files[StatusFsPath]=Encode(Record(1));fail_call=1;RestartableStatusWriter sharing;
    assert(sharing.WriteRecord(fs,Record(2))==0xe02 && !sharing.permanent);
    fail_call=0;assert(!sharing.WriteRecord(fs,Record(2)) && renames==1);
    Reset();files[StatusFsPath]=Encode(Record(1));rename_ambiguous=true;RestartableStatusWriter uncertain;
    assert(uncertain.WriteRecord(fs,Record(2))==0xe02 && uncertain.permanent && renames==1);
    assert(uncertain.WriteRecord(fs,Record(2))==0xe02 && !writes && renames==1);
    Restart();RestartableStatusWriter after_ambiguous;assert(!after_ambiguous.WriteRecord(fs,Record(3)));
    assert(files.at(Archive(1,2))==Encode(Record(1)) && files.at(StatusFsPath)==Encode(Record(3)));
    // Process cuts at each durable boundary. Never truncate or delete partials.
    for(const char *point:{"rename","create","write"}){
        Reset();files[StatusFsPath]=Encode(Record(1));crash_after=point;RestartableStatusWriter interrupted;
        try{(void)interrupted.WriteRecord(fs,Record(2));assert(false);}catch(const PowerLoss&){}
        assert(files.at(Archive(1,2))==Encode(Record(1)));Restart();auto before=files;
        RestartableStatusWriter next;const auto rc=next.WriteRecord(fs,Record(3));
        if(std::string(point)=="create")assert(rc && next.permanent && files==before);
        else assert(!rc && files.at(StatusFsPath)==Encode(Record(3)));
    }
    Reset();partial_write=true;RestartableStatusWriter partial;
    assert(partial.WriteRecord(fs,Record(1))==0xe02 && partial.permanent && writes==1);
    auto incomplete=files;partial_write=false;assert(partial.WriteRecord(fs,Record(1))==0xe02 && files==incomplete);
    RestartableStatusWriter after_partial;assert(after_partial.WriteRecord(fs,Record(2)) && files==incomplete);
    // UI must not mistake a previous process's temporally fresh status for this one.
    StartupRecord startup{};startup.instance=7;startup.stage=unsigned(StartupStage::Status);startup.now_ms=1000;startup.checksum=StartupChecksum(startup);
    assert(CurrentStatus(Record(7),startup,5001));
    assert(!CurrentStatus(Record(6),startup,5001));
    assert(!CurrentStatus(Record(7),startup,7001));
    startup.result=0x402;startup.checksum=StartupChecksum(startup);assert(!CurrentStatus(Record(7),startup,5001));
    startup.result=0;startup.stage=unsigned(StartupStage::Main);startup.checksum=StartupChecksum(startup);assert(!CurrentStatus(Record(7),startup,5001));
    std::printf("PASS restartable status: repeated boots, exact prior preservation, %u IO faults, interrupted rename/create/write, malformed/collision refusal, sticky ambiguous/partial writes, current-startup-bound UI\n",operations);
}
