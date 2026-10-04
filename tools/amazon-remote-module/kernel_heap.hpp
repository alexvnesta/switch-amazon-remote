// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>
#include <cstddef>
namespace armodule {
inline constexpr std::size_t KernelHeapSize=0x200000;
// Our work-buffer policy requires a real heap, not stack/static fake-heap storage.
inline Result InitializeKernelHeap(void *&start,void *&end) {
    start=end=nullptr; void *base{};
    const auto rc=svcSetHeapSize(&base,KernelHeapSize);
    if (!rc) { start=base; end=static_cast<unsigned char *>(base)+KernelHeapSize; }
    return rc;
}
}
