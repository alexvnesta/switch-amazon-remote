// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "setup_view.hpp"
#include "../../../mc_mitm/source/amazon_remote/central_bond_wire.hpp"
#include <cstring>
namespace arsetupview {
// Presentation only. Preserve power/preflight errors and terminal cleanup states.
inline View SelectSavedDiscovery(View base,const Input &i,std::uint32_t query,
                                 const amazon_remote::central_bond::Status &bond,bool native_peer=false) {
    namespace cb=amazon_remote::central_bond;
    if(!i.fresh || i.result || i.cleanup || i.state>=4 || base.tone==Tone::Error) return base;
    if(query || bond.magic!=cb::Magic || bond.version!=cb::Version || bond.store!=cb::Store::Saved)
        return {"SAVED BOND NOT AVAILABLE","+ Exit and open FTPD.",
            "No new enrollment. Existing pairing is preserved.",Tone::Error};
    if(bond.phase==cb::Phase::Failed)
        return {"SAVED LINK FAILED","Joy-Con X if needed, then + and FTPD.",
            "No automatic re-pair or saved-key replacement.",Tone::Error};
    if(i.state==0 && !std::strcmp(base.status,"READY TO SEARCH"))
        return {"SAVED BOND READY",native_peer?"Joy-Con A once to prepare Bluetooth.":"Joy-Con A once to start discovery.",
            "Do not hold remote Home or enter pairing mode.",Tone::Good};
    if(i.state==1 && native_peer)
        return {i.link_ready?"READY TO RECONNECT":"PREPARING BLUETOOTH",
            i.link_ready?"Tap remote Up, then Joy-Con ZL ONCE.":"Wait for READY TO RECONNECT. Do not press ZL yet.",
            "Do not hold remote Home or enter pairing mode.",i.link_ready?Tone::Good:Tone::Waiting};
    if(i.state==1)
        return {i.link_ready?"REMOTE SEEN":"SEARCHING SAVED REMOTE",
            i.link_ready?"Joy-Con ZL ONCE. Then wait.":"Tap remote Up. Wait for REMOTE SEEN before ZL.",
            "Do not hold Home or put the remote in pairing mode.",i.link_ready?Tone::Good:Tone::Waiting};
    if(i.state==2)
        return {bond.phase==cb::Phase::Idle?"CONNECTING TO REMOTE":"SECURING SAVED LINK",
            "Wait here. Do not press ZL again.","Input waits for encryption and identity checks.",Tone::Waiting};
    if(i.state==3 && i.pad_attached && bond.phase==cb::Phase::Ready && !bond.result &&
       !bond.native_status && bond.encrypted==1 && bond.key_bytes==16)
        return {"CONNECTED / SAVED BOND","Try remote directions, Select and Back.",
            "Then Joy-Con X once. Wait for DISCONNECTED; + exits.",Tone::Good};
    return base;
}
}
