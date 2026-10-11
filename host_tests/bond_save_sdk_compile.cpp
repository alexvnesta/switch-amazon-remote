// SPDX-License-Identifier: GPL-2.0-only
// Compile-only contract against the actual installed libnx headers. This TU is
// never a console entry point or deployable executable. No synthetic records are
// written to hardware. Exporting both functions forces the native calls into
// the target object so the review can check its undefined-symbol boundary.
#include "../mc_mitm/source/amazon_remote/bond_save_io.hpp"
using namespace amazon_remote::bond::save_io;
static_assert(sizeof(FsSaveDataAttribute)==0x40);
static_assert(sizeof(FsSaveDataInfo)==0x60);
static_assert(sizeof(FsSaveDataFilter)==0x48);
static_assert(sizeof(FsSaveDataExtraData)==0x200);
static_assert(offsetof(FsSaveDataInfo,application_id)==0x28);
static_assert(offsetof(FsSaveDataExtraData,owner_id)==0x40);
Receipt CompileReplace(Input before,Input after) {
    DeviceSlot slot;
    return slot.Replace(before,after);
}
bool CompileRead(Output out) {
    DeviceSlot slot;
    return slot.ReadCommitted(out);
}
