// SPDX-License-Identifier: GPL-2.0-only
// Compile-only instantiation, no entry point or console binary.
#include "../mc_mitm/source/amazon_remote/bond_candidate.hpp"
namespace b=amazon_remote::bond;
struct CandidateCompileGuard {
    bool (*capture)(const b::Request &,const b::record::Binding &);
    bool (*current)(const b::Request &,const b::Completion &);
    std::uint64_t (*now)();
    bool CaptureCurrent(const b::Request &r,const b::record::Binding &binding) { return capture(r,binding); }
    bool Current(const b::Request &r,const b::Completion &c) { return current(r,c); }
    std::uint64_t Now() { return now(); }
};
bool CompileBegin(b::candidate::Collector &c,const b::candidate::Plan &p,CandidateCompileGuard &g) { return c.Begin(p,g); }
b::candidate::EventResult CompileKey(b::candidate::Collector &c,const b::Owner &o,std::span<const std::uint8_t> frame,CandidateCompileGuard &g) { return c.Key(o,frame,g); }
b::candidate::EventResult CompileComplete(b::candidate::Collector &c,const b::candidate::NativeCompletion &e,CandidateCompileGuard &g) { return c.Complete(e,g); }
bool CompilePoll(b::candidate::Collector &c,CandidateCompileGuard &g) { return c.Poll(g); }
b::Receipt CompilePersist(b::candidate::Collector &c,CandidateCompileGuard &g) {
    b::save_io::DeviceSlot io;return c.Persist(g,io);
}
