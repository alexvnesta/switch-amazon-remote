// SPDX-License-Identifier: GPL-2.0-only
// Compile-only private bridge, no entry point, storage, native sender or radio.
#include "../mc_mitm/source/amazon_remote/legacy_bond_reload.hpp"
namespace b=amazon_remote::bond;
struct ReloadCompileGuard {
    bool (*current)(const b::record::Binding &,std::uint64_t);
    bool Current(const b::record::Binding &binding,std::uint64_t generation) { return current(binding,generation); }
};
struct ReloadCompileSink {
    bool (*submit)(const b::legacy::Address &,std::uint8_t,std::span<const std::uint8_t,28>);
    bool Submit(const b::legacy::Address &peer,std::uint8_t kind,std::span<const std::uint8_t,28> key) {
        return submit(peer,kind,key);
    }
};
bool CompileReloadPrepare(b::reload::SelectiveKeys &keys,std::span<const std::uint8_t> committed,
        const b::record::Binding &binding,std::uint64_t generation,std::uint8_t minimum,b::legacy::Layout layout) {
    return keys.Prepare(committed,binding,generation,minimum,layout);
}
b::reload::Receipt CompileReloadSubmit(b::reload::SelectiveKeys &keys,ReloadCompileGuard &g,ReloadCompileSink &s) {
    return keys.Submit(g,s);
}
void CompileReloadCancel(b::reload::SelectiveKeys &keys) { keys.Cancel(); }
