// SPDX-License-Identifier: GPL-2.0-only
// Pure policy. No radio calls. Result0 is API acceptance, not RF/installation proof.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace amazon_remote {
// Wire offsets pinned by the SDK sizeof/offsetof checks and stored BTM image.
// This is deliberately NOT a general scan-filter editor.
using ManufacturerFilterWire = std::array<std::uint8_t, 62>;
inline constexpr std::uint64_t FilterBtmProgram = 0x010000000000002aull;
inline constexpr std::array<std::uint8_t, 6> AmazonManufacturerPrefix{
    0x71, 0x01, 0x04, 0x27, 0x02, 0x7d};

constexpr ManufacturerFilterWire OriginalAmazonGeneralFilter() {
    ManufacturerFilterWire filter{};
    // General profile index0, condition size10, AD type FF.
    filter[1] = 10; filter[2] = 0xff;
    constexpr std::array<std::uint8_t, 10> generated{
        0x71,0x01,0x01,0x04,0x27,0x02,0x7d,0,0,0};
    for (std::size_t i=0;i<generated.size();++i) filter[3+i]=generated[i];
    for (std::size_t i=0;i<9;++i) filter[32+i]=0xff;
    filter[61]=9;
    return filter;
}
constexpr ManufacturerFilterWire CorrectedAmazonGeneralFilter() {
    ManufacturerFilterWire filter{};
    filter[1]=AmazonManufacturerPrefix.size(); filter[2]=0xff;
    for (std::size_t i=0;i<AmazonManufacturerPrefix.size();++i) {
        filter[3+i]=AmazonManufacturerPrefix[i]; filter[32+i]=0xff;
    }
    filter[61]=AmazonManufacturerPrefix.size();
    return filter;
}
inline bool ExactOriginalAmazonFilter(const void *data,std::size_t size) {
    if (!data || size!=62) return false;
    const auto expected=OriginalAmazonGeneralFilter();
    const auto *bytes=static_cast<const std::uint8_t *>(data);
    return std::equal(expected.begin(),expected.end(),bytes);
}
// IPC owner must serialize calls. Plans never mutate caller buffers. A plan
// must be committed once with the actual forwarding result, not presumed0.
// Cleanup is permitted after lease expiry ONLY for our exact retained owner.
class ManufacturerFilterCorrection {
public:
    enum class State { Unused, Adding, Installed, Removing, Removed, Failed, RemovalFailed };
    enum class Operation { PassOriginal, CorrectAdd, CorrectDelete };
    struct Plan {
        Operation operation{Operation::PassOriginal};
        ManufacturerFilterWire forwarded{};
    };
    Plan Add(std::uint64_t program,std::uint64_t process,
             bool opted_in,bool active_lease,const void *data,std::size_t size) {
        if (state_!=State::Unused || !opted_in || !active_lease ||
            program!=FilterBtmProgram || !process || !ExactOriginalAmazonFilter(data,size)) return {};
        process_=process;state_=State::Adding;
        return {Operation::CorrectAdd,CorrectedAmazonGeneralFilter()};
    }
    Plan Delete(std::uint64_t program,std::uint64_t process,const void *data,std::size_t size) {
        if ((state_!=State::Installed && state_!=State::RemovalFailed) || program!=FilterBtmProgram || process!=process_ ||
            !ExactOriginalAmazonFilter(data,size)) return {};
        state_=State::Removing;
        return {Operation::CorrectDelete,CorrectedAmazonGeneralFilter()};
    }
    bool Commit(Operation operation,std::uint32_t result) {
        if (operation==Operation::CorrectAdd && state_==State::Adding) {
            result_=result;state_=result?State::Failed:State::Installed;return true;
        }
        if (operation==Operation::CorrectDelete && state_==State::Removing) {
            result_=result;state_=result?State::RemovalFailed:State::Removed;return true;
        }
        return false;
    }
    State Current() const {return state_;}
    std::uint32_t Result() const {return result_;}
    // No reset/rearm, slot allocation/deletion, global clear, wildcard,
    // target identity assertion, pairing, connection or automatic retry.
private:
    State state_{State::Unused};
    std::uint64_t process_{};
    std::uint32_t result_{};
};
}
