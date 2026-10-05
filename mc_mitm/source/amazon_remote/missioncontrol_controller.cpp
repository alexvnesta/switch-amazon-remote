// SPDX-License-Identifier: GPL-2.0-only
#include "missioncontrol_controller.hpp"

namespace ams::controller {
bool AmazonBleController::SetDecodedButtons(std::uint32_t remote) {
    // The input must come from the validated decoder, not an HDLS button mask.
    if (remote&~std::uint32_t(0xffff)) { ClearRemoteState();return false; }
    m_buttons=amazon_remote::MissionControlButtons<SwitchButtons,SwitchButton>(remote);
    m_left_stick.Reset();m_right_stick.Reset();
    return true;
}
void AmazonBleController::ClearRemoteState() { Reset(); }
std::size_t AmazonBleController::EncodeBasicNavigationReport(std::uint8_t *out,std::size_t capacity) {
    if (!out || capacity<sizeof(SwitchHidBasicInputReport)) return 0;
    // Upstream formatter, not a duplicate Switch protocol implementation.
    // Give upstream an aligned, initialized structure rather than cast an
    // arbitrary caller byte pointer; copy only the complete encoded report.
    SwitchHidBasicInputReport report{};
    const auto size=FillBasicInputReport(reinterpret_cast<u8 *>(&report),sizeof report);
    if(size!=sizeof report)return 0;
    std::memcpy(out,&report,size);return size;
}
}
