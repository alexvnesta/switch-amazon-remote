// SPDX-License-Identifier: GPL-2.0-only
// Narrow test declarations for the real adapter. No create/delete/mount-SD,
// system-save, raw-partition, radio or key-export API is available here.
#pragma once
#include <cstddef>
#include <cstdint>
using u8=std::uint8_t; using u16=std::uint16_t; using u32=std::uint32_t;
using u64=std::uint64_t; using s64=std::int64_t; using Result=u32;
inline constexpr unsigned FS_MAX_PATH=0x301;
struct AccountUid { u64 uid[2]{}; };
struct FsFileSystem { unsigned handle{}; bool readonly{}; };
struct FsFile { unsigned handle{}; bool readonly{}; };
struct FsSaveDataInfoReader { unsigned handle{}, reads{}; };
enum FsSaveDataSpaceId { FsSaveDataSpaceId_System=0, FsSaveDataSpaceId_User=1 };
enum { FsSaveDataType_System=0, FsSaveDataType_Account=1, FsSaveDataType_Device=3,
       FsSaveDataRank_Primary=0, FsOpenMode_Read=1, FsOpenMode_Write=2,
       FsReadOption_None=0, FsWriteOption_None=0 };
struct FsSaveDataAttribute {
    u64 application_id{}; AccountUid uid{}; u64 system_save_data_id{};
    u8 save_data_type{}, save_data_rank{}; u16 save_data_index{};
    u32 pad_x24{}; u64 unk_x28{},unk_x30{},unk_x38{};
};
struct FsSaveDataInfo {
    u64 save_data_id{}; u8 save_data_space_id{},save_data_type{}; u8 pad[6]{};
    AccountUid uid{}; u64 system_save_data_id{},application_id{},size{};
    u16 save_data_index{}; u8 save_data_rank{},unk_x3b[0x25]{};
};
struct FsSaveDataFilter {
    bool filter_by_application_id{},filter_by_save_data_type{},filter_by_user_id{},
         filter_by_system_save_data_id{},filter_by_index{};
    u8 save_data_rank{},padding[2]{}; FsSaveDataAttribute attr{};
};
struct FsSaveDataExtraData {
    FsSaveDataAttribute attr{}; u64 owner_id{},timestamp{}; u32 flags{},unk_x54{};
    s64 data_size{},journal_size{}; u64 commit_id{}; u8 unused[0x190]{};
};
Result fsOpenSaveDataInfoReaderWithFilter(FsSaveDataInfoReader*,FsSaveDataSpaceId,const FsSaveDataFilter*);
Result fsSaveDataInfoReaderRead(FsSaveDataInfoReader*,FsSaveDataInfo*,std::size_t,s64*);
void fsSaveDataInfoReaderClose(FsSaveDataInfoReader*);
Result fsReadSaveDataFileSystemExtraDataBySaveDataSpaceId(void*,std::size_t,FsSaveDataSpaceId,u64);
Result fsOpenSaveDataFileSystem(FsFileSystem*,FsSaveDataSpaceId,const FsSaveDataAttribute*);
Result fsOpenReadOnlySaveDataFileSystem(FsFileSystem*,FsSaveDataSpaceId,const FsSaveDataAttribute*);
Result fsFsOpenFile(FsFileSystem*,const char*,u32,FsFile*);
Result fsFileGetSize(FsFile*,s64*);
Result fsFileRead(FsFile*,s64,void*,u64,u32,u64*);
Result fsFileWrite(FsFile*,s64,const void*,u64,u32);
Result fsFileFlush(FsFile*);
void fsFileClose(FsFile*);
Result fsFsCommit(FsFileSystem*);
void fsFsClose(FsFileSystem*);
void sha256CalculateHash(void*,const void*,std::size_t);
