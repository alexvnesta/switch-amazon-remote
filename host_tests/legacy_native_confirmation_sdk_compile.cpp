// SPDX-License-Identifier: GPL-2.0-only
// Compile-only check. No native lookup, IPC, radio or private-key export.
#include "../mc_mitm/source/amazon_remote/legacy_native_confirmation.hpp"
namespace b=amazon_remote::bond;
struct ConfirmationCompileGuard {
    bool (*current)(const b::record::Binding &,std::uint64_t);
    bool Current(const b::record::Binding &binding,std::uint64_t generation) { return current(binding,generation); }
};
bool CompileNativeConfirmation(std::span<const std::uint8_t> record,const b::record::Binding &binding,
        std::uint64_t generation,std::uint8_t minimum,b::legacy::Layout layout,
        std::span<const std::uint8_t> entry,ConfirmationCompileGuard &guard) {
    return b::native_confirmation::Check(record,binding,generation,minimum,layout,entry,guard);
}
