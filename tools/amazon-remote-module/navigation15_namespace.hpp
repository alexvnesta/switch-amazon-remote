// SPDX-License-Identifier: GPL-2.0-only
#pragma once
// Same temporary program ID; separate evidence/intent namespace per trial.
#if defined(ARMODULE_AUTOMATIC_A1)
#define ARMODULE_NAV15_CONFIG "sdmc:/config/amazon-remote-auto-a1"
#define ARMODULE_NAV15_CONFIG_FS "/config/amazon-remote-auto-a1"
#elif defined(ARMODULE_NATIVE_HCI_C2)
#define ARMODULE_NAV15_CONFIG "sdmc:/config/amazon-remote-hci-c2"
#define ARMODULE_NAV15_CONFIG_FS "/config/amazon-remote-hci-c2"
#elif defined(ARMODULE_NATIVE_PEER_C1)
#define ARMODULE_NAV15_CONFIG "sdmc:/config/amazon-remote-peer-c1"
#define ARMODULE_NAV15_CONFIG_FS "/config/amazon-remote-peer-c1"
#elif defined(ARMODULE_NAV17)
#define ARMODULE_NAV15_CONFIG "sdmc:/config/amazon-remote-nav17"
#define ARMODULE_NAV15_CONFIG_FS "/config/amazon-remote-nav17"
#elif defined(ARMODULE_NAV16)
#define ARMODULE_NAV15_CONFIG "sdmc:/config/amazon-remote-nav16"
#define ARMODULE_NAV15_CONFIG_FS "/config/amazon-remote-nav16"
#elif defined(ARMODULE_NAV15_SYSTEM)
#define ARMODULE_NAV15_CONFIG "sdmc:/config/amazon-remote-nav15s"
#define ARMODULE_NAV15_CONFIG_FS "/config/amazon-remote-nav15s"
#else
#define ARMODULE_NAV15_CONFIG "sdmc:/config/amazon-remote-nav15"
#define ARMODULE_NAV15_CONFIG_FS "/config/amazon-remote-nav15"
#endif
