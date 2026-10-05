// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../controllers/emulated_switch_controller.hpp"
#include "missioncontrol_buttons.hpp"

namespace ams::controller {
// Local integration proof, NOT registered in MissionControl's factory.
// Reuses upstream controller state/report encoding. It does not fabricate a
// classic HID connection, touch bonds, inject reports or initialize Bluetooth.
class AmazonBleController final : public EmulatedSwitchController {
public:
    explicit AmazonBleController(bluetooth::Address address)
        :EmulatedSwitchController(address,{0x0171,0x0427}) {}
    bool SetDecodedButtons(std::uint32_t remote);
    void ClearRemoteState();
    std::size_t EncodeBasicNavigationReport(std::uint8_t *out,std::size_t capacity);
};
}
