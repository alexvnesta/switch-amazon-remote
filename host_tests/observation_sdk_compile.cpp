// SPDX-License-Identifier: GPL-2.0-only
// Compile-only, never a console entry point. Exercise the real target C++/C
// filesystem declarations without providing any Bluetooth or service access.
#include "../tools/amazon-remote-module/control/observation_checkpoint_io.hpp"
#include "../mc_mitm/source/amazon_remote/controller_passive.hpp"

bool CompileCheckpoint(arobservation::Journal &journal, const char *directory,
                       std::uint64_t run, std::uint64_t context, std::uint64_t time,
                       const std::array<std::uint8_t,32> &manifest) {
    arobservation::FileStore disk(directory);
    return journal.Start(disk,run,context,time,manifest) &&
           journal.Advance(disk,arobservation::Stage::BeforeRead,time);
}
bool CompilePassiveDecode(const void *data,std::size_t size,
                          amazon_remote::controller_capabilities::Reply &reply) {
    return amazon_remote::controller_passive::Decode(data,size,reply);
}
arobservation::Evidence CompileInspect(const char *directory) {
    arobservation::FileStore disk(directory);
    return arobservation::Inspect(disk);
}
