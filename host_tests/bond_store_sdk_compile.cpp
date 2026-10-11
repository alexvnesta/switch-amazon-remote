// SPDX-License-Identifier: GPL-2.0-only
// Compile-only against real libnx. Not an entry point or console deployment.
#include "../mc_mitm/source/amazon_remote/bond_store.hpp"
namespace bond=amazon_remote::bond;
struct CompileGuard {
    bool (*current)(const bond::Request &,const bond::Completion &);
    std::uint64_t (*now)();
    bool Current(const bond::Request &r,const bond::Completion &c) { return current(r,c); }
    std::uint64_t Now() { return now(); }
};
bond::Receipt CompileStore(const bond::Request &r,const bond::Completion &c,
                          const bond::record::Binding &b,const bond::record::Materials &keys,
                          std::uint32_t mask,CompileGuard &guard) {
    bond::save_io::DeviceSlot io;
    bond::PrivateStore store(r,c,b,keys,mask,guard,io);
    return store.Run(guard.Now());
}
