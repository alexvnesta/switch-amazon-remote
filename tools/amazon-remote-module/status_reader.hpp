// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>
#include "protocol.hpp"
namespace armodule {
// Exact raw-FS error classification: a sharing lock may retain a still-fresh
// previously verified snapshot. Corruption or other errors never grant permission.
inline Result ReadStatusFrame(FsFileSystem &fs,Status &out,std::uint64_t now) {
    FsFile file{};auto rc=fsFsOpenFile(&fs,StatusFsPath,FsOpenMode_Read,&file);
    if (rc) return rc;
    Status candidate{};s64 size{};u64 read{};
    rc=fsFileGetSize(&file,&size);
    if (!rc && size!=sizeof candidate) rc=0x402;
    if (!rc) rc=fsFileRead(&file,0,&candidate,sizeof candidate,0,&read);
    fsFileClose(&file);
    if (!rc && (read!=sizeof candidate || !FreshStatus(candidate,now))) rc=0x402;
    if (!rc) out=candidate;
    return rc;
}
}
