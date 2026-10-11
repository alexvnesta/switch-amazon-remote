// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "bond_record.hpp"

namespace amazon_remote::bond::native_confirmation {
inline constexpr std::size_t EntryBytes=0x1e0;
// Private value comparison only. No native lookup, pointer traversal, file or
// IPC input, key getter, loader, radio call or production caller is provided.
// The trusted adapter must obtain EXACTLY its owned BLE-only entry on the
// native stack's serialized path after the final selective store. That same
// serialization must cover the immutable record and both Current checks.
// Current independently verifies driver epoch, target/type, controller/root
// context and committed generation. This function is NOT that ownership proof.
// A true result is field equality now, not durable storage, remote retention,
// link encryption, reboot reconnect, or permission to reuse a stale result.
template<class Guard> bool Check(std::span<const std::uint8_t> committed,
        const record::Binding &binding,std::uint64_t generation,std::uint8_t minimum_key_bytes,
        legacy::Layout layout,std::span<const std::uint8_t> entry,Guard &guard) {
    if(layout!=legacy::Layout::AuditedC91Legacy || entry.size()!=EntryBytes || !generation)
        return false;
    record::Metadata metadata;
    if(!record::Inspect(committed,binding,minimum_key_bytes,metadata) || !metadata.occupied ||
       metadata.generation!=generation || !guard.Current(binding,generation)) return false;
    const auto flags=std::uint16_t(entry[0x46]) | std::uint16_t(entry[0x47])<<8;
    if((flags&0x1080)!=0x1080 || bool(flags&0x2000)!=metadata.mitm ||
       entry[0x16c]!=binding.peer_type || entry[0x1c4]!=2 || entry[0x17b]!=metadata.mask ||
       !std::equal(binding.peer.begin(),binding.peer.end(),entry.begin()+0x2c)) return false;
    // Meaningful fields of this exact pre-SC layout. Native padding, names and
    // unrelated connection state are not compared. Shared fields are checked
    // for each present record; record::Inspect already rejects contradictions.
    struct Field { std::uint8_t slot,source,length;std::uint16_t native; };
    constexpr Field fields[]{
        {0,0,16,0x18c},{0,16,8,0x1ac},{0,24,2,0x1b4},{0,26,1,0x1b8},{0,27,1,0x1b9},
        {1,0,16,0x17c},{1,16,1,0x16d},{1,17,6,0x16e},
        {2,0,4,0x1bc},{2,4,16,0x19c},{2,20,1,0x1ba},
        {3,0,2,0x1b6},{3,2,1,0x1b9},{3,3,1,0x1b8},
        {4,0,4,0x1c0},{4,4,2,0x1b6},{4,6,1,0x1bb},
    };
    std::uint8_t different{};
    for(const auto &field:fields) if(metadata.mask&record::Kinds[field.slot]) {
        const auto offset=record::SlotsOffset+field.slot*record::SlotBytes+4+field.source;
        for(unsigned i=0;i<field.length;++i) different|=committed[offset+i]^entry[field.native+i];
    }
    // Always finish the ownership check after comparing material. No key bytes
    // or per-key mismatch details leave this private function.
    return guard.Current(binding,generation) && !different;
}
}
