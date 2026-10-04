// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdio>
#include <unistd.h>
#include <type_traits>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>

namespace armodule {
template<class T> bool ReadExact(const char *path, T &out) {
    static_assert(std::is_trivially_copyable_v<T>);
    FILE *f = std::fopen(path, "rb"); if (!f) return false;
    T value{};
    const bool read = std::fread(&value, 1, sizeof value, f) == sizeof value;
    const bool end = std::fgetc(f) == EOF && !std::ferror(f);
    const bool closed = std::fclose(f) == 0;
    if (!read || !end || !closed) return false;
    std::memcpy(&out, &value, sizeof value); return true;
}
// Temporary/destination paths belong ONLY to this module's config folder.
// Caller is the sole writer. Existing .tmp blocks writes; never truncate it.
// Horizon fsFsRenameFile does NOT implement POSIX overwrite semantics. Persist
// the complete temp first, remove only a regular previous destination, then
// rename. A reader can briefly see MISSING, but never a half-written new record.
// Missing/stale read means no permission. This is not crash-atomic replacement.
using RenameFunction = int (*)(const char *, const char *);
template<class T> bool Publish(const char *temp, const char *path, const T &value,
        RenameFunction rename_file = std::rename) {
    static_assert(std::is_trivially_copyable_v<T>);
    FILE *f = std::fopen(temp, "wbx"); if (!f) return false;
    const bool wrote = std::fwrite(&value, 1, sizeof value, f) == sizeof value;
    const bool flushed = std::fflush(f) == 0;
    const bool synced = ::fsync(::fileno(f)) == 0;
    const bool closed = std::fclose(f) == 0;
    if (!wrote || !flushed || !synced || !closed) return false;
    struct stat previous{};
    if (::stat(path, &previous) == 0) {
        if (!S_ISREG(previous.st_mode) || std::remove(path) != 0) return false;
    } else if (errno != ENOENT) return false;
    return rename_file(temp, path) == 0;
}
}
