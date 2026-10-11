// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../bond_save_sdk/switch.h"
struct FsSaveDataCreationInfo {s64 save_data_size,journal_size;u64 available_size,owner_id;u32 flags;u8 save_data_space_id,unk,padding[26];};
struct FsSaveDataMetaInfo {u32 size;u8 type,reserved[11];};
struct BtdrvAdapterProperty {u8 data[256];};
struct BtdrvAddress {u8 address[6];};
inline constexpr int BtdrvAdapterPropertyType_Address=1,Module_Libnx=345,LibnxError_BadInput=11;
#define MAKERESULT(m,d) ((Result)((m)|((d)<<9)))
#define R_SUCCEEDED(r) ((r)==0)
Result fsInitialize();
Result btdrvGetAdapterProperty(int,BtdrvAdapterProperty*);
Result btdrvWriteHidData2(BtdrvAddress,const void*,std::size_t);
Result pminfoGetProgramId(u64*,u64);
u64 armGetSystemTick();
u64 armTicksToNs(u64);
Result fsCreateSaveDataFileSystem(const FsSaveDataAttribute*,const FsSaveDataCreationInfo*,const FsSaveDataMetaInfo*);
Result fsFsCreateFile(FsFileSystem*,const char*,s64,u32);
