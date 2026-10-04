// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>
#include "startup.hpp"

namespace armodule {
// Raw FS calls work before C++ constructors/devoptab/stdio logging. Only this
// fixed module-owned file is touched. No deletion, resizing, directories or NAND.
inline Result WriteStartup(FsFileSystem &fs, const StartupRecord &record) {
    FsFile file{};
    auto rc = fsFsOpenFile(&fs, StartupFsPath, FsOpenMode_Write, &file);
    if (rc == 0x202) { // Public FS PathNotFound (module2, description1).
        rc = fsFsCreateFile(&fs, StartupFsPath, sizeof record, 0);
        if (rc) return rc;
        rc = fsFsOpenFile(&fs, StartupFsPath, FsOpenMode_Write, &file);
    }
    if (rc) return rc;
    s64 size{};
    rc = fsFileGetSize(&file, &size);
    if (!rc && size != sizeof record) rc = 0x402; // Preserve unexpected file size.
    if (!rc) rc = fsFileWrite(&file, 0, &record, sizeof record, FsWriteOption_Flush);
    fsFileClose(&file);
    return rc;
}
}
